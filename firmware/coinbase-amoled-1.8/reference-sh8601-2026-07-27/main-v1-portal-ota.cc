#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "feed_config.h"
#include "network_portal.h"

#define PROGMEM
#include "glcdfont.h"

static const char *TAG = "coinbase-display";
static constexpr int W=368,H=448,TX_LINES=16;
static constexpr uint32_t REFRESH_MS=30000, STALE_MS=75000;
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t lcd_io;
static i2c_master_dev_handle_t touch_dev;
static uint16_t *fb, *txbuf;
static SemaphoreHandle_t tx_done;
static bool wifi_up=false, detail=false, force_fetch=true;
static uint64_t last_ok_ms=0;
static std::string status="STARTING";
struct Position { bool open=false; std::string side="-"; double size=0,entry=0,pnl=0; };
struct ClosedPosition { std::string symbol="-",side="-"; double size=0,pnl=0; };
struct Asset { const char *name; double price=0; Position pos; } assets[]={{"BTC"},{"SOL"},{"XLM"},{"HYPE"},{"ETH"}};
static std::vector<ClosedPosition> closed_today;
static double balance=0, total_pnl=0, realized_pnl_today=0;
static std::string realized_date="---- -- --";

static uint16_t rgb(uint8_t r,uint8_t g,uint8_t b){ return __builtin_bswap16(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)); }
static const uint16_t BLACK=rgb(5,8,15),CARD=rgb(18,24,38),MUTED=rgb(139,148,166),WHITE=rgb(245,247,250),GREEN=rgb(48,209,88),RED=rgb(255,69,58),BLUE=rgb(55,126,255),AMBER=rgb(255,180,0);
static void rect(int x,int y,int w,int h,uint16_t c){ x=std::max(0,x); y=std::max(0,y); w=std::min(w,W-x); h=std::min(h,H-y); for(int yy=y;yy<y+h;yy++) std::fill(fb+yy*W+x,fb+yy*W+x+w,c); }
static void text(int x,int y,const char*s,uint16_t c,int scale=2){ for(;*s;s++,x+=6*scale){ unsigned ch=(unsigned char)*s; if(ch<32||ch>127) ch='?'; for(int i=0;i<5;i++){ uint8_t col=font[ch*5+i]; for(int j=0;j<8;j++) if(col&(1<<j)) rect(x+i*scale,y+j*scale,scale,scale,c); } } }
static void fmt_money(char *b,size_t n,double v){ double a=fabs(v); if(a>=10000) snprintf(b,n,"$%.0f",v); else if(a>=100) snprintf(b,n,"$%.2f",v); else if(a>=1) snprintf(b,n,"$%.3f",v); else snprintf(b,n,"$%.5f",v); }
static void button(int x,int y,int w,const char *label,uint16_t c){ rect(x,y,w,36,c); text(x+10,y+10,label,WHITE,2); }
static void draw(){
  rect(0,0,W,H,BLACK); char b[64];
  auto& network=NetworkPortal::GetInstance();
  if(network.IsPortalActive()){
    text(20,24,"COINBASE SETUP",WHITE,3);
    text(20,82,"CONNECT WI-FI:",MUTED,2);
    text(20,112,network.GetApSsid().c_str(),BLUE,2);
    text(20,158,"OPEN:",MUTED,2); text(20,186,"http://192.168.4.1",WHITE,2);
    text(20,238,"SAVE WI-FI TO RESTART",MUTED,2);
    if(network.IsOtaArmed()){
      text(20,302,"OTA ARMED - 5 MIN",AMBER,2);
      snprintf(b,sizeof(b),"CODE %s",network.GetOtaCode().c_str()); text(20,336,b,WHITE,3);
    } else { text(20,302,"OTA LOCKED",GREEN,2); text(20,332,"HOLD BOOT 10S TO ARM",MUTED,2); }
    text(20,410,"AP CLIENTS ONLY",MUTED,1);
    for(int y=0;y<H;y++){ memcpy(txbuf,fb+y*W,W*sizeof(uint16_t)); ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,y,W,y+1,txbuf)); }
    return;
  }
  uint64_t now=esp_timer_get_time()/1000; bool stale=!last_ok_ms||now-last_ok_ms>STALE_MS;
  text(16,14,"COINBASE",WHITE,3); text(224,18, stale?"STALE":(wifi_up?"LIVE":"OFFLINE"),stale?AMBER:GREEN,2);
  rect(16,48,336,66,CARD); text(28,60,"PORTFOLIO",MUTED,1); fmt_money(b,sizeof(b),balance); text(28,78,b,WHITE,3); snprintf(b,sizeof(b),"UPL %+.2f",total_pnl); text(216,79,b,total_pnl>=0?GREEN:RED,2); snprintf(b,sizeof(b),"RPL TODAY %+.2f",realized_pnl_today); text(216,103,b,realized_pnl_today>=0?GREEN:RED,1);
  if(!detail){
    int y=126; for(auto &a:assets){ rect(16,y,336,47,CARD); text(28,y+7,a.name,WHITE,2); fmt_money(b,sizeof(b),a.price); text(98,y+7,b,WHITE,2); if(a.pos.open){ snprintf(b,sizeof(b),"%s %+.2f",a.pos.side.c_str(),a.pos.pnl); text(224,y+28,b,a.pos.pnl>=0?GREEN:RED,1); } else text(224,y+28,"NO POSITION",MUTED,1); y+=52; }
  } else {
    text(16,124,"OPEN POSITIONS",WHITE,2); int y=148, count=0; for(auto &a:assets) if(a.pos.open){ count++; rect(16,y,336,20,CARD); snprintf(b,sizeof(b),"%s %s %.5g @%.6g U%+.2f",a.name,a.pos.side.c_str(),a.pos.size,a.pos.entry,a.pos.pnl); text(24,y+6,b,a.pos.pnl>=0?GREEN:RED,1); y+=23; } if(!count) text(24,150,"NO OPEN POSITIONS",MUTED,1);
    text(16,276,"CLOSED TODAY",WHITE,2); y=300; int shown=0; for(auto &p:closed_today){ if(y>372) break; rect(16,y,336,20,CARD); snprintf(b,sizeof(b),"%s %s %.5g R%+.2f",p.symbol.c_str(),p.side.c_str(),p.size,p.pnl); text(24,y+6,b,p.pnl>=0?GREEN:RED,1); y+=23; shown++; } if(!shown) text(24,302,"NONE YET",MUTED,1);
  }
  button(16,400,158,detail?"PRICES":"POSITIONS",BLUE); button(194,400,158,"REFRESH",CARD);
  snprintf(b,sizeof(b),"30S  %s",status.c_str()); text(16,390,b,stale?AMBER:MUTED,1);
  for(int y=0;y<H;y+=TX_LINES){ int rows=std::min(TX_LINES,H-y); memcpy(txbuf,fb+y*W,W*rows*sizeof(uint16_t)); ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,y,W,y+rows,txbuf)); if(xSemaphoreTake(tx_done,pdMS_TO_TICKS(2000))!=pdTRUE){ESP_LOGE(TAG,"LCD transfer timeout y=%d",y);break;} }
}
static bool read_touch(uint16_t& x,uint16_t& y){ uint8_t reg=0x02,points=0; if(i2c_master_transmit_receive(touch_dev,&reg,1,&points,1,30)!=ESP_OK||points==0||points>5)return false; uint8_t data[6]={}; reg=0x03; if(i2c_master_transmit_receive(touch_dev,&reg,1,data,6,30)!=ESP_OK)return false; x=((data[0]&0x0f)<<8)|data[1]; y=((data[2]&0x0f)<<8)|data[3]; return true; }
static esp_err_t http_evt(esp_http_client_event_t *e){ auto*v=(std::vector<char>*)e->user_data; if(e->event_id==HTTP_EVENT_ON_DATA){ const char* p=static_cast<const char*>(e->data); v->insert(v->end(),p,p+e->data_len); } return ESP_OK; }
static double num(cJSON*o,const char*k){ cJSON*x=cJSON_GetObjectItemCaseSensitive(o,k); return cJSON_IsNumber(x)?x->valuedouble:0; }
static bool fetch(){
  if(!wifi_up){ status="NO WIFI"; return false; } std::vector<char> body; std::string auth="Bearer "+std::string(FEED_TOKEN);
  esp_http_client_config_t cfg={}; cfg.url=FEED_URL; cfg.event_handler=http_evt; cfg.user_data=&body; cfg.timeout_ms=12000; cfg.crt_bundle_attach=esp_crt_bundle_attach;
  auto h=esp_http_client_init(&cfg); esp_http_client_set_header(h,"Authorization",auth.c_str()); esp_http_client_set_header(h,"X-Device-ID",DEVICE_ID); esp_err_t err=esp_http_client_perform(h); int code=esp_http_client_get_status_code(h); esp_http_client_cleanup(h);
  ESP_LOGI(TAG,"feed http=%d err=%s bytes=%u",code,esp_err_to_name(err),(unsigned)body.size());
  if(err!=ESP_OK||code!=200){ status="HTTP "+std::to_string(code); return false; } body.push_back(0); cJSON*d=cJSON_Parse(body.data()); if(!d){status="JSON ERROR";ESP_LOGE(TAG,"feed JSON parse failed");return false;}
  cJSON*ro=cJSON_GetObjectItem(d,"read_only"); if(!cJSON_IsTrue(ro)){ status="UNSAFE FEED"; cJSON_Delete(d); return false; }
  cJSON*prices=cJSON_GetObjectItem(d,"prices"),*positions=cJSON_GetObjectItem(d,"positions"),*portfolio=cJSON_GetObjectItem(d,"portfolio"); balance=num(portfolio,"balance"); total_pnl=num(portfolio,"unrealized_pnl"); realized_pnl_today=num(portfolio,"realized_pnl_today");
  for(auto&a:assets){ a.price=num(prices,a.name); a.pos=Position{}; cJSON*p=cJSON_GetObjectItem(positions,a.name); if(cJSON_IsObject(p)){ a.pos.open=true; cJSON*s=cJSON_GetObjectItem(p,"side"); if(cJSON_IsString(s))a.pos.side=s->valuestring; a.pos.size=num(p,"contracts"); if(a.pos.size==0) a.pos.size=num(p,"size"); a.pos.entry=num(p,"entry"); a.pos.pnl=num(p,"pnl"); } }
  closed_today.clear(); cJSON*closed=cJSON_GetObjectItem(d,"closed_today"); cJSON*item=nullptr; cJSON_ArrayForEach(item,closed){ ClosedPosition p; cJSON*s=cJSON_GetObjectItem(item,"symbol"); if(cJSON_IsString(s))p.symbol=s->valuestring; s=cJSON_GetObjectItem(item,"side"); if(cJSON_IsString(s))p.side=s->valuestring; p.size=num(item,"contracts"); p.pnl=num(item,"pnl"); closed_today.push_back(p); }
  cJSON*rd=cJSON_GetObjectItem(d,"realized_date"); if(cJSON_IsString(rd))realized_date=rd->valuestring; int open_count=0; for(auto&a:assets)if(a.pos.open)open_count++;
  ESP_LOGI(TAG,"parsed bal=%.2f open=%d upl=%.2f rpl_today=%.2f closed=%u date=%s",balance,open_count,total_pnl,realized_pnl_today,(unsigned)closed_today.size(),realized_date.c_str());
  cJSON_Delete(d); last_ok_ms=esp_timer_get_time()/1000; status="UPDATED"; return true;
}
static void i2c_write(i2c_master_bus_handle_t bus,uint8_t addr,uint8_t reg,uint8_t val){ i2c_master_dev_handle_t dev; i2c_device_config_t c={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=addr,.scl_speed_hz=400000}; ESP_ERROR_CHECK(i2c_master_bus_add_device(bus,&c,&dev)); uint8_t d[]={reg,val}; ESP_ERROR_CHECK(i2c_master_transmit(dev,d,2,100)); i2c_master_bus_rm_device(dev); }
static bool color_done(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*){ BaseType_t wake=pdFALSE; xSemaphoreGiveFromISR(tx_done,&wake); return wake==pdTRUE; }
static void init_hw(){
  i2c_master_bus_handle_t bus; i2c_master_bus_config_t bc={.i2c_port=I2C_NUM_0,.sda_io_num=GPIO_NUM_15,.scl_io_num=GPIO_NUM_14,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags={.enable_internal_pullup=1}}; ESP_ERROR_CHECK(i2c_new_master_bus(&bc,&bus));
  esp_io_expander_handle_t ex; ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(bus,ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000,&ex)); ESP_ERROR_CHECK(esp_io_expander_set_dir(ex,IO_EXPANDER_PIN_NUM_0|IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2,IO_EXPANDER_OUTPUT)); ESP_ERROR_CHECK(esp_io_expander_set_dir(ex,IO_EXPANDER_PIN_NUM_4,IO_EXPANDER_INPUT)); ESP_ERROR_CHECK(esp_io_expander_set_level(ex,IO_EXPANDER_PIN_NUM_0|IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2,1)); vTaskDelay(pdMS_TO_TICKS(100)); ESP_ERROR_CHECK(esp_io_expander_set_level(ex,IO_EXPANDER_PIN_NUM_0|IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2,0)); vTaskDelay(pdMS_TO_TICKS(300)); ESP_ERROR_CHECK(esp_io_expander_set_level(ex,IO_EXPANDER_PIN_NUM_0|IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2,1));
  i2c_write(bus,0x34,0x22,0x06); i2c_write(bus,0x34,0x27,0x10); i2c_write(bus,0x34,0x80,0x01); i2c_write(bus,0x34,0x90,0x00); i2c_write(bus,0x34,0x91,0x00); i2c_write(bus,0x34,0x82,18); i2c_write(bus,0x34,0x92,28); i2c_write(bus,0x34,0x90,0x01); i2c_write(bus,0x34,0x64,0x02); i2c_write(bus,0x34,0x61,0x02); i2c_write(bus,0x34,0x62,0x08); i2c_write(bus,0x34,0x63,0x01); vTaskDelay(pdMS_TO_TICKS(300));
  for(int a=1;a<0x7f;a++) if(i2c_master_probe(bus,a,30)==ESP_OK) ESP_LOGI(TAG,"i2c device 0x%02x",a);
  spi_bus_config_t sb={}; sb.sclk_io_num=11;sb.data0_io_num=4;sb.data1_io_num=5;sb.data2_io_num=6;sb.data3_io_num=7;sb.max_transfer_sz=W*H*2;sb.flags=SPICOMMON_BUSFLAG_QUAD; ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&sb,SPI_DMA_CH_AUTO));
  tx_done=xSemaphoreCreateBinary(); esp_lcd_panel_io_spi_config_t io=SH8601_PANEL_IO_QSPI_CONFIG(12,color_done,nullptr); ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST,&io,&lcd_io));
  static const sh8601_lcd_init_cmd_t cmds[]={{0x11,(uint8_t[]){0},0,120},{0x44,(uint8_t[]){1,0xD1},2,0},{0x35,(uint8_t[]){0},1,0},{0x53,(uint8_t[]){0x20},1,10},{0x2A,(uint8_t[]){0,0,1,0x6F},4,0},{0x2B,(uint8_t[]){0,0,1,0xBF},4,0},{0x51,(uint8_t[]){0xC0},1,10},{0x29,(uint8_t[]){0},0,10}}; sh8601_vendor_config_t vc={.init_cmds=cmds,.init_cmds_size=sizeof(cmds)/sizeof(cmds[0]),.flags={.use_qspi_interface=1}}; esp_lcd_panel_dev_config_t pc={};pc.reset_gpio_num=GPIO_NUM_NC;pc.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;pc.bits_per_pixel=16;pc.vendor_config=&vc; ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(lcd_io,&pc,&panel)); ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));ESP_ERROR_CHECK(esp_lcd_panel_init(panel));ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true)); uint8_t brightness=0xC0; int brightness_cmd=(0x51<<8)|(0x02ULL<<24); ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io,brightness_cmd,&brightness,1));
  i2c_device_config_t td={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x38,.scl_speed_hz=400000}; ESP_ERROR_CHECK(i2c_master_bus_add_device(bus,&td,&touch_dev)); gpio_config_t gi={.pin_bit_mask=1ULL<<GPIO_NUM_21,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE}; gpio_config(&gi);
  gpio_config_t gb={.pin_bit_mask=1ULL<<GPIO_NUM_0,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE}; gpio_config(&gb);
  fb=(uint16_t*)heap_caps_malloc(W*H*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); txbuf=(uint16_t*)heap_caps_malloc(W*TX_LINES*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL); if(!fb||!txbuf||!tx_done) abort();
}
extern "C" void app_main(){ esp_err_t n=nvs_flash_init();if(n==ESP_ERR_NVS_NO_FREE_PAGES||n==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());n=nvs_flash_init();}ESP_ERROR_CHECK(n); init_hw(); n=esp_ota_mark_app_valid_cancel_rollback();if(n!=ESP_OK&&n!=ESP_ERR_OTA_ROLLBACK_INVALID_STATE)ESP_LOGW(TAG,"unable to confirm OTA image: %s",esp_err_to_name(n)); draw();
  auto& network=NetworkPortal::GetInstance();
  network.Initialize([](bool connected){wifi_up=connected;if(connected)force_fetch=true;},[](){});
  uint64_t last_fetch=0,last_draw=0,button_at=0;bool down=false,button_down=false,ota_hold_handled=false,shown_portal=false,shown_armed=false;
  while(true){ uint64_t now=esp_timer_get_time()/1000; bool portal=network.IsPortalActive(),armed=network.IsOtaArmed(); if(portal!=shown_portal||armed!=shown_armed){shown_portal=portal;shown_armed=armed;draw();last_draw=now;} if(!portal&&(force_fetch||now-last_fetch>=REFRESH_MS)){force_fetch=false;last_fetch=now;fetch();draw();last_draw=now;} bool pressed=false; uint16_t x=0,y=0; pressed=read_touch(x,y); if(!portal&&pressed&&!down){ESP_LOGI(TAG,"touch x=%u y=%u",x,y); if(y>=390){if(x<184)detail=!detail;else force_fetch=true;draw();last_draw=now;}}down=pressed; bool b=gpio_get_level(GPIO_NUM_0)==0; if(b&&!button_down){button_at=now;ota_hold_handled=false;} if(b&&!ota_hold_handled&&now-button_at>=10000){network.ArmOta();ota_hold_handled=true;draw();last_draw=now;} if(!b&&button_down&&!ota_hold_handled&&!portal){if(now-button_at>=800)force_fetch=true;else detail=!detail;draw();last_draw=now;} button_down=b; if(now-last_draw>30000){draw();last_draw=now;}vTaskDelay(pdMS_TO_TICKS(50)); }
}

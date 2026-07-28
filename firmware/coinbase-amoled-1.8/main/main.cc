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
#include "feed_config.h"
#if BOARD_IS_V1
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"
#else
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst816s.h"
#endif
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca9554.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "network_portal.h"

#define PROGMEM
#include "glcdfont.h"

static const char *TAG = "coinbase-display";
static constexpr int W=368,H=448,TX_LINES=16;
#if BOARD_IS_V1
static constexpr int PANEL_X_GAP=0;
#else
static constexpr int PANEL_X_GAP=0x10;
#endif
// V2 keeps the long diagnostic pauses it shipped with. V1 must reach touch init
// quickly: the FT5x06/FT3168 stops accepting register writes if the host leaves
// it idle for many seconds after power-on (the proven ClawBuddy flow inits it ~2s in).
static constexpr int PHASE_DELAY_MS=BOARD_IS_V1?100:2000;
static constexpr int BARS_HOLD_MS=BOARD_IS_V1?1500:5000;
static constexpr int HISTORY_SAMPLES=60;
static constexpr uint32_t REFRESH_MS=30000, STALE_MS=75000;
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t lcd_io;
static esp_lcd_touch_handle_t touch;
static uint16_t *fb, *txbuf;
static SemaphoreHandle_t tx_done;
static i2c_master_bus_handle_t i2c_bus;
static esp_io_expander_handle_t io_expander;
static bool wifi_up=false, detail=false, force_fetch=true;
static int selected_chart=-1;
static uint32_t feed_refresh_seconds=30,history_sample_seconds=30;
static uint64_t last_ok_ms=0;
static std::string status="STARTING";
struct Position { bool open=false; std::string side="-"; double size=0,entry=0,pnl=0; };
struct ClosedPosition { std::string symbol="-",side="-"; double size=0,pnl=0; };
struct Asset { const char *name; double price=0; Position pos; double history[HISTORY_SAMPLES]={}; uint8_t history_count=0,history_head=0; explicit Asset(const char*n):name(n){} };
static Asset assets[]={Asset("BTC"),Asset("SOL"),Asset("XLM"),Asset("HYPE"),Asset("ETH")};
static std::vector<ClosedPosition> closed_today;
static double balance=0, total_pnl=0, realized_pnl_today=0;
static std::string realized_date="---- -- --";

static uint16_t rgb(uint8_t r,uint8_t g,uint8_t b){ return __builtin_bswap16(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)); }
static const uint16_t BLACK=rgb(5,8,15),CARD=rgb(18,24,38),GRID=rgb(45,57,78),MUTED=rgb(190,198,214),WHITE=rgb(245,247,250),GREEN=rgb(48,209,88),RED=rgb(255,69,58),BLUE=rgb(55,126,255),AMBER=rgb(255,180,0);
static void rect(int x,int y,int w,int h,uint16_t c){ x=std::max(0,x); y=std::max(0,y); w=std::min(w,W-x); h=std::min(h,H-y); for(int yy=y;yy<y+h;yy++) std::fill(fb+yy*W+x,fb+yy*W+x+w,c); }
static int text_width(const char*s,int scale=2){ scale=std::max(2,scale); return (int)strlen(s)*6*scale; }
static void text(int x,int y,const char*s,uint16_t c,int scale=2){ scale=std::max(2,scale); for(;*s;s++,x+=6*scale){ unsigned ch=(unsigned char)*s; if(ch<32||ch>127) ch='?'; for(int i=0;i<5;i++){ uint8_t col=font[ch*5+i]; for(int j=0;j<8;j++) if(col&(1<<j)) rect(x+i*scale,y+j*scale,scale,scale,c); } } }
static void text_right(int right,int y,const char*s,uint16_t c,int scale=2){ text(std::max(0,right-text_width(s,scale)),y,s,c,scale); }
static void text_center(int left,int width,int y,const char*s,uint16_t c,int scale=2){ text(left+std::max(0,(width-text_width(s,scale))/2),y,s,c,scale); }
static void fmt_money(char *b,size_t n,double v){ double a=fabs(v); if(a>=10000) snprintf(b,n,"$%.0f",v); else if(a>=100) snprintf(b,n,"$%.2f",v); else if(a>=1) snprintf(b,n,"$%.3f",v); else snprintf(b,n,"$%.5f",v); }
static void button(int x,int y,int w,const char *label,uint16_t c){ rect(x,y,w,36,c); text(x+10,y+10,label,WHITE,2); }
static void pixel(int x,int y,uint16_t c){ if(x>=0&&x<W&&y>=0&&y<H)fb[y*W+x]=c; }
static void draw_line(int x0,int y0,int x1,int y1,uint16_t c,int thickness=1){
  int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
  while(true){ for(int yy=0;yy<thickness;yy++)pixel(x0,y0+yy,c); if(x0==x1&&y0==y1)break; int e2=2*err; if(e2>=dy){err+=dy;x0+=sx;} if(e2<=dx){err+=dx;y0+=sy;} }
}
static double history_at(const Asset&a,int i){ int start=(a.history_head+HISTORY_SAMPLES-a.history_count)%HISTORY_SAMPLES; return a.history[(start+i)%HISTORY_SAMPLES]; }
static void push_price(Asset&a,double value){ if(!(value>0)||!std::isfinite(value))return; a.history[a.history_head]=value; a.history_head=(a.history_head+1)%HISTORY_SAMPLES; if(a.history_count<HISTORY_SAMPLES)a.history_count++; }
static bool load_price_history(Asset&a,cJSON*root){
  if(!cJSON_IsObject(root))return false;
  cJSON*series=cJSON_GetObjectItemCaseSensitive(root,a.name);
  if(!cJSON_IsArray(series)||cJSON_GetArraySize(series)==0)return false;
  a.history_count=0;a.history_head=0;
  cJSON*item=nullptr;cJSON_ArrayForEach(item,series)if(cJSON_IsNumber(item))push_price(a,item->valuedouble);
  return a.history_count>0;
}
static bool history_bounds(const Asset&a,double&lo,double&hi){
  if(!a.history_count)return false;
  lo=hi=history_at(a,0);
  for(int i=1;i<a.history_count;i++){ double v=history_at(a,i); lo=std::min(lo,v); hi=std::max(hi,v); }
  return true;
}
static double history_change_pct(const Asset&a){ if(a.history_count<2)return 0; double first=history_at(a,0),last=history_at(a,a.history_count-1); return first?((last-first)/first)*100.0:0; }
static void sparkline(const Asset&a,int x,int y,int w,int h,bool full=false){
  if(full){ for(int i=1;i<4;i++)draw_line(x,y+(h*i)/4,x+w-1,y+(h*i)/4,GRID); }
  else draw_line(x,y+h/2,x+w-1,y+h/2,GRID);
  if(!a.history_count)return;
  double lo,hi; history_bounds(a,lo,hi); if(fabs(hi-lo)<1e-12){ double pad=std::max(fabs(hi)*0.0005,0.000001);lo-=pad;hi+=pad; }
  auto px=[&](int i){ return a.history_count<2?x+w/2:x+(i*(w-1))/(a.history_count-1); };
  auto py=[&](double v){ double n=(v-lo)/(hi-lo); n=std::max(0.0,std::min(1.0,n)); return y+h-1-(int)lround(n*(h-1)); };
  uint16_t color=a.history_count<2||history_at(a,a.history_count-1)>=history_at(a,0)?GREEN:RED;
  if(a.history_count==1){ rect(px(0)-2,py(history_at(a,0))-2,5,5,color);return; }
  for(int i=1;i<a.history_count;i++)draw_line(px(i-1),py(history_at(a,i-1)),px(i),py(history_at(a,i)),color,2);
}
static void history_window(char*b,size_t n,const Asset&a){
  uint32_t seconds=a.history_count>1?(a.history_count-1)*history_sample_seconds:0;
  if(seconds>=3600)snprintf(b,n,"%luH %02luM WINDOW",(unsigned long)(seconds/3600),(unsigned long)((seconds%3600)/60));
  else if(seconds>=60)snprintf(b,n,"%luM WINDOW",(unsigned long)(seconds/60));
  else snprintf(b,n,"%luS WINDOW",(unsigned long)seconds);
}
static void flush_frame(){ for(int y=0;y<H;y+=TX_LINES){ int rows=std::min(TX_LINES,H-y); memcpy(txbuf,fb+y*W,W*rows*sizeof(uint16_t)); ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,y,W,y+rows,txbuf)); if(xSemaphoreTake(tx_done,pdMS_TO_TICKS(2000))!=pdTRUE){ESP_LOGE(TAG,"LCD transfer timeout y=%d",y);break;} } }
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
    text(20,406,"AP CLIENTS ONLY",MUTED,2);
    flush_frame();
    return;
  }
  uint64_t now=esp_timer_get_time()/1000; bool stale=!last_ok_ms||now-last_ok_ms>STALE_MS;
  text(16,14,"COINBASE",WHITE,3);
  bool feed_problem=status!="UPDATED"&&status!="STARTING";
  if(stale)snprintf(b,sizeof(b),"STALE");
  else if(!wifi_up)snprintf(b,sizeof(b),"OFFLINE");
  else if(feed_problem)snprintf(b,sizeof(b),"%s",status.c_str());
  else snprintf(b,sizeof(b),"LIVE %luS",(unsigned long)feed_refresh_seconds);
  text_right(352,18,b,stale||feed_problem?AMBER:GREEN,2);
  rect(16,48,336,68,CARD);
  text(28,56,"PORTFOLIO",MUTED,2);
  fmt_money(b,sizeof(b),balance); text(28,80,b,WHITE,3);
  snprintf(b,sizeof(b),"UPL %+.2f",total_pnl); text_right(340,56,b,total_pnl>=0?GREEN:RED,2);
  snprintf(b,sizeof(b),"RPL %+.2f",realized_pnl_today); text_right(340,88,b,realized_pnl_today>=0?GREEN:RED,2);
  if(selected_chart>=0){
    auto&a=assets[selected_chart]; rect(16,124,336,260,CARD); text(28,134,a.name,WHITE,3); fmt_money(b,sizeof(b),a.price); text(112,138,b,WHITE,2);
    double pct=history_change_pct(a); snprintf(b,sizeof(b),"%+.2f%%",pct); text_right(340,138,b,pct>=0?GREEN:RED,2);
    text(28,164,"LIVE PRICE TREND",MUTED,2);
    sparkline(a,28,188,312,130,true);
    if(a.history_count<2)text_center(28,312,244,"COLLECTING",AMBER,2);
    double lo=0,hi=0; if(history_bounds(a,lo,hi)){ fmt_money(b,sizeof(b),lo); text(28,326,b,MUTED,2); fmt_money(b,sizeof(b),hi); text_right(340,326,b,MUTED,2); }
    history_window(b,sizeof(b),a); text(28,352,b,MUTED,2); text_right(340,352,"TAP < >",MUTED,2);
  } else if(!detail){
    int y=124; for(auto &a:assets){
      rect(16,y,336,50,CARD);
      text(24,y+4,a.name,WHITE,2); fmt_money(b,sizeof(b),a.price); text(80,y+4,b,WHITE,2); sparkline(a,208,y+4,134,24);
      if(a.pos.open){ snprintf(b,sizeof(b),"%c U%+.2f",a.pos.side.empty()?'-':a.pos.side[0],a.pos.pnl); text(24,y+29,b,a.pos.pnl>=0?GREEN:RED,2); }
      else text(24,y+29,"FLAT",MUTED,2);
      double pct=history_change_pct(a); if(a.history_count>=2){snprintf(b,sizeof(b),"%+.2f%%",pct);text_right(340,y+29,b,pct>=0?GREEN:RED,2);}
      y+=52;
    }
  } else {
    int open_total=0; for(auto&a:assets)if(a.pos.open)open_total++;
    snprintf(b,sizeof(b),"OPEN POSITIONS %d",open_total); text(16,124,b,WHITE,2);
    int y=148,shown=0;
    for(auto&a:assets)if(a.pos.open&&shown<2){
      rect(16,y,336,42,CARD);
      snprintf(b,sizeof(b),"%s %s",a.name,a.pos.side.c_str()); text(24,y+4,b,WHITE,2);
      snprintf(b,sizeof(b),"U%+.2f",a.pos.pnl); text_right(340,y+4,b,a.pos.pnl>=0?GREEN:RED,2);
      char entry[24]; fmt_money(entry,sizeof(entry),a.pos.entry); snprintf(b,sizeof(b),"%.4g @ %s",a.pos.size,entry); text(24,y+23,b,MUTED,2);
      y+=44;shown++;
    }
    if(!open_total)text(24,150,"NONE",MUTED,2);
    else if(open_total>shown){snprintf(b,sizeof(b),"+%d MORE",open_total-shown);text(24,238,b,AMBER,2);}
    snprintf(b,sizeof(b),"CLOSED TODAY %u",(unsigned)closed_today.size()); text(16,258,b,WHITE,2);
    y=282;shown=0;
    for(auto&p:closed_today){ if(shown>=2)break; rect(16,y,336,48,CARD); snprintf(b,sizeof(b),"%s %s",p.symbol.c_str(),p.side.c_str()); text(24,y+4,b,WHITE,2); snprintf(b,sizeof(b),"R%+.2f",p.pnl); text_right(340,y+4,b,p.pnl>=0?GREEN:RED,2); snprintf(b,sizeof(b),"SIZE %.4g",p.size); text(24,y+27,b,MUTED,2); y+=50; shown++; }
    if(!shown)text(24,284,"NONE YET",MUTED,2);
  }
  button(16,400,158,selected_chart>=0||detail?"PRICES":"POSITIONS",BLUE); button(194,400,158,"REFRESH",CARD);
  flush_frame();
}
static bool read_touch(uint16_t& x,uint16_t& y){
  if(!touch)return false;
#if BOARD_IS_V1
  // The V1 FT-family controller NACKs i2c reads while idle (monitor mode); only
  // read when INT (GPIO 21, active low) reports a pending touch event.
  if(gpio_get_level(GPIO_NUM_21))return false;
#endif
  if(esp_lcd_touch_read_data(touch)!=ESP_OK)return false;
  esp_lcd_touch_point_data_t point={}; uint8_t points=0;
  if(esp_lcd_touch_get_data(touch,&point,&points,1)!=ESP_OK||points==0)return false;
  x=point.x; y=point.y; return true;
}
static esp_err_t http_evt(esp_http_client_event_t *e){ auto*v=(std::vector<char>*)e->user_data; if(e->event_id==HTTP_EVENT_ON_DATA){ const char* p=static_cast<const char*>(e->data); v->insert(v->end(),p,p+e->data_len); } return ESP_OK; }
static double num(cJSON*o,const char*k){ cJSON*x=cJSON_GetObjectItemCaseSensitive(o,k); return cJSON_IsNumber(x)?x->valuedouble:0; }
static bool fetch(){
  if(!wifi_up){ status="NO WIFI"; return false; } std::vector<char> body; std::string auth="Bearer "+std::string(FEED_TOKEN);
  esp_http_client_config_t cfg={}; cfg.url=FEED_URL; cfg.event_handler=http_evt; cfg.user_data=&body; cfg.timeout_ms=12000; cfg.crt_bundle_attach=esp_crt_bundle_attach;
  auto h=esp_http_client_init(&cfg); esp_http_client_set_header(h,"Authorization",auth.c_str()); esp_http_client_set_header(h,"X-Device-ID",DEVICE_ID); esp_err_t err=esp_http_client_perform(h); int code=esp_http_client_get_status_code(h); esp_http_client_cleanup(h);
  ESP_LOGI(TAG,"feed http=%d err=%s bytes=%u",code,esp_err_to_name(err),(unsigned)body.size());
  if(err!=ESP_OK||code!=200){ status="HTTP "+std::to_string(code); return false; } body.push_back(0); cJSON*d=cJSON_Parse(body.data()); if(!d){status="JSON ERROR";ESP_LOGE(TAG,"feed JSON parse failed");return false;}
  cJSON*ro=cJSON_GetObjectItem(d,"read_only"); if(!cJSON_IsTrue(ro)){ status="UNSAFE FEED"; cJSON_Delete(d); return false; }
  cJSON*prices=cJSON_GetObjectItem(d,"prices"),*price_history=cJSON_GetObjectItem(d,"price_history"),*positions=cJSON_GetObjectItem(d,"positions"),*portfolio=cJSON_GetObjectItem(d,"portfolio"); balance=num(portfolio,"balance"); total_pnl=num(portfolio,"unrealized_pnl"); realized_pnl_today=num(portfolio,"realized_pnl_today"); double refresh=num(d,"refresh_seconds"); if(refresh>=5&&refresh<=3600)feed_refresh_seconds=(uint32_t)refresh; double history_step=num(d,"price_history_seconds"); history_sample_seconds=history_step>=5&&history_step<=3600?(uint32_t)history_step:feed_refresh_seconds;
  for(auto&a:assets){ bool loaded=load_price_history(a,price_history); double price=num(prices,a.name); if(price>0&&std::isfinite(price)){a.price=price;if(!loaded)push_price(a,price);} a.pos=Position{}; cJSON*p=cJSON_GetObjectItem(positions,a.name); if(cJSON_IsObject(p)){ a.pos.open=true; cJSON*s=cJSON_GetObjectItem(p,"side"); if(cJSON_IsString(s))a.pos.side=s->valuestring; a.pos.size=num(p,"contracts"); if(a.pos.size==0) a.pos.size=num(p,"size"); a.pos.entry=num(p,"entry"); a.pos.pnl=num(p,"pnl"); } }
  closed_today.clear(); cJSON*closed=cJSON_GetObjectItem(d,"closed_today"); cJSON*item=nullptr; cJSON_ArrayForEach(item,closed){ ClosedPosition p; cJSON*s=cJSON_GetObjectItem(item,"symbol"); if(cJSON_IsString(s))p.symbol=s->valuestring; s=cJSON_GetObjectItem(item,"side"); if(cJSON_IsString(s))p.side=s->valuestring; p.size=num(item,"contracts"); p.pnl=num(item,"pnl"); closed_today.push_back(p); }
  cJSON*rd=cJSON_GetObjectItem(d,"realized_date"); if(cJSON_IsString(rd))realized_date=rd->valuestring; int open_count=0; for(auto&a:assets)if(a.pos.open)open_count++;
  ESP_LOGI(TAG,"parsed bal=%.2f open=%d upl=%.2f rpl_today=%.2f closed=%u date=%s",balance,open_count,total_pnl,realized_pnl_today,(unsigned)closed_today.size(),realized_date.c_str());
  cJSON_Delete(d); last_ok_ms=esp_timer_get_time()/1000; status="UPDATED"; return true;
}
static bool color_done(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*){ BaseType_t wake=pdFALSE; xSemaphoreGiveFromISR(tx_done,&wake); return wake==pdTRUE; }
static i2c_master_bus_handle_t ensure_i2c_bus(){
  if(i2c_bus)return i2c_bus;
  i2c_master_bus_config_t bc={.i2c_port=I2C_NUM_0,.sda_io_num=GPIO_NUM_15,.scl_io_num=GPIO_NUM_14,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags={.enable_internal_pullup=1}};
  ESP_ERROR_CHECK(i2c_new_master_bus(&bc,&i2c_bus));
  return i2c_bus;
}
// Per-variant panel power/reset. Both use TCA9554 outputs 0, 1, and 2 at 0x20,
// asserted before the first panel command; doing this after panel_init is too late.
// V1 additionally requires the AXP2101 rail setup at 0x34 (the proven ClawBuddy
// sequence); V2 must NEVER receive those PMU writes — they blank the CO5300 panel
// while every driver init still logs success.
static void panel_power_reset(){
  ensure_i2c_bus();
  ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(i2c_bus,ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000,&io_expander));
  constexpr uint32_t mask=IO_EXPANDER_PIN_NUM_0|IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2;
  ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander,mask,IO_EXPANDER_OUTPUT));
#if BOARD_IS_V1
  ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander,IO_EXPANDER_PIN_NUM_4,IO_EXPANDER_INPUT));
  ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander,mask,1));
  vTaskDelay(pdMS_TO_TICKS(100));
  ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander,mask,0));
  vTaskDelay(pdMS_TO_TICKS(300));
  ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander,mask,1));
  i2c_master_dev_handle_t axp;
  i2c_device_config_t axp_cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x34,.scl_speed_hz=400000};
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus,&axp_cfg,&axp));
  static const uint8_t axp_seq[][2]={
    {0x22,0b110},{0x27,0x10},                       // PWRON>OFFLEVEL poweroff source, 4s hold
    {0x80,0x01},{0x90,0x00},{0x91,0x00},            // only DC1 on, all LDOs off
    {0x82,(3300-1500)/100},{0x92,(3300-500)/100},   // DC1 3.3V, ALDO1 3.3V
    {0x90,0x01},                                    // enable ALDO1
    {0x64,0x02},{0x61,0x02},{0x62,0x08},{0x63,0x01} // charger 4.1V / 50mA pre / 200mA / 25mA term
  };
  for(auto&rv:axp_seq){ uint8_t buf[2]={rv[0],rv[1]}; ESP_ERROR_CHECK(i2c_master_transmit(axp,buf,2,100)); }
  ESP_ERROR_CHECK(i2c_master_bus_rm_device(axp));
  ESP_LOGI(TAG,"V1 TCA9554+AXP2101 power/reset complete");
#else
  ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander,mask,0));
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander,mask,1));
  vTaskDelay(pdMS_TO_TICKS(120));
  uint32_t levels=0;
  ESP_ERROR_CHECK(esp_io_expander_get_level(io_expander,mask,&levels));
  ESP_LOGI(TAG,"V2 TCA9554 power/reset complete: mask=0x%02lx levels=0x%02lx",(unsigned long)mask,(unsigned long)levels);
#endif
}
// Diagnostic: color bars via the app's own draw path, drawn straight into the DMA txbuf
// (no PSRAM involved), at the same point in bring-up where the official demo draws.
static void splash_bars(){
  static const uint16_t colors[8]={0xFFFF,0xFFE0,0x07FF,0x07E0,0xF81F,0xF800,0x001F,0x0000};
  for(int y=0;y<H;y+=TX_LINES){
    for(int r=0;r<TX_LINES;r++) for(int x=0;x<W;x++) txbuf[r*W+x]=__builtin_bswap16(colors[(x*8)/W]);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,y,W,y+TX_LINES,txbuf));
    if(xSemaphoreTake(tx_done,pdMS_TO_TICKS(2000))!=pdTRUE){ESP_LOGE(TAG,"splash timeout y=%d",y);return;}
  }
  ESP_LOGI(TAG,"SPLASH drawn OK");
}
// Diagnostic: read-only AXP2101 register dump so serial shows whether a rail changes state.
static void axp_dump(const char*phase){
  if(!i2c_bus)return;
  i2c_master_dev_handle_t dev; i2c_device_config_t c={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x34,.scl_speed_hz=400000};
  if(i2c_master_bus_add_device(i2c_bus,&c,&dev)!=ESP_OK){ESP_LOGW(TAG,"axp dump: add device failed");return;}
  static const uint8_t regs[]={0x00,0x01,0x80,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99};
  char line[120]; int off=0;
  for(uint8_t r:regs){ uint8_t v=0; if(i2c_master_transmit_receive(dev,&r,1,&v,1,100)==ESP_OK&&off<(int)sizeof(line)-8) off+=snprintf(line+off,sizeof(line)-off,"%02x=%02x ",r,v); }
  i2c_master_bus_rm_device(dev);
  ESP_LOGI(TAG,"AXP2101[%s] %s",phase,line);
}
// PHASE1: exact per-variant expander power/reset sequence, then the panel colorbar path.
// No NVS, PSRAM framebuffer, touch, or Wi-Fi yet. Static DMA stripe buffer only.
static void phase1_display_bars(){
  panel_power_reset();
  tx_done=xSemaphoreCreateBinary(); if(!tx_done) abort();
  spi_bus_config_t sb={}; sb.sclk_io_num=11;sb.data0_io_num=4;sb.data1_io_num=5;sb.data2_io_num=6;sb.data3_io_num=7;sb.max_transfer_sz=W*TX_LINES*2; ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&sb,SPI_DMA_CH_AUTO));
  esp_lcd_panel_io_spi_config_t io={};io.cs_gpio_num=12;io.dc_gpio_num=-1;io.spi_mode=0;io.pclk_hz=40*1000*1000;io.trans_queue_depth=10;io.on_color_trans_done=color_done;io.user_ctx=nullptr;io.lcd_cmd_bits=32;io.lcd_param_bits=8;io.flags.quad_mode=true; ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io,&lcd_io));
#if BOARD_IS_V1
  // Proven ClawBuddy/Xiaozhi SH8601 init for the V1 panel, with 0x51 raised to 75%
  // so the splash is visible immediately (the stock table left brightness at 0).
  static const sh8601_lcd_init_cmd_t cmds[]={{0x11,(uint8_t[]){0x00},0,120},{0x44,(uint8_t[]){0x01,0xD1},2,0},{0x35,(uint8_t[]){0x00},1,0},{0x53,(uint8_t[]){0x20},1,10},{0x2A,(uint8_t[]){0x00,0x00,0x01,0x6F},4,0},{0x2B,(uint8_t[]){0x00,0x00,0x01,0xBF},4,0},{0x51,(uint8_t[]){0xBF},1,10},{0x29,(uint8_t[]){0x00},0,10}}; sh8601_vendor_config_t vc={.init_cmds=cmds,.init_cmds_size=sizeof(cmds)/sizeof(cmds[0]),.flags={.use_qspi_interface=1}}; esp_lcd_panel_dev_config_t pc={};pc.reset_gpio_num=GPIO_NUM_NC;pc.flags.reset_active_high=1;pc.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;pc.bits_per_pixel=16;pc.vendor_config=&vc; ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(lcd_io,&pc,&panel)); ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));ESP_ERROR_CHECK(esp_lcd_panel_init(panel));ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel,false));ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel,PANEL_X_GAP,0));ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
#else
  static const co5300_lcd_init_cmd_t cmds[]={{0xFE,(uint8_t[]){0x00},1,0},{0xC4,(uint8_t[]){0x80},1,0},{0x3A,(uint8_t[]){0x55},1,0},{0x35,(uint8_t[]){0x00},1,0},{0x53,(uint8_t[]){0x20},1,0},{0x51,(uint8_t[]){0xFF},1,0},{0x63,(uint8_t[]){0xFF},1,0},{0x2A,(uint8_t[]){0x00,0x00,0x01,0x6F},4,0},{0x2B,(uint8_t[]){0x00,0x00,0x01,0xBF},4,0},{0x11,nullptr,0,100},{0x29,nullptr,0,0}}; co5300_vendor_config_t vc={.init_cmds=cmds,.init_cmds_size=sizeof(cmds)/sizeof(cmds[0]),.flags={.use_qspi_interface=1}}; esp_lcd_panel_dev_config_t pc={};pc.reset_gpio_num=GPIO_NUM_NC;pc.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;pc.bits_per_pixel=16;pc.vendor_config=&vc; ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(lcd_io,&pc,&panel)); ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));ESP_ERROR_CHECK(esp_lcd_panel_init(panel));ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel,PANEL_X_GAP,0));ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
#endif
  static DMA_ATTR uint16_t bars[W*TX_LINES];
  static const uint16_t colors[8]={0xFFFF,0xFFE0,0x07FF,0x07E0,0xF81F,0xF800,0x001F,0x0000};
  for(int y=0;y<H;y+=TX_LINES){
    for(int r=0;r<TX_LINES;r++) for(int x=0;x<W;x++) bars[r*W+x]=__builtin_bswap16(colors[(x*8)/W]);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,y,W,y+TX_LINES,bars));
    if(xSemaphoreTake(tx_done,pdMS_TO_TICKS(2000))!=pdTRUE){ESP_LOGE(TAG,"PHASE1 tx timeout y=%d",y);return;}
  }
  ESP_LOGI(TAG,"PHASE1: static color bars drawn (pure display path)");
}
static void init_rest(){
  i2c_master_bus_handle_t bus=ensure_i2c_bus();
#if !BOARD_IS_V1
  // Diagnostic bus scan, V2 only: the address-only probe transactions can wedge
  // the V1 FT-family touch controller before its driver ever talks to it.
  for(int a=1;a<0x7f;a++) if(i2c_master_probe(bus,a,30)==ESP_OK) ESP_LOGI(TAG,"i2c device 0x%02x",a);
#endif
  axp_dump("i2c-up"); ESP_LOGI(TAG,"PHASE3: i2c bus + PMU dump done"); vTaskDelay(pdMS_TO_TICKS(PHASE_DELAY_MS));
  fb=(uint16_t*)heap_caps_malloc(W*H*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); txbuf=(uint16_t*)heap_caps_malloc(W*TX_LINES*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL); if(!fb||!txbuf) abort();
  ESP_LOGI(TAG,"PHASE4: PSRAM fb allocated"); vTaskDelay(pdMS_TO_TICKS(PHASE_DELAY_MS));
  splash_bars(); ESP_LOGI(TAG,"PHASE5: bars redrawn via app path"); vTaskDelay(pdMS_TO_TICKS(BOARD_IS_V1?PHASE_DELAY_MS:3000));
#if BOARD_IS_V1
  { uint8_t bl=(uint8_t)((255*75)/100); ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io,(0x02UL<<24)|(0x51UL<<8),&bl,1)); }
#else
  ESP_ERROR_CHECK(esp_lcd_panel_co5300_set_brightness(panel,75));
#endif
  ESP_LOGI(TAG,"PHASE6: brightness 75 applied"); vTaskDelay(pdMS_TO_TICKS(PHASE_DELAY_MS));
  esp_lcd_panel_io_handle_t touch_io; esp_lcd_panel_io_i2c_config_t touch_io_cfg={};
#if BOARD_IS_V1
  touch_io_cfg.dev_addr=ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
#else
  touch_io_cfg.dev_addr=ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
#endif
  touch_io_cfg.control_phase_bytes=1;touch_io_cfg.lcd_cmd_bits=8;touch_io_cfg.flags.disable_control_phase=1;touch_io_cfg.scl_speed_hz=400000; ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus,&touch_io_cfg,&touch_io)); esp_lcd_touch_config_t touch_cfg={.x_max=W,.y_max=H,.rst_gpio_num=GPIO_NUM_NC,.int_gpio_num=GPIO_NUM_21,.levels={.reset=0,.interrupt=0},.flags={.swap_xy=0,.mirror_x=0,.mirror_y=0}};
#if BOARD_IS_V1
  // FT-family quirk handling: retry with an INT-pin wake pulse (holding INT low
  // briefly wakes the controller from hibernate). Touch failure must never abort
  // the boot — the display keeps working without it.
  {
    esp_err_t terr=esp_lcd_touch_new_i2c_ft5x06(touch_io,&touch_cfg,&touch);
    for(int attempt=1;terr!=ESP_OK&&attempt<=2;attempt++){
      ESP_LOGW(TAG,"FT5x06 init failed (%s), wake attempt %d",esp_err_to_name(terr),attempt);
      touch=nullptr;
      gpio_config_t gi={.pin_bit_mask=1ULL<<GPIO_NUM_21,.mode=GPIO_MODE_OUTPUT,.pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
      gpio_config(&gi); gpio_set_level(GPIO_NUM_21,0); vTaskDelay(pdMS_TO_TICKS(15)); gpio_set_level(GPIO_NUM_21,1); vTaskDelay(pdMS_TO_TICKS(120));
      gpio_reset_pin(GPIO_NUM_21);
      terr=esp_lcd_touch_new_i2c_ft5x06(touch_io,&touch_cfg,&touch);
    }
    if(terr!=ESP_OK){ touch=nullptr; ESP_LOGE(TAG,"touch unavailable (%s); continuing display-only",esp_err_to_name(terr)); }
  }
#else
  if(esp_lcd_touch_new_i2c_cst816s(touch_io,&touch_cfg,&touch)!=ESP_OK){ touch=nullptr; ESP_LOGE(TAG,"touch unavailable; continuing display-only"); }
#endif
  axp_dump("touch"); ESP_LOGI(TAG,"PHASE7: touch %s",touch?"ready":"absent"); vTaskDelay(pdMS_TO_TICKS(PHASE_DELAY_MS));
  gpio_config_t gb={.pin_bit_mask=1ULL<<GPIO_NUM_0,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE}; gpio_config(&gb);
}
extern "C" void app_main(){
  ESP_LOGI(TAG,"board variant: %s",BOARD_IS_V1?"V1 (SH8601/FT5x06/AXP2101)":"V2 (CO5300/CST820)");
  phase1_display_bars();
  ESP_LOGI(TAG,"PHASE1 hold %dms",BARS_HOLD_MS); vTaskDelay(pdMS_TO_TICKS(BARS_HOLD_MS));
  esp_err_t n=nvs_flash_init();if(n==ESP_ERR_NVS_NO_FREE_PAGES||n==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());n=nvs_flash_init();}ESP_ERROR_CHECK(n);
  ESP_LOGI(TAG,"PHASE2: nvs done"); vTaskDelay(pdMS_TO_TICKS(PHASE_DELAY_MS));
  init_rest();
  n=esp_ota_mark_app_valid_cancel_rollback();if(n!=ESP_OK&&n!=ESP_ERR_OTA_ROLLBACK_INVALID_STATE)ESP_LOGW(TAG,"unable to confirm OTA image: %s",esp_err_to_name(n));
  ESP_LOGI(TAG,"PHASE8: ota image confirmed"); draw(); ESP_LOGI(TAG,"PHASE9: first UI frame drawn");
  auto& network=NetworkPortal::GetInstance();
  network.Initialize([](bool connected){wifi_up=connected;if(connected)force_fetch=true;},[](){});
  vTaskDelay(pdMS_TO_TICKS(3000)); axp_dump("wifi+3s"); ESP_LOGI(TAG,"PHASE10: wifi/portal up");
  uint64_t last_fetch=0,last_draw=0,button_at=0;bool down=false,button_down=false,ota_hold_handled=false,shown_portal=false,shown_armed=false;
  while(true){
    uint64_t now=esp_timer_get_time()/1000;
    bool portal=network.IsPortalActive(),armed=network.IsOtaArmed();
    if(portal!=shown_portal||armed!=shown_armed){shown_portal=portal;shown_armed=armed;draw();last_draw=now;}
    if(!portal&&(force_fetch||now-last_fetch>=REFRESH_MS)){force_fetch=false;last_fetch=now;fetch();draw();last_draw=now;}
    bool pressed=false; uint16_t x=0,y=0; pressed=read_touch(x,y);
    if(!portal&&pressed&&!down){
      ESP_LOGI(TAG,"touch x=%u y=%u",x,y);
      if(y>=390){
        if(x<184){ if(selected_chart>=0)selected_chart=-1; else detail=!detail; }
        else force_fetch=true;
        draw();last_draw=now;
      } else if(selected_chart>=0&&y>=124&&y<385){
        selected_chart=(selected_chart+(x<W/2?4:1))%5;
        draw();last_draw=now;
      } else if(!detail&&y>=124&&y<382){
        int row=(y-124)/52;
        if(row>=0&&row<5&&(y-124)%52<50){selected_chart=row;draw();last_draw=now;}
      }
    }
    down=pressed;
    bool b=gpio_get_level(GPIO_NUM_0)==0;
    if(b&&!button_down){button_at=now;ota_hold_handled=false;}
    if(b&&!ota_hold_handled&&now-button_at>=10000){network.ArmOta();ota_hold_handled=true;draw();last_draw=now;}
    if(!b&&button_down&&!ota_hold_handled&&!portal){
      if(now-button_at>=800)force_fetch=true;
      else if(selected_chart>=0)selected_chart=-1;
      else detail=!detail;
      draw();last_draw=now;
    }
    button_down=b;
    if(now-last_draw>30000){draw();last_draw=now;}
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

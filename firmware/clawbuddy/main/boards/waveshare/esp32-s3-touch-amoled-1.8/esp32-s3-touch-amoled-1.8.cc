#include "wifi_board.h"
#include "display/lcd_display.h"
#include "esp_lcd_sh8601.h"

#include "codecs/es8311_audio_codec.h"
#include "application.h"
#include "button.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "config.h"
#include "power_save_timer.h"
#include "axp2101.h"
#include "i2c_device.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include "esp_io_expander_tca9554.h"
#include "settings.h"

#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define TAG "WaveshareEsp32s3TouchAMOLED1inch8"

LV_FONT_DECLARE(font_puhui_basic_30_4);

class Pmic : public Axp2101 {
public:
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        WriteReg(0x22, 0b110); // PWRON > OFFLEVEL as POWEROFF Source enable
        WriteReg(0x27, 0x10);  // hold 4s to power off

        // Disable All DCs but DC1
        WriteReg(0x80, 0x01);
        // Disable All LDOs
        WriteReg(0x90, 0x00);
        WriteReg(0x91, 0x00);

        // Set DC1 to 3.3V
        WriteReg(0x82, (3300 - 1500) / 100);

        // Set ALDO1 to 3.3V
        WriteReg(0x92, (3300 - 500) / 100);

        // Enable ALDO1(MIC)
        WriteReg(0x90, 0x01);
    
        WriteReg(0x64, 0x02); // CV charger voltage setting to 4.1V
        
        WriteReg(0x61, 0x02); // set Main battery precharge current to 50mA
        WriteReg(0x62, 0x08); // set Main battery charger current to 400mA ( 0x08-200mA, 0x09-300mA, 0x0A-400mA )
        WriteReg(0x63, 0x01); // set Main battery term charge current to 25mA
    }
};

#define LCD_OPCODE_WRITE_CMD (0x02ULL)
#define LCD_OPCODE_READ_CMD (0x03ULL)
#define LCD_OPCODE_WRITE_COLOR (0x32ULL)

static const sh8601_lcd_init_cmd_t vendor_specific_init[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10}
};

// 在waveshare_amoled_1_8类之前添加新的显示类
class CustomLcdDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* camera_overlay_ = nullptr;
    lv_obj_t* camera_image_ = nullptr;
    lv_obj_t* camera_status_label_ = nullptr;
    std::unique_ptr<LvglImage> camera_image_cached_ = nullptr;

    void EnsureCameraOverlayLocked() {
        if (camera_overlay_ != nullptr && lv_obj_is_valid(camera_overlay_)) {
            return;
        }

        lv_obj_t* screen = lv_screen_active();
        camera_overlay_ = lv_obj_create(screen);
        lv_obj_set_size(camera_overlay_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(camera_overlay_, 0, 0);
        lv_obj_set_style_bg_color(camera_overlay_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(camera_overlay_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(camera_overlay_, 0, 0);
        lv_obj_set_style_radius(camera_overlay_, 0, 0);
        lv_obj_set_style_pad_all(camera_overlay_, 0, 0);
        lv_obj_clear_flag(camera_overlay_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(camera_overlay_);

        camera_status_label_ = lv_label_create(camera_overlay_);
        lv_label_set_text(camera_status_label_, "Loading camera...");
        lv_obj_set_style_text_font(camera_status_label_, &font_puhui_basic_30_4, 0);
        lv_obj_set_style_text_color(camera_status_label_, lv_color_white(), 0);
        lv_obj_align(camera_status_label_, LV_ALIGN_CENTER, 0, 0);

        camera_image_ = lv_image_create(camera_overlay_);
        lv_obj_center(camera_image_);
        lv_obj_add_flag(camera_image_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* back_btn = lv_button_create(camera_overlay_);
        lv_obj_set_size(back_btn, 118, 56);
        lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1f2937), 0);
        lv_obj_set_style_bg_opa(back_btn, LV_OPA_90, 0);
        lv_obj_set_style_radius(back_btn, 12, 0);
        lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
            auto* self = static_cast<CustomLcdDisplay*>(lv_event_get_user_data(e));
            if (self != nullptr) {
                self->HideCameraView();
            }
        }, LV_EVENT_CLICKED, this);

        lv_obj_t* back_label = lv_label_create(back_btn);
        lv_label_set_text(back_label, "Back");
        lv_obj_set_style_text_font(back_label, &font_puhui_basic_30_4, 0);
        lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
        lv_obj_center(back_label);
    }

public:
    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                    esp_lcd_panel_handle_t panel_handle,
                    int width,
                    int height,
                    int offset_x,
                    int offset_y,
                    bool mirror_x,
                    bool mirror_y,
                    bool swap_xy)
        : SpiLcdDisplay(io_handle, panel_handle,
                    width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
        // Note: UI customization should be done in SetupUI(), not in constructor
        // to ensure lvgl objects are created before accessing them
    }

    virtual void SetupUI() override {
        // Call parent SetupUI() first to create all lvgl objects.
        // The compact myAI layout keeps status text inside top_bar_ and intentionally
        // does not create the old second-row status_bar_.
        SpiLcdDisplay::SetupUI();
    }

    void ShowCameraLoading() {
        DisplayLockGuard lock(this);
        EnsureCameraOverlayLocked();
        if (camera_status_label_ != nullptr && lv_obj_is_valid(camera_status_label_)) {
            lv_label_set_text(camera_status_label_, "Loading camera...");
            lv_obj_clear_flag(camera_status_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (camera_image_ != nullptr && lv_obj_is_valid(camera_image_)) {
            lv_obj_add_flag(camera_image_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_move_foreground(camera_overlay_);
    }

    void ShowCameraImage(std::unique_ptr<LvglImage> image) {
        if (image == nullptr) {
            ShowCameraLoading();
            return;
        }

        DisplayLockGuard lock(this);
        EnsureCameraOverlayLocked();
        camera_image_cached_ = std::move(image);
        auto img_dsc = camera_image_cached_->image_dsc();
        lv_image_set_src(camera_image_, img_dsc);

        lv_coord_t img_width = img_dsc->header.w;
        lv_coord_t img_height = img_dsc->header.h;
        if (img_width > 0 && img_height > 0) {
            lv_coord_t max_width = LV_HOR_RES;
            lv_coord_t max_height = LV_VER_RES;
            lv_coord_t zoom_w = (max_width * 256) / img_width;
            lv_coord_t zoom_h = (max_height * 256) / img_height;
            lv_coord_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
            if (zoom > 256) {
                zoom = 256;
            }
            if (zoom <= 0) {
                zoom = 64;
            }
            lv_image_set_scale(camera_image_, zoom);
        }

        lv_obj_center(camera_image_);
        lv_obj_clear_flag(camera_image_, LV_OBJ_FLAG_HIDDEN);
        if (camera_status_label_ != nullptr && lv_obj_is_valid(camera_status_label_)) {
            lv_obj_add_flag(camera_status_label_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_move_foreground(camera_overlay_);
    }

    void HideCameraView() {
        DisplayLockGuard lock(this);
        if (camera_overlay_ != nullptr && lv_obj_is_valid(camera_overlay_)) {
            lv_obj_del(camera_overlay_);
        }
        camera_overlay_ = nullptr;
        camera_image_ = nullptr;
        camera_status_label_ = nullptr;
        camera_image_cached_.reset();
    }
};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(esp_lcd_panel_io_handle_t panel_io) : Backlight(), panel_io_(panel_io) {}

protected:
    esp_lcd_panel_io_handle_t panel_io_;

    virtual void SetBrightnessImpl(uint8_t brightness) override {
        auto display = Board::GetInstance().GetDisplay();
        DisplayLockGuard lock(display);
        uint8_t data[1] = {((uint8_t)((255 * brightness) / 100))};
        int lcd_cmd = 0x51;
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
        esp_lcd_panel_io_tx_param(panel_io_, lcd_cmd, &data, sizeof(data));
    }
};

class WaveshareEsp32s3TouchAMOLED1inch8 : public WifiBoard {
private:
    static constexpr const char* kOpenClawVisionCaptureUrl = "http://openclaw-vision.local/capture";
    static constexpr const char* kOpenClawVisionCaptureMdnsUrl = "http://openclaw-vision.local/capture";
    static constexpr const char* kOpenClawVisionCaptureApUrl = "http://192.168.4.1/capture";
    static constexpr const char* kOpenClawVisionPreviewPngUrl = "http://myai-vision-proxy.local:8766/camera-preview.rgb565";
    static constexpr int kOpenClawVisionPreviewWidth = 80;
    static constexpr int kOpenClawVisionPreviewHeight = 60;
    static constexpr int kCameraSnapshotMaxBytes = 512 * 1024;

    i2c_master_bus_handle_t codec_i2c_bus_;
    Pmic* pmic_ = nullptr;
    Button boot_button_;
    CustomLcdDisplay* display_;
    CustomBacklight* backlight_;
    esp_io_expander_handle_t io_expander = NULL;
    PowerSaveTimer* power_save_timer_;
    uint32_t camera_live_generation_ = 0;
    bool push_to_talk_active_ = false;

    struct CameraLiveTaskArgs {
        WaveshareEsp32s3TouchAMOLED1inch8* board;
        std::string url;
        int duration_seconds;
        int refresh_ms;
        uint32_t generation;
    };

    struct CameraSnapshotTaskArgs {
        WaveshareEsp32s3TouchAMOLED1inch8* board;
        std::string url;
        uint32_t generation;
    };

    std::vector<std::string> CameraCaptureCandidates(const std::string& preferred_url) {
        std::vector<std::string> urls;
        auto add_url = [&urls](const std::string& candidate) {
            if (!candidate.empty() && std::find(urls.begin(), urls.end(), candidate) == urls.end()) {
                urls.push_back(candidate);
            }
        };
        // Prefer the Mac-side PNG proxy. Raw camera JPEG/PNG decoding/rendering has
        // been unstable on this ESP32-S3 AMOLED build; the proxy resizes and
        // converts the image before the device displays it.
        add_url(preferred_url);
        add_url(kOpenClawVisionPreviewPngUrl);
        return urls;
    }

    std::unique_ptr<LvglImage> DownloadCameraSnapshotUrl(const std::string& url, size_t* bytes_read = nullptr) {
        auto http = GetNetwork()->CreateHttp(3);
        http->SetTimeout(10000);
        http->SetHeader("User-Agent", "myAI-OpenClaw-Vision/1.1");
        if (!http->Open("GET", url)) {
            throw std::runtime_error("Failed to open camera URL: " + url);
        }

        int status_code = http->GetStatusCode();
        if (status_code != 200) {
            http->Close();
            throw std::runtime_error("Camera returned HTTP " + std::to_string(status_code));
        }

        std::string body = http->ReadAll();
        http->Close();
        if (body.empty()) {
            throw std::runtime_error("Camera snapshot was empty");
        }
        if (body.size() > kCameraSnapshotMaxBytes) {
            throw std::runtime_error("Camera snapshot too large: " + std::to_string(body.size()) + " bytes");
        }

        void* data = heap_caps_malloc(body.size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (data == nullptr) {
            data = heap_caps_malloc(body.size(), MALLOC_CAP_8BIT);
        }
        if (data == nullptr) {
            throw std::runtime_error("Failed to allocate memory for camera snapshot");
        }
        memcpy(data, body.data(), body.size());
        if (bytes_read != nullptr) {
            *bytes_read = body.size();
        }
        if (url.find(".rgb565") != std::string::npos) {
            if (body.size() != kOpenClawVisionPreviewWidth * kOpenClawVisionPreviewHeight * 2) {
                heap_caps_free(data);
                throw std::runtime_error("RGB565 preview size mismatch: " + std::to_string(body.size()));
            }
            return std::make_unique<LvglAllocatedImage>(
                data,
                body.size(),
                kOpenClawVisionPreviewWidth,
                kOpenClawVisionPreviewHeight,
                kOpenClawVisionPreviewWidth * 2,
                LV_COLOR_FORMAT_RGB565);
        }
        return std::make_unique<LvglAllocatedImage>(data, body.size());
    }

    std::unique_ptr<LvglImage> DownloadCameraSnapshot(const std::string& preferred_url, std::string* used_url = nullptr, size_t* bytes_read = nullptr) {
        std::string last_error;
        for (const auto& url : CameraCaptureCandidates(preferred_url)) {
            try {
                auto image = DownloadCameraSnapshotUrl(url, bytes_read);
                if (used_url != nullptr) {
                    *used_url = url;
                }
                return image;
            } catch (const std::exception& e) {
                last_error = url + ": " + e.what();
                ESP_LOGW(TAG, "OpenClaw Vision candidate failed: %s", last_error.c_str());
            }
        }
        throw std::runtime_error("All camera URLs failed; last error: " + last_error);
    }

    static void CameraSnapshotTask(void* arg) {
        std::unique_ptr<CameraSnapshotTaskArgs> args(static_cast<CameraSnapshotTaskArgs*>(arg));
        auto* board = args->board;

        try {
            size_t snapshot_bytes = 0;
            std::string used_url;
            auto image = board->DownloadCameraSnapshot(args->url, &used_url, &snapshot_bytes);
            if (board->camera_live_generation_ == args->generation) {
                board->display_->ShowCameraImage(std::move(image));
                ESP_LOGI(TAG, "OpenClaw Vision full-screen snapshot: %u bytes from %s", snapshot_bytes, used_url.c_str());
            }
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "OpenClaw Vision snapshot failed: %s", e.what());
            if (board->camera_live_generation_ == args->generation) {
                board->display_->ShowCameraLoading();
            }
        }

        vTaskDelete(nullptr);
    }

    cJSON* ShowCameraSnapshot(const std::string& url) {
        camera_live_generation_++;
        auto* args = new CameraSnapshotTaskArgs{this, url, camera_live_generation_};

        BaseType_t started = xTaskCreate(
            CameraSnapshotTask,
            "camera_snapshot",
            8192,
            args,
            3,
            nullptr);
        if (started != pdPASS) {
            delete args;
            throw std::runtime_error("Failed to start camera snapshot task");
        }

        display_->ShowCameraLoading();

        cJSON* json = cJSON_CreateObject();
        cJSON_AddBoolToObject(json, "success", true);
        cJSON_AddStringToObject(json, "mode", "snapshot");
        cJSON_AddStringToObject(json, "url", url.c_str());
        cJSON_AddStringToObject(json, "response", "Loading camera snapshot.");
        return json;
    }

    static void CameraLiveTask(void* arg) {
        std::unique_ptr<CameraLiveTaskArgs> args(static_cast<CameraLiveTaskArgs*>(arg));
        auto* board = args->board;
        int iterations = std::max(1, (args->duration_seconds * 1000) / args->refresh_ms);

        for (int i = 0; i < iterations && board->camera_live_generation_ == args->generation; ++i) {
            try {
                size_t snapshot_bytes = 0;
                std::string used_url;
                auto image = board->DownloadCameraSnapshot(args->url, &used_url, &snapshot_bytes);
                board->display_->ShowCameraImage(std::move(image));
                ESP_LOGI(TAG, "OpenClaw Vision full-screen live frame %d/%d: %u bytes from %s", i + 1, iterations, snapshot_bytes, used_url.c_str());
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "OpenClaw Vision live frame failed: %s", e.what());
                board->display_->ShowCameraLoading();
            }

            int remaining_ms = args->refresh_ms;
            while (remaining_ms > 0 && board->camera_live_generation_ == args->generation) {
                int sleep_ms = std::min(remaining_ms, 250);
                vTaskDelay(pdMS_TO_TICKS(sleep_ms));
                remaining_ms -= sleep_ms;
            }
        }

        if (board->camera_live_generation_ == args->generation) {
            board->display_->HideCameraView();
        }
        vTaskDelete(nullptr);
    }

    cJSON* StartCameraLiveView(const std::string& url, int duration_seconds, int refresh_ms) {
        duration_seconds = std::max(1, std::min(duration_seconds, 120));
        refresh_ms = std::max(500, std::min(refresh_ms, 10000));
        camera_live_generation_++;

        auto* args = new CameraLiveTaskArgs{this, url, duration_seconds, refresh_ms, camera_live_generation_};

        BaseType_t started = xTaskCreate(
            CameraLiveTask,
            "camera_live",
            8192,
            args,
            3,
            nullptr);
        if (started != pdPASS) {
            delete args;
            throw std::runtime_error("Failed to start camera live view task");
        }

        display_->ShowCameraLoading();
        cJSON* json = cJSON_CreateObject();
        cJSON_AddBoolToObject(json, "success", true);
        cJSON_AddStringToObject(json, "mode", "live_snapshot_refresh");
        cJSON_AddStringToObject(json, "url", url.c_str());
        cJSON_AddNumberToObject(json, "duration_seconds", duration_seconds);
        cJSON_AddNumberToObject(json, "refresh_ms", refresh_ms);
        cJSON_AddStringToObject(json, "response", "Showing live camera.");
        return json;
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(20);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            pmic_->PowerOff();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeTca9554(void) {
        esp_err_t ret = esp_io_expander_new_i2c_tca9554(codec_i2c_bus_, I2C_ADDRESS, &io_expander);
        if(ret != ESP_OK)
            ESP_LOGE(TAG, "TCA9554 create returned error");
        ret = esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 |IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT);
        ret |= esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_INPUT);
        ESP_ERROR_CHECK(ret);
        ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2, 1);
        ESP_ERROR_CHECK(ret);
        vTaskDelay(pdMS_TO_TICKS(100));
        ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2, 0);
        ESP_ERROR_CHECK(ret);
        vTaskDelay(pdMS_TO_TICKS(300));
        ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2, 1);
        ESP_ERROR_CHECK(ret);
    }

    void InitializeAxp2101() {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(codec_i2c_bus_, 0x34);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.sclk_io_num = GPIO_NUM_11;
        buscfg.data0_io_num = GPIO_NUM_4;
        buscfg.data1_io_num = GPIO_NUM_5;
        buscfg.data2_io_num = GPIO_NUM_6;
        buscfg.data3_io_num = GPIO_NUM_7;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        buscfg.flags = SPICOMMON_BUSFLAG_QUAD;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // Long hold = push-to-talk. Release = stop listening and send the turn.
        // This keeps normal short-press behavior intact while giving a private,
        // bounded input mode for noisy rooms.
        boot_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                return;
            }
            push_to_talk_active_ = true;
            if (display_ != nullptr) {
                display_->ShowNotification("Push to talk");
            }
            app.StartListening();
        });

        boot_button_.OnPressUp([this]() {
            if (!push_to_talk_active_) {
                return;
            }
            push_to_talk_active_ = false;
            Application::GetInstance().StopListening();
        });
    }

    void InitializeSH8601Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(
            EXAMPLE_PIN_NUM_LCD_CS,
            nullptr,
            nullptr
        );
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        const sh8601_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(sh8601_lcd_init_cmd_t),
            .flags ={
                .use_qspi_interface = 1,
            }
        };

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.flags.reset_active_high = 1,
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void *)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, false);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new CustomLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        backlight_ = new CustomBacklight(panel_io);
        backlight_->RestoreBrightness();
    }

    void InitializeTouch()
    {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_21,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
        tp_io_config.scl_speed_hz = 400 * 1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(codec_i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(), 
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }

    // 初始化工具
    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        // Camera image/live display is intentionally disabled for now.
        // The ESP32-S3 AMOLED freezes or drops Wi-Fi when it fetches/renders
        // camera frames during a voice session. Keep camera usage description-only
        // via the server-side self_camera_describe_scene tool.
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "End this conversation and enter WiFi configuration mode.\n"
            "**CAUTION** You must ask the user to confirm this action.",
            PropertyList(), [this](const PropertyList& properties) {
                EnterWifiConfigMode();
                return true;
            });
    }

public:
    WaveshareEsp32s3TouchAMOLED1inch8() :
        boot_button_(BOOT_BUTTON_GPIO, false, 650) {
        InitializePowerSaveTimer();
        InitializeCodecI2c();
        InitializeTca9554();
        InitializeAxp2101();
        InitializeSpi();
        InitializeSH8601Display();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        return backlight_;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(WaveshareEsp32s3TouchAMOLED1inch8);

#include "esp_camera.h"
#include <WiFi.h>

// OpenClaw ESP32 bridge vision camera for Heltec HT-HC33.
// Runs as a local Wi-Fi AP so the Mac can fetch http://192.168.4.1/capture.

#ifndef CAMERA_AP_SSID
#define CAMERA_AP_SSID "OpenClaw-Vision"
#endif
#ifndef CAMERA_AP_PASSWORD
#define CAMERA_AP_PASSWORD "openclaw-vision"
#endif

// HT-HC33 camera pins from Heltec variant.
#define PWDN_GPIO_NUM 20
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 47
#define SIOD_GPIO_NUM 45
#define SIOC_GPIO_NUM 42
#define Y2_GPIO_NUM 17
#define Y3_GPIO_NUM 13
#define Y4_GPIO_NUM 12
#define Y5_GPIO_NUM 14
#define Y6_GPIO_NUM 18
#define Y7_GPIO_NUM 46
#define Y8_GPIO_NUM 48
#define Y9_GPIO_NUM 38
#define VSYNC_GPIO_NUM 40
#define HREF_GPIO_NUM 39
#define PCLK_GPIO_NUM 21

void startCameraServer();
void setupLedFlash(int pin);

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  delay(500);
  Serial.println("OpenClaw Vision camera booting");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_SVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.jpeg_quality = 10;
  config.fb_count = psramFound() ? 2 : 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    delay(5000);
    ESP.restart();
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, 0);
    s->set_framesize(s, FRAMESIZE_VGA);
  }

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  bool ok = WiFi.softAP(CAMERA_AP_SSID, CAMERA_AP_PASSWORD, 6, 0, 2);
  if (!ok) {
    Serial.println("SoftAP start failed");
    delay(5000);
    ESP.restart();
  }

  Serial.print("AP SSID: ");
  Serial.println(CAMERA_AP_SSID);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Snapshot: http://192.168.4.1/capture");
  Serial.println("Stream:   http://192.168.4.1:81/stream");

  startCameraServer();
}

void loop() {
  delay(10000);
  Serial.printf("OpenClaw Vision alive: %lus clients=%d\n", millis() / 1000, WiFi.softAPgetStationNum());
}

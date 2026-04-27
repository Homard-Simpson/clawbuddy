#include "esp_camera.h"
#include <WiFi.h>
#include <HaLow.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// OpenClaw ESP32 bridge vision camera for Heltec HT-HC33.
// Runs as a setup AP + captive portal, and can also join home Wi-Fi.

#ifndef CAMERA_AP_SSID
#define CAMERA_AP_SSID "OpenClaw-Vision"
#endif
#ifndef CAMERA_AP_PASSWORD
#define CAMERA_AP_PASSWORD "openclaw-vision"
#endif
#ifndef CAMERA_HOSTNAME
#define CAMERA_HOSTNAME "openclaw-vision"
#endif

static const byte DNS_PORT = 53;
static const int MAX_SAVED_WIFI = 8;
static DNSServer dnsServer;
static Preferences preferences;
static String configuredSsid;
static String lastStaIp;
static bool staConnected = false;
static bool setupPortalActive = false;
static unsigned long staDisconnectedSince = 0;

static String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (char c : in) {
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += c;
    }
  }
  return out;
}

static String htmlEscapeMain(const String &in) {
  String out;
  out.reserve(in.length());
  for (char c : in) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

static String keyFor(const char *prefix, int i) {
  return String(prefix) + String(i);
}

String cameraSavedWifiJson() {
  preferences.begin("openclawcam", true);
  String json = "[";
  bool first = true;
  for (int i = 0; i < MAX_SAVED_WIFI; i++) {
    String ssid = preferences.getString(keyFor("ssid", i).c_str(), "");
    if (ssid.isEmpty()) continue;
    if (!first) json += ",";
    first = false;
    json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"active\":" + String(ssid == configuredSsid ? "true" : "false") + "}";
  }
  preferences.end();
  json += "]";
  return json;
}

String cameraSavedWifiHtml() {
  preferences.begin("openclawcam", true);
  String html;
  bool any = false;
  for (int i = 0; i < MAX_SAVED_WIFI; i++) {
    String ssid = preferences.getString(keyFor("ssid", i).c_str(), "");
    if (ssid.isEmpty()) continue;
    any = true;
    String safe = htmlEscapeMain(ssid);
    html += "<div class='saved'><strong>" + safe + "</strong>";
    if (ssid == configuredSsid && WiFi.status() == WL_CONNECTED) html += " <span class='ok'>connected</span>";
    html += "<form method='post' action='/wifi/connect'><input type='hidden' name='ssid' value='" + safe + "'><button type='submit'>Connect</button></form>";
    html += "<form method='post' action='/wifi/forget'><input type='hidden' name='ssid' value='" + safe + "'><button type='submit' class='danger'>Forget</button></form></div>";
  }
  preferences.end();
  if (!any) html = "<p class='muted'>No saved SSIDs yet.</p>";
  return html;
}

String cameraNetworkStatusJson() {
  IPAddress staIp = WiFi.localIP();
  IPAddress apIp = WiFi.softAPIP();
  String json = "{\"ok\":true,\"name\":\"OpenClaw Vision\",\"camera\":\"HT-HC33\"";
  json += ",\"hostname\":\"" CAMERA_HOSTNAME "\"";
  json += ",\"ap_ssid\":\"" CAMERA_AP_SSID "\"";
  json += ",\"setup_portal_active\":" + String(setupPortalActive ? "true" : "false");
  json += ",\"ap_ip\":\"" + (setupPortalActive ? apIp.toString() : String("")) + "\"";
  json += ",\"sta_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",\"sta_ssid\":\"" + jsonEscape(configuredSsid) + "\"";
  json += ",\"sta_ip\":\"" + (WiFi.status() == WL_CONNECTED ? staIp.toString() : String("")) + "\"";
  json += ",\"saved_ssids\":" + cameraSavedWifiJson();
  json += "}";
  return json;
}

void startSetupPortal() {
  if (setupPortalActive) return;
  Serial.println("Starting fallback setup portal...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  bool ok = WiFi.softAP(CAMERA_AP_SSID, CAMERA_AP_PASSWORD, 6, 0, 2);
  if (!ok) {
    Serial.println("SoftAP start failed");
    return;
  }
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  setupPortalActive = true;
  Serial.print("Setup AP SSID: ");
  Serial.println(CAMERA_AP_SSID);
  Serial.print("Setup AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void stopSetupPortal() {
  if (!setupPortalActive) return;
  Serial.println("Stopping setup portal; camera is on LAN now.");
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  setupPortalActive = false;
  WiFi.mode(WIFI_STA);
}

void saveCameraWifiCredentials(const String &ssid, const String &password) {
  preferences.begin("openclawcam", false);
  int target = -1;
  int firstEmpty = -1;
  for (int i = 0; i < MAX_SAVED_WIFI; i++) {
    String existing = preferences.getString(keyFor("ssid", i).c_str(), "");
    if (existing == ssid) target = i;
    if (existing.isEmpty() && firstEmpty < 0) firstEmpty = i;
  }
  if (target < 0) target = firstEmpty >= 0 ? firstEmpty : 0;
  preferences.putString(keyFor("ssid", target).c_str(), ssid);
  preferences.putString(keyFor("pass", target).c_str(), password);
  preferences.putString("last", ssid);
  preferences.end();
  configuredSsid = ssid;
}

void forgetCameraWifiCredentials(const String &ssid) {
  preferences.begin("openclawcam", false);
  for (int i = 0; i < MAX_SAVED_WIFI; i++) {
    String existing = preferences.getString(keyFor("ssid", i).c_str(), "");
    if (existing == ssid) {
      preferences.remove(keyFor("ssid", i).c_str());
      preferences.remove(keyFor("pass", i).c_str());
    }
  }
  String last = preferences.getString("last", "");
  if (last == ssid) preferences.remove("last");
  preferences.end();
  if (configuredSsid == ssid) configuredSsid = "";
}

void clearCameraWifiCredentials() {
  preferences.begin("openclawcam", false);
  preferences.clear();
  preferences.end();
  configuredSsid = "";
}

bool connectCameraWifi(const String &ssid, const String &password) {
  if (ssid.isEmpty()) return false;
  WiFi.disconnect(false, false);
  delay(200);
  configuredSsid = ssid;
  Serial.printf("Connecting STA to %s", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  staConnected = WiFi.status() == WL_CONNECTED;
  if (staConnected) {
    lastStaIp = WiFi.localIP().toString();
    staDisconnectedSince = 0;
    Serial.print("STA IP: ");
    Serial.println(lastStaIp);
    if (MDNS.begin(CAMERA_HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("mDNS: http://" CAMERA_HOSTNAME ".local/");
    } else {
      Serial.println("mDNS start failed");
    }
    stopSetupPortal();
  } else {
    if (!staDisconnectedSince) staDisconnectedSince = millis();
    Serial.println("Home Wi-Fi connect failed; setup portal will open after fallback delay.");
  }
  return staConnected;
}

bool connectCameraWifiBySsid(const String &ssid) {
  preferences.begin("openclawcam", true);
  String password;
  bool found = false;
  for (int i = 0; i < MAX_SAVED_WIFI; i++) {
    String existing = preferences.getString(keyFor("ssid", i).c_str(), "");
    if (existing == ssid) {
      password = preferences.getString(keyFor("pass", i).c_str(), "");
      found = true;
      break;
    }
  }
  preferences.end();
  return found ? connectCameraWifi(ssid, password) : false;
}

void connectConfiguredWifi() {
  preferences.begin("openclawcam", true);
  String last = preferences.getString("last", "");
  String ssids[MAX_SAVED_WIFI];
  String passwords[MAX_SAVED_WIFI];
  int count = 0;
  for (int i = 0; i < MAX_SAVED_WIFI; i++) {
    String ssid = preferences.getString(keyFor("ssid", i).c_str(), "");
    if (!ssid.isEmpty()) {
      ssids[count] = ssid;
      passwords[count] = preferences.getString(keyFor("pass", i).c_str(), "");
      count++;
    }
  }
  preferences.end();

  if (count == 0) {
    Serial.println("No home Wi-Fi saved yet; use captive portal on AP.");
    return;
  }

  Serial.println("Scanning for saved Wi-Fi networks...");
  int n = WiFi.scanNetworks(false, true);
  int bestSaved = -1;
  int bestRssi = -1000;
  for (int scan = 0; scan < n; scan++) {
    String seen = WiFi.SSID(scan);
    int rssi = WiFi.RSSI(scan);
    for (int saved = 0; saved < count; saved++) {
      if (seen == ssids[saved] && rssi > bestRssi) {
        bestSaved = saved;
        bestRssi = rssi;
      }
    }
  }

  if (bestSaved >= 0) {
    connectCameraWifi(ssids[bestSaved], passwords[bestSaved]);
    return;
  }

  if (!last.isEmpty()) {
    for (int i = 0; i < count; i++) {
      if (ssids[i] == last) {
        connectCameraWifi(ssids[i], passwords[i]);
        return;
      }
    }
  }
  connectCameraWifi(ssids[0], passwords[0]);
}

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

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  connectConfiguredWifi();

  if (WiFi.status() != WL_CONNECTED) {
    delay(10000);
    if (WiFi.status() != WL_CONNECTED) {
      startSetupPortal();
    }
  }

  Serial.println("Snapshot: http://192.168.4.1/capture");
  Serial.println("Stream:   http://192.168.4.1:81/stream");
  Serial.println("LAN URL:  http://" CAMERA_HOSTNAME ".local/ or http://<STA-IP>/");

  startCameraServer();
}

void loop() {
  if (setupPortalActive) {
    dnsServer.processNextRequest();
  }

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    lastCheck = millis();
    bool nowConnected = WiFi.status() == WL_CONNECTED;
    if (configuredSsid.length() && !nowConnected) {
      if (!staDisconnectedSince) staDisconnectedSince = millis();
      Serial.println("STA disconnected; reconnecting...");
      WiFi.reconnect();
      if (!setupPortalActive && millis() - staDisconnectedSince > 10000) {
        startSetupPortal();
      }
    }
    if (nowConnected) {
      lastStaIp = WiFi.localIP().toString();
      staDisconnectedSince = 0;
      if (setupPortalActive) stopSetupPortal();
    }
    Serial.printf("OpenClaw Vision alive: %lus setup=%s ap_clients=%d sta=%s ip=%s\n",
                  millis() / 1000,
                  setupPortalActive ? "on" : "off",
                  WiFi.softAPgetStationNum(),
                  nowConnected ? "connected" : "down",
                  nowConnected ? WiFi.localIP().toString().c_str() : "");
  }
  delay(10);
}

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include <WiFi.h>

String cameraNetworkStatusJson();
String cameraSavedWifiJson();
String cameraSavedWifiHtml();
void saveCameraWifiCredentials(const String &ssid, const String &password);
void forgetCameraWifiCredentials(const String &ssid);
void clearCameraWifiCredentials();
void connectConfiguredWifi();
bool connectCameraWifiBySsid(const String &ssid);

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char* _STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static httpd_handle_t camera_httpd = NULL;
static httpd_handle_t stream_httpd = NULL;

static String htmlEscape(const String &in) {
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

static String jsEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (char c : in) {
    if (c == '\\' || c == '\'') {
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

static String urlDecode(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < in.length()) {
      char h[3] = {in[i + 1], in[i + 2], 0};
      out += char(strtol(h, nullptr, 16));
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

static String formValue(const String &body, const String &key) {
  String needle = key + "=";
  int start = body.indexOf(needle);
  if (start < 0) return "";
  start += needle.length();
  int end = body.indexOf('&', start);
  if (end < 0) end = body.length();
  return urlDecode(body.substring(start, end));
}

static String currentBaseUrl() {
  if (WiFi.status() == WL_CONNECTED) {
    return String("http://") + WiFi.localIP().toString();
  }
  return "http://192.168.4.1";
}

static String scannedNetworksHtml() {
  String html = "<div class='networks'>";
  int n = WiFi.scanNetworks(false, true);
  if (n <= 0) {
    html += "<p class='muted'>No Wi-Fi networks found. Tap refresh or type SSID manually.</p>";
  } else {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.isEmpty()) ssid = "(hidden network)";
      html += "<button type='button' class='network' onclick=\"pickSsid('" + jsEscape(WiFi.SSID(i)) + "')\">";
      html += htmlEscape(ssid) + " <span>" + String(WiFi.RSSI(i)) + " dBm";
      html += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " · open" : " · locked";
      html += "</span></button>";
    }
  }
  html += "</div>";
  return html;
}

static String savedNetworksHtml() {
  String html = cameraSavedWifiHtml();
  html += "<form method='post' action='/wifi/clear'><button type='submit' class='danger'>Forget all saved Wi-Fi</button></form>";
  return html;
}

static esp_err_t index_handler(httpd_req_t *req) {
  String status = cameraNetworkStatusJson();
  String base = currentBaseUrl();
  String html =
    "<!doctype html><html><head><title>OpenClaw Vision</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:system-ui;background:#050816;color:#e5f7ef;margin:0;padding:1.25rem}a{color:#00d084}.card{border:1px solid #00d084;border-radius:16px;padding:1rem;max-width:760px;margin:auto;background:#0b1020}input,button{font:inherit;border-radius:10px;border:1px solid #334155;padding:.7rem;margin:.25rem 0;width:100%;box-sizing:border-box}button{background:#00d084;color:#03130c;font-weight:700}.network{background:#111827;color:#e5f7ef;text-align:left}.network span{float:right;color:#9ca3af}.saved{border:1px solid #334155;border-radius:12px;padding:.7rem;margin:.5rem 0}.saved form{display:inline-block;width:48%;margin-right:2%}.danger{background:#ef4444;color:white}.ok{color:#00d084}.muted{color:#9ca3af}code,pre{word-break:break-all;white-space:pre-wrap}</style>"
    "<script>function pickSsid(s){document.getElementById('ssid').value=s;document.getElementById('password').focus()}</script>"
    "</head><body><div class='card'><h1>OpenClaw Vision</h1>"
    "<p>HT-HC33 camera online.</p>"
    "<p><a href='/capture'>Snapshot</a> · <a href='" + base + ":81/stream'>Stream</a> · <a href='/status'>Status JSON</a></p>"
    "<p class='muted'>LAN name after setup: <code>http://openclaw-vision.local/</code><br>Current base: <code>" + htmlEscape(base) + "</code></p>"
    "<h2>Pick Wi-Fi</h2>"
    "<p class='muted'>Tap a network, enter password, save. Camera keeps this setup AP as fallback, but uses home Wi-Fi when connected.</p>" +
    scannedNetworksHtml() +
    "<form method='get' action='/'><button type='submit'>Refresh scan</button></form>"
    "<form method='post' action='/wifi'><label>SSID</label><input id='ssid' name='ssid' autocomplete='off' required><label>Password</label><input id='password' name='password' type='password' autocomplete='current-password'><button type='submit'>Save and connect</button></form>"
    "<h2>Saved SSIDs</h2>" + savedNetworksHtml() +
    "<h2>Preview</h2><img src='/capture' style='max-width:100%;border-radius:12px'>"
    "<h2>Status</h2><pre>" + htmlEscape(status) + "</pre>"
    "</div></body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html.c_str(), html.length());
}

static String readBody(httpd_req_t *req) {
  char buf[768] = {0};
  int total = req->content_len;
  int received = 0;
  while (received < total && received < (int)sizeof(buf) - 1) {
    int r = httpd_req_recv(req, buf + received, min(total - received, (int)sizeof(buf) - 1 - received));
    if (r <= 0) break;
    received += r;
  }
  return String(buf);
}

static esp_err_t wifi_save_handler(httpd_req_t *req) {
  String body = readBody(req);
  String ssid = formValue(body, "ssid");
  String password = formValue(body, "password");
  if (ssid.isEmpty()) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
    return ESP_FAIL;
  }
  saveCameraWifiCredentials(ssid, password);
  connectConfiguredWifi();
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/");
  return httpd_resp_send(req, "Saved. Reconnecting...", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_connect_handler(httpd_req_t *req) {
  String body = readBody(req);
  String ssid = formValue(body, "ssid");
  if (!ssid.isEmpty()) connectCameraWifiBySsid(ssid);
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/");
  return httpd_resp_send(req, "Connecting...", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_forget_handler(httpd_req_t *req) {
  String body = readBody(req);
  String ssid = formValue(body, "ssid");
  if (!ssid.isEmpty()) forgetCameraWifiCredentials(ssid);
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/");
  return httpd_resp_send(req, "Forgot.", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_clear_handler(httpd_req_t *req) {
  clearCameraWifiCredentials();
  WiFi.disconnect(false, true);
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/");
  return httpd_resp_send(req, "Cleared.", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  String json = cameraNetworkStatusJson();
  json.remove(json.length() - 1);
  json += ",\"sensor_pid\":" + String(s ? s->id.PID : -1) + "}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json.c_str(), json.length());
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=openclaw-vision.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  char part_buf[64];
  httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return ESP_FAIL;
    esp_err_t res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return ESP_OK;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.max_uri_handlers = 12;
  config.uri_match_fn = httpd_uri_match_wildcard;

  httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL};
  httpd_uri_t captive_uri = {.uri = "/*", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL};
  httpd_uri_t wifi_uri = {.uri = "/wifi", .method = HTTP_POST, .handler = wifi_save_handler, .user_ctx = NULL};
  httpd_uri_t wifi_connect_uri = {.uri = "/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_handler, .user_ctx = NULL};
  httpd_uri_t wifi_forget_uri = {.uri = "/wifi/forget", .method = HTTP_POST, .handler = wifi_forget_handler, .user_ctx = NULL};
  httpd_uri_t wifi_clear_uri = {.uri = "/wifi/clear", .method = HTTP_POST, .handler = wifi_clear_handler, .user_ctx = NULL};
  httpd_uri_t status_uri = {.uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL};
  httpd_uri_t capture_uri = {.uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL};

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &wifi_uri);
    httpd_register_uri_handler(camera_httpd, &wifi_connect_uri);
    httpd_register_uri_handler(camera_httpd, &wifi_forget_uri);
    httpd_register_uri_handler(camera_httpd, &wifi_clear_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &captive_uri);
  }

  config.server_port = 81;
  config.ctrl_port = 32769;
  httpd_uri_t stream_uri = {.uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL};
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

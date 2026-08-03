#include "network_portal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "feed_config.h"
#include "ota_policy.h"
#include "runtime_operation.h"

namespace {
constexpr char TAG[] = "coinbase-network";
constexpr int kPortalFallbackSeconds = 45;
constexpr int kOtaWindowSeconds = 300;
constexpr size_t kMaxCredentials = 10;
constexpr size_t kMaxUploadBytes = 7 * 1024 * 1024;

struct Credential { std::string ssid; std::string password; };
std::vector<Credential> credentials;
httpd_handle_t http_server = nullptr;
esp_netif_t* ap_netif = nullptr;
esp_timer_handle_t connection_timer = nullptr;
esp_timer_handle_t ota_timer = nullptr;
std::atomic<int> dns_socket{-1};
std::atomic<uint32_t> dns_generation{0};

struct DnsTaskContext {
    int socket_fd;
    uint32_t generation;
};

class MutexGuard {
public:
    explicit MutexGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
        if (mutex_) locked_ = xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
    }
    ~MutexGuard() { if (locked_) xSemaphoreGive(mutex_); }
    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

class RecursiveMutexGuard {
public:
    explicit RecursiveMutexGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
        if (mutex_) locked_ = xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
    }
    ~RecursiveMutexGuard() { if (locked_) xSemaphoreGiveRecursive(mutex_); }
    RecursiveMutexGuard(const RecursiveMutexGuard&) = delete;
    RecursiveMutexGuard& operator=(const RecursiveMutexGuard&) = delete;

private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

const char kPortalHtml[] = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Coinbase Display Setup</title><style>body{font:16px system-ui;background:#07101f;color:#f5f7fa;max-width:520px;margin:32px auto;padding:0 18px}section{background:#121826;padding:20px;border-radius:14px;margin:16px 0}input,button{box-sizing:border-box;width:100%;padding:12px;margin:7px 0;border-radius:8px;border:1px solid #526079}button{background:#377eff;color:white;font-weight:700}small{color:#9aa5b5}.status{white-space:pre-wrap}</style></head><body><h1>Coinbase Display</h1><section><h2>Wi-Fi setup</h2><form method="post" action="/save"><label>Network name</label><input name="ssid" maxlength="32" required><label>Password</label><input name="password" type="password" maxlength="64"><button>Save and restart</button></form></section><section><h2>Firmware update</h2><p>Updates are disabled unless physically armed on the display. Hold BOOT for 10 seconds, then enter the one-time code shown on screen.</p><input id="code" inputmode="numeric" maxlength="6" placeholder="One-time code"><input id="file" type="file" accept=".bin,application/octet-stream"><button type="button" onclick="upload()">Install firmware</button><div class="status" id="out"></div><small>The image must be a Coinbase AMOLED firmware image. The device changes boot slots only after full image validation.</small></section><script>async function upload(){const o=document.getElementById('out'),f=document.getElementById('file').files[0],c=document.getElementById('code').value;if(!f||c.length!==6){o.textContent='Choose a .bin and enter the 6-digit screen code.';return}o.textContent='Uploading… keep the device powered.';try{const r=await fetch('/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream','X-OTA-Code':c},body:f});o.textContent=await r.text()}catch(e){o.textContent='Connection closed. If validation finished, the device is restarting.'}}</script></body></html>)HTML";

bool IsApClient(httpd_req_t* req) {
    sockaddr_storage peer{};
    socklen_t len = sizeof(peer);
    int fd = httpd_req_to_sockfd(req);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &len) != 0) return false;
    const uint8_t* bytes = nullptr;
    if (peer.ss_family == AF_INET) {
        bytes = reinterpret_cast<const uint8_t*>(&reinterpret_cast<sockaddr_in*>(&peer)->sin_addr.s_addr);
    } else if (peer.ss_family == AF_INET6) {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&peer);
        const uint8_t* raw = v6->sin6_addr.s6_addr;
        if (raw[10] != 0xff || raw[11] != 0xff) return false;
        bytes = raw + 12;
    } else return false;
    ESP_LOGI(TAG, "portal request peer=%u.%u.%u.%u family=%d", bytes[0], bytes[1], bytes[2], bytes[3], peer.ss_family);
    return bytes[0] == 192 && bytes[1] == 168 && bytes[2] == 4;
}

std::string UrlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') out.push_back(' ');
        else if (value[i] == '%' && i + 2 < value.size()) {
            char hex[3] = {value[i + 1], value[i + 2], 0};
            char* end = nullptr;
            long decoded = strtol(hex, &end, 16);
            if (end && *end == 0) { out.push_back(static_cast<char>(decoded)); i += 2; }
            else out.push_back(value[i]);
        } else out.push_back(value[i]);
    }
    return out;
}

std::string FormValue(const std::string& body, const char* name) {
    std::string prefix = std::string(name) + "=";
    size_t start = body.find(prefix);
    if (start == std::string::npos || (start && body[start - 1] != '&')) return {};
    start += prefix.size();
    size_t end = body.find('&', start);
    return UrlDecode(body.substr(start, end == std::string::npos ? end : end - start));
}

void RestartTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

esp_err_t RootHandler(httpd_req_t* req) {
    if (!IsApClient(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "AP clients only");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kPortalHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t RedirectHandler(httpd_req_t* req) {
    if (!IsApClient(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "AP clients only");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t SaveHandler(httpd_req_t* req) {
    if (!IsApClient(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "AP clients only");
    if (req->content_len <= 0 || req->content_len > 256) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form");
    std::string body(req->content_len, '\0');
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (n <= 0) return ESP_FAIL;
        received += n;
    }
    std::string ssid = FormValue(body, "ssid");
    std::string password = FormValue(body, "password");
    if (ssid.empty() || ssid.size() > 32 || password.size() > 64) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID or password");
    NetworkPortal::GetInstance().SaveCredential(ssid, password);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Saved. The display is restarting and will join the new network.");
    xTaskCreate(RestartTask, "wifi_restart", 2048, nullptr, 5, nullptr);
    return ESP_OK;
}

esp_err_t OtaHandler(httpd_req_t* req) {
    auto& portal = NetworkPortal::GetInstance();
    if (!IsApClient(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "AP clients only");
    char code[16]{};
    if (httpd_req_get_hdr_value_str(req, "X-OTA-Code", code, sizeof(code)) != ESP_OK ||
        !portal.ValidateOtaCode(code))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Wrong one-time code");
    if (req->content_len < 1024 || req->content_len > kMaxUploadBytes)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware size");

    RuntimeOperationLease ota_operation(runtime_operation_gate(), RuntimeOperation::kManualOta);
    if (!ota_operation.Acquired()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Another firmware update or full standby is active; try again shortly");
    }

    const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);
    if (!update || static_cast<size_t>(req->content_len) > update->size)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No suitable OTA slot");

    std::vector<uint8_t> buffer(4096);
    constexpr size_t app_desc_offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    constexpr size_t required_header = app_desc_offset + sizeof(esp_app_desc_t);
    int first_len = 0;
    while (first_len < static_cast<int>(required_header) && first_len < req->content_len) {
        int n = httpd_req_recv(req, reinterpret_cast<char*>(buffer.data()) + first_len,
                               std::min<int>(buffer.size() - first_len,
                                             req->content_len - first_len));
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Truncated firmware header");
        first_len += n;
    }
    if (first_len < static_cast<int>(app_desc_offset + sizeof(esp_app_desc_t)))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Truncated firmware header");
    const auto* desc = reinterpret_cast<const esp_app_desc_t*>(buffer.data() + app_desc_offset);
    char project[sizeof(desc->project_name) + 1]{};
    char version[sizeof(desc->version) + 1]{};
    const char* required_suffix = BOARD_IS_V1 ? "-v1" : kOtaV2VersionSuffix;
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD ||
        !ota_copy_bounded_field(desc->project_name, sizeof(desc->project_name),
                                project, sizeof(project)) ||
        !ota_copy_bounded_field(desc->version, sizeof(desc->version),
                                version, sizeof(version)) ||
        !ota_string_equals(project, kOtaProject) ||
        !ota_has_suffix(version, required_suffix))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wrong Coinbase AMOLED board image");

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(update, req->content_len, &handle);
    if (err == ESP_OK) err = esp_ota_write(handle, buffer.data(), first_len);
    int total = first_len;
    while (err == ESP_OK && total < req->content_len) {
        int n = httpd_req_recv(req, reinterpret_cast<char*>(buffer.data()), std::min<int>(buffer.size(), req->content_len - total));
        if (n <= 0) { err = ESP_FAIL; break; }
        err = esp_ota_write(handle, buffer.data(), n);
        total += n;
    }
    if (err == ESP_OK) err = esp_ota_end(handle); else esp_ota_abort(handle);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed after %d bytes: %s", total, esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware validation or write failed; current firmware remains active");
    }
    ESP_LOGI(TAG, "OTA accepted: version=%s bytes=%d partition=%s", version, total, update->label);
    ota_operation.RetainUntilRestart();
    esp_err_t response = httpd_resp_sendstr(req, "Firmware validated. Restarting into the new slot.");
    if (xTaskCreate(RestartTask, "ota_restart", 2048, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "unable to create OTA restart task; restarting immediately");
        esp_restart();
    }
    return response;
}

void DnsTask(void* arg) {
    auto* context = static_cast<DnsTaskContext*>(arg);
    const int socket_fd = context->socket_fd;
    const uint32_t generation = context->generation;
    delete context;
    std::array<uint8_t, 512> packet{};
    while (dns_generation.load(std::memory_order_acquire) == generation) {
        sockaddr_in client{};
        socklen_t client_len = sizeof(client);
        int len = recvfrom(socket_fd, packet.data(), packet.size() - 16, 0,
                           reinterpret_cast<sockaddr*>(&client), &client_len);
        if (len < 0) {
            if (dns_generation.load(std::memory_order_acquire) == generation)
                ESP_LOGW(TAG, "DNS receive stopped errno=%d", errno);
            break;
        }
        if (len < 12 || dns_generation.load(std::memory_order_acquire) != generation) continue;
        packet[2] |= 0x80; packet[3] |= 0x80; packet[6] = packet[7] = 0; packet[7] = 1;
        const uint8_t answer[] = {0xc0,0x0c,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x1c,0x00,0x04,192,168,4,1};
        memcpy(packet.data() + len, answer, sizeof(answer));
        sendto(socket_fd, packet.data(), len + sizeof(answer), 0,
               reinterpret_cast<sockaddr*>(&client), client_len);
    }
    int expected = socket_fd;
    if (dns_generation.load(std::memory_order_acquire) == generation &&
        dns_socket.compare_exchange_strong(expected, -1, std::memory_order_acq_rel)) {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }
    vTaskDelete(nullptr);
}
}

NetworkPortal& NetworkPortal::GetInstance() {
    static NetworkPortal instance;
    return instance;
}

bool NetworkPortal::IsConnected() const {
    MutexGuard lock(state_mutex_);
    return connected_;
}

bool NetworkPortal::IsPortalActive() const {
    MutexGuard lock(state_mutex_);
    return portal_active_;
}

bool NetworkPortal::IsOtaArmed() const {
    const int64_t now = esp_timer_get_time();
    MutexGuard lock(state_mutex_);
    return ota_armed_ && ota_deadline_us_ > now;
}

std::string NetworkPortal::GetApSsid() const {
    MutexGuard lock(state_mutex_);
    return ap_ssid_;
}

std::string NetworkPortal::GetOtaCode() const {
    const int64_t now = esp_timer_get_time();
    MutexGuard lock(state_mutex_);
    return ota_armed_ && ota_deadline_us_ > now ? ota_code_ : std::string{};
}

bool NetworkPortal::ValidateOtaCode(const char* code) const {
    if (!code) return false;
    const int64_t now = esp_timer_get_time();
    MutexGuard lock(state_mutex_);
    return portal_active_ && ota_armed_ && ota_deadline_us_ > now && ota_code_ == code;
}

bool NetworkPortal::HasCredentials() const {
    MutexGuard lock(state_mutex_);
    return !credentials.empty();
}

void NetworkPortal::NotifyState() {
    if (state_callback_) state_callback_();
}

void NetworkPortal::RestartConnectionTimer() {
    RecursiveMutexGuard lifecycle(lifecycle_mutex_);
    {
        MutexGuard state(state_mutex_);
        if (!connection_timer || credentials.empty() || suspended_ || connected_ || portal_active_) return;
    }
    esp_timer_stop(connection_timer);
    esp_err_t err = esp_timer_start_once(connection_timer, kPortalFallbackSeconds * 1000000ULL);
    if (err != ESP_OK) ESP_LOGW(TAG, "unable to arm Wi-Fi portal fallback timer: %s", esp_err_to_name(err));
}

void NetworkPortal::LoadCredentials() {
    std::vector<Credential> loaded;
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) return;
    for (size_t i = 0; i < kMaxCredentials; ++i) {
        std::string suffix = i ? std::to_string(i) : "";
        char ssid[33]{}, password[65]{};
        size_t ssid_len = sizeof(ssid), password_len = sizeof(password);
        if (nvs_get_str(nvs, ("ssid" + suffix).c_str(), ssid, &ssid_len) == ESP_OK && ssid[0] &&
            nvs_get_str(nvs, ("password" + suffix).c_str(), password, &password_len) == ESP_OK)
            loaded.push_back({ssid, password});
    }
    nvs_close(nvs);
    MutexGuard state(state_mutex_);
    credentials = std::move(loaded);
}

void NetworkPortal::SaveCredential(const std::string& ssid, const std::string& password) {
    std::vector<Credential> saved;
    {
        MutexGuard state(state_mutex_);
        credentials.erase(std::remove_if(credentials.begin(), credentials.end(), [&](const Credential& c) { return c.ssid == ssid; }), credentials.end());
        credentials.insert(credentials.begin(), {ssid, password});
        if (credentials.size() > kMaxCredentials) credentials.resize(kMaxCredentials);
        saved = credentials;
    }
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open("wifi", NVS_READWRITE, &nvs));
    for (size_t i = 0; i < kMaxCredentials; ++i) {
        std::string suffix = i ? std::to_string(i) : "";
        std::string ssid_key = "ssid" + suffix, password_key = "password" + suffix;
        if (i < saved.size()) {
            ESP_ERROR_CHECK(nvs_set_str(nvs, ssid_key.c_str(), saved[i].ssid.c_str()));
            ESP_ERROR_CHECK(nvs_set_str(nvs, password_key.c_str(), saved[i].password.c_str()));
        } else {
            nvs_erase_key(nvs, ssid_key.c_str()); nvs_erase_key(nvs, password_key.c_str());
        }
    }
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
}

void NetworkPortal::ApplyCredential(size_t index) {
    Credential selected;
    {
        MutexGuard state(state_mutex_);
        if (credentials.empty()) return;
        credential_index_ = index % credentials.size();
        selected = credentials[credential_index_];
    }
    wifi_config_t config{};
    strlcpy(reinterpret_cast<char*>(config.sta.ssid), selected.ssid.c_str(), sizeof(config.sta.ssid));
    strlcpy(reinterpret_cast<char*>(config.sta.password), selected.password.c_str(), sizeof(config.sta.password));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
}

void NetworkPortal::Initialize(std::function<void(bool)> connection_callback,
                               std::function<void()> state_callback) {
    state_mutex_ = xSemaphoreCreateMutex();
    lifecycle_mutex_ = xSemaphoreCreateRecursiveMutex();
    if (!state_mutex_ || !lifecycle_mutex_) {
        ESP_LOGE(TAG, "unable to allocate network portal mutexes");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    connection_callback_ = std::move(connection_callback);
    state_callback_ = std::move(state_callback);
    LoadCredentials();
    const bool has_credentials = HasCredentials();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, EventHandler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, EventHandler, this));
    ESP_ERROR_CHECK(esp_wifi_set_mode(has_credentials ? WIFI_MODE_STA : WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (!has_credentials) {
        StartPortal();
    } else {
        esp_timer_create_args_t args{};
        args.callback = ConnectionTimeout; args.arg = this; args.name = "wifi_portal_fallback";
        ESP_ERROR_CHECK(esp_timer_create(&args, &connection_timer));
        RestartConnectionTimer();
    }
    size_t saved_networks = 0;
    {
        MutexGuard state(state_mutex_);
        saved_networks = credentials.size();
    }
    ESP_LOGI(TAG, "network initialized saved_networks=%u", static_cast<unsigned>(saved_networks));
}

void NetworkPortal::EventHandler(void* arg, const char* event_base, int32_t event_id, void* event_data) {
    (void)event_data;
    auto* self = static_cast<NetworkPortal*>(arg);
    RecursiveMutexGuard lifecycle(self->lifecycle_mutex_);
    bool suspended = false;
    {
        MutexGuard state(self->state_mutex_);
        suspended = self->suspended_;
    }
    if (suspended) {
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            bool was_connected = false;
            {
                MutexGuard state(self->state_mutex_);
                was_connected = self->connected_;
                self->connected_ = false;
            }
            if (was_connected && self->connection_callback_) self->connection_callback_(false);
        }
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START && self->HasCredentials()) {
        self->ApplyCredential(0);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        bool ota_armed = false;
        {
            MutexGuard state(self->state_mutex_);
            self->connected_ = true;
            ota_armed = self->ota_armed_ && self->ota_deadline_us_ > esp_timer_get_time();
        }
        if (connection_timer) esp_timer_stop(connection_timer);
        if (!ota_armed) self->StopPortal();
        if (self->connection_callback_) self->connection_callback_(true);
        self->NotifyState();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED && self->HasCredentials()) {
        bool was_connected = false;
        size_t next_credential = 0;
        {
            MutexGuard state(self->state_mutex_);
            was_connected = self->connected_;
            self->connected_ = false;
            next_credential = self->credential_index_ + 1;
        }
        self->ApplyCredential(next_credential);
        esp_wifi_connect();
        if (was_connected) self->RestartConnectionTimer();
        if (was_connected && self->connection_callback_) self->connection_callback_(false);
        self->NotifyState();
    }
}

void NetworkPortal::ConnectionTimeout(void* arg) {
    static_cast<NetworkPortal*>(arg)->StartPortal();
}

void NetworkPortal::OtaTimeout(void* arg) {
    auto* self = static_cast<NetworkPortal*>(arg);
    RecursiveMutexGuard lifecycle(self->lifecycle_mutex_);
    bool changed = false;
    {
        MutexGuard state(self->state_mutex_);
        const int64_t now = esp_timer_get_time();
        // A callback already dispatched from an older window must not clear a
        // freshly rearmed code whose deadline is still in the future.
        if (self->ota_armed_ && self->ota_deadline_us_ <= now) {
            self->ota_armed_ = false;
            self->ota_deadline_us_ = 0;
            self->ota_code_.clear();
            changed = true;
        }
    }
    if (changed) self->NotifyState();
}

void NetworkPortal::StartPortal(bool allow_when_connected) {
    RecursiveMutexGuard lifecycle(lifecycle_mutex_);
    {
        MutexGuard state(state_mutex_);
        if (portal_active_ || suspended_ || (connected_ && !allow_when_connected)) return;
    }
    uint8_t mac[6]{};
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "ClawBuddy-Coinbase-%02X%02X", mac[4], mac[5]);

    if (!ap_netif) ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_ip_info_t ip{};
    IP4_ADDR(&ip.ip, 192, 168, 4, 1); IP4_ADDR(&ip.gw, 192, 168, 4, 1); IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap_netif); ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip)); ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    wifi_config_t config{};
    strlcpy(reinterpret_cast<char*>(config.ap.ssid), ssid, sizeof(config.ap.ssid));
    config.ap.ssid_len = strlen(ssid); config.ap.max_connection = 4; config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 12; hc.uri_match_fn = httpd_uri_match_wildcard; hc.recv_wait_timeout = 20; hc.send_wait_timeout = 20;
    ESP_ERROR_CHECK(httpd_start(&http_server, &hc));
    httpd_uri_t root{}; root.uri = "/"; root.method = HTTP_GET; root.handler = RootHandler; ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &root));
    httpd_uri_t save{}; save.uri = "/save"; save.method = HTTP_POST; save.handler = SaveHandler; ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &save));
    httpd_uri_t ota{}; ota.uri = "/ota"; ota.method = HTTP_POST; ota.handler = OtaHandler; ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &ota));
    httpd_uri_t wildcard{}; wildcard.uri = "/*"; wildcard.method = HTTP_GET; wildcard.handler = RedirectHandler; ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &wildcard));

    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(53);
    if (socket_fd < 0 || bind(socket_fd, reinterpret_cast<sockaddr*>(&server), sizeof(server)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed errno=%d", errno);
        if (socket_fd >= 0) close(socket_fd);
    } else {
        const uint32_t generation = dns_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        dns_socket.store(socket_fd, std::memory_order_release);
        auto* context = new (std::nothrow) DnsTaskContext{socket_fd, generation};
        if (!context || xTaskCreate(DnsTask, "captive_dns", 4096, context, 5, nullptr) != pdPASS) {
            ESP_LOGE(TAG, "unable to create captive DNS task");
            delete context;
            dns_generation.fetch_add(1, std::memory_order_acq_rel);
            dns_socket.store(-1, std::memory_order_release);
            close(socket_fd);
        }
    }
    {
        MutexGuard state(state_mutex_);
        ap_ssid_ = ssid;
        portal_active_ = true;
    }
    NotifyState();
    ESP_LOGI(TAG, "captive portal started ssid=%s url=http://192.168.4.1", ssid);
}

void NetworkPortal::StopPortal() {
    RecursiveMutexGuard lifecycle(lifecycle_mutex_);
    {
        MutexGuard state(state_mutex_);
        if (!portal_active_) return;
        portal_active_ = false;
        ota_armed_ = false;
        ota_deadline_us_ = 0;
        ota_code_.clear();
    }
    if (ota_timer) esp_timer_stop(ota_timer);
    if (http_server) { httpd_stop(http_server); http_server = nullptr; }
    dns_generation.fetch_add(1, std::memory_order_acq_rel);
    int socket_fd = dns_socket.exchange(-1, std::memory_order_acq_rel);
    if (socket_fd >= 0) { shutdown(socket_fd, SHUT_RDWR); close(socket_fd); }
    esp_wifi_set_mode(WIFI_MODE_STA);
    NotifyState();
}

void NetworkPortal::ArmOta() {
    RecursiveMutexGuard lifecycle(lifecycle_mutex_);
    RuntimeOperation active_operation = runtime_operation_gate().Current();
    if (active_operation != RuntimeOperation::kNone) {
        ESP_LOGW(TAG, "physical OTA arm ignored: %s active",
                 runtime_operation_name(active_operation));
        return;
    }
    StartPortal(true);
    if (!IsPortalActive()) return;
    char code[7];
    snprintf(code, sizeof(code), "%06lu", static_cast<unsigned long>(esp_random() % 1000000));
    if (!ota_timer) {
        esp_timer_create_args_t args{};
        args.callback = OtaTimeout; args.arg = this; args.name = "ota_window";
        ESP_ERROR_CHECK(esp_timer_create(&args, &ota_timer));
    } else esp_timer_stop(ota_timer);
    {
        MutexGuard state(state_mutex_);
        ota_code_ = code;
        ota_armed_ = true;
        ota_deadline_us_ = esp_timer_get_time() + kOtaWindowSeconds * 1000000LL;
    }
    ESP_ERROR_CHECK(esp_timer_start_once(ota_timer, kOtaWindowSeconds * 1000000ULL));
    NotifyState();
    ESP_LOGW(TAG, "physical OTA window armed for %d seconds", kOtaWindowSeconds);
}

void NetworkPortal::Suspend() {
    {
        RecursiveMutexGuard lifecycle(lifecycle_mutex_);
        bool stop_portal = false;
        {
            MutexGuard state(state_mutex_);
            if (suspended_) return;
            suspended_ = true;
            resume_portal_ = portal_active_;
            stop_portal = portal_active_;
            connected_ = false;
        }
        if (connection_timer) esp_timer_stop(connection_timer);
        if (stop_portal) StopPortal();
    }
    esp_err_t err = esp_wifi_stop();
    if (connection_callback_) connection_callback_(false);
    NotifyState();
    ESP_LOGI(TAG, "Wi-Fi stopped for screen-off standby (%s)", esp_err_to_name(err));
}

void NetworkPortal::Resume() {
    bool restore_portal = false;
    {
        RecursiveMutexGuard lifecycle(lifecycle_mutex_);
        MutexGuard state(state_mutex_);
        if (!suspended_) return;
        restore_portal = resume_portal_ || credentials.empty();
        resume_portal_ = false;
        // Keep suspended_ true while the driver starts so a queued old disconnect
        // cannot race a connect against a stopped Wi-Fi interface.
    }
    esp_err_t err = esp_wifi_start();
    {
        RecursiveMutexGuard lifecycle(lifecycle_mutex_);
        if (err == ESP_OK) {
            {
                MutexGuard state(state_mutex_);
                suspended_ = false;
            }
            // Connect explicitly in case WIFI_EVENT_STA_START was delivered while
            // suspended_ was still true. A later duplicate connect is harmless.
            if (HasCredentials()) { ApplyCredential(0); esp_wifi_connect(); }
            if (restore_portal) StartPortal();
            else RestartConnectionTimer();
        } else {
            MutexGuard state(state_mutex_);
            resume_portal_ = restore_portal;
        }
    }
    ESP_LOGI(TAG, "Wi-Fi resumed after standby (%s)", esp_err_to_name(err));
}

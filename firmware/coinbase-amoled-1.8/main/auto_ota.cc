#include "auto_ota.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "feed_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "network_portal.h"
#include "ota_policy.h"
#include "runtime_operation.h"

namespace {
constexpr char TAG[] = "coinbase-auto-ota";
constexpr std::size_t kMaxManifestBytes = 4096;
constexpr TickType_t kStartupDelay = pdMS_TO_TICKS(30000);
constexpr TickType_t kOfflineRetry = pdMS_TO_TICKS(10000);
constexpr TickType_t kFailureRetry = pdMS_TO_TICKS(15 * 60 * 1000);
constexpr TickType_t kNormalInterval = pdMS_TO_TICKS(6 * 60 * 60 * 1000);

struct Manifest {
  std::string project;
  std::string board;
  std::string version;
  std::string url;
  std::string sha256;
  std::size_t size = 0;

  OtaManifestFields Fields() const {
    return {project.c_str(), board.c_str(), version.c_str(), url.c_str(),
            sha256.c_str(), size};
  }
};

struct BodyCollector {
  std::vector<char> bytes;
  bool overflow = false;
};

esp_err_t CollectBody(esp_http_client_event_t* event) {
  auto* body = static_cast<BodyCollector*>(event->user_data);
  if (event->event_id != HTTP_EVENT_ON_DATA || !body) return ESP_OK;
  if (event->data_len < 0 || body->bytes.size() + event->data_len > kMaxManifestBytes) {
    body->overflow = true;
    return ESP_FAIL;
  }
  const char* data = static_cast<const char*>(event->data);
  body->bytes.insert(body->bytes.end(), data, data + event->data_len);
  return ESP_OK;
}

void SetAuthenticatedHeaders(esp_http_client_handle_t client,
                             const char* current_version) {
  const std::string authorization = "Bearer " + std::string(FEED_TOKEN);
  esp_http_client_set_header(client, "Authorization", authorization.c_str());
  esp_http_client_set_header(client, "X-Device-ID", DEVICE_ID);
  esp_http_client_set_header(client, "X-Firmware-Project", kOtaProject);
  esp_http_client_set_header(client, "X-Firmware-Board", kOtaV2Board);
  esp_http_client_set_header(client, "X-Firmware-Version", current_version);
  esp_http_client_set_header(client, "Accept", "application/json");
}

bool JsonString(cJSON* root, const char* key, std::string& out) {
  cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsString(item) || !item->valuestring) return false;
  out = item->valuestring;
  return true;
}

bool FetchManifest(const char* current_version, Manifest& manifest) {
  BodyCollector body;
  esp_http_client_config_t config{};
  config.url = OTA_MANIFEST_URL;
  config.event_handler = CollectBody;
  config.user_data = &body;
  config.timeout_ms = 12000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.disable_auto_redirect = true;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;
  SetAuthenticatedHeaders(client, current_version);
  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (err != ESP_OK || status != 200 || body.overflow || body.bytes.empty()) {
    ESP_LOGW(TAG, "manifest check failed: http=%d err=%s bytes=%u", status,
             esp_err_to_name(err), static_cast<unsigned>(body.bytes.size()));
    return false;
  }
  body.bytes.push_back('\0');
  cJSON* root = cJSON_Parse(body.bytes.data());
  if (!cJSON_IsObject(root)) {
    if (root) cJSON_Delete(root);
    ESP_LOGW(TAG, "manifest check failed: invalid JSON");
    return false;
  }
  cJSON* size = cJSON_GetObjectItemCaseSensitive(root, "size");
  const bool valid = JsonString(root, "project", manifest.project) &&
                     JsonString(root, "board", manifest.board) &&
                     JsonString(root, "version", manifest.version) &&
                     JsonString(root, "url", manifest.url) &&
                     JsonString(root, "sha256", manifest.sha256) &&
                     cJSON_IsNumber(size) && size->valuedouble >= 0 &&
                     size->valuedouble <= static_cast<double>(SIZE_MAX) &&
                     size->valuedouble == static_cast<double>(
                         static_cast<std::size_t>(size->valuedouble));
  if (valid) manifest.size = static_cast<std::size_t>(size->valuedouble);
  cJSON_Delete(root);
  if (!valid) ESP_LOGW(TAG, "manifest check failed: missing or invalid fields");
  return valid;
}

bool DescriptorFieldEquals(const char* field, std::size_t capacity,
                           const char* expected) {
  if (!field || !expected) return false;
  const std::size_t actual_len = strnlen(field, capacity);
  const std::size_t expected_len = std::strlen(expected);
  return actual_len < capacity && actual_len == expected_len &&
         std::memcmp(field, expected, expected_len) == 0;
}

bool HexToDigest(const std::string& hex, uint8_t digest[32]) {
  if (!ota_valid_sha256_hex(hex.c_str())) return false;
  auto nibble = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return static_cast<uint8_t>(c - 'a' + 10);
  };
  for (int i = 0; i < 32; ++i)
    digest[i] = static_cast<uint8_t>((nibble(hex[i * 2]) << 4) |
                                     nibble(hex[i * 2 + 1]));
  return true;
}

bool DownloadAndInstall(const Manifest& manifest, const char* current_version,
                        const esp_partition_t* update) {
  esp_http_client_config_t config{};
  config.url = manifest.url.c_str();
  config.timeout_ms = 20000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.disable_auto_redirect = true;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;
  SetAuthenticatedHeaders(client, current_version);
  esp_http_client_set_header(client, "Accept", "application/octet-stream");
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "image connection failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }
  const int64_t content_length = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status != 200 || content_length < 0 ||
      static_cast<uint64_t>(content_length) != manifest.size) {
    ESP_LOGE(TAG, "image headers rejected: http=%d length=%lld expected=%u",
             status, static_cast<long long>(content_length),
             static_cast<unsigned>(manifest.size));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  std::vector<uint8_t> buffer(4096);
  constexpr std::size_t descriptor_offset =
      sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
  constexpr std::size_t required_header = descriptor_offset + sizeof(esp_app_desc_t);
  std::size_t first_size = 0;
  while (first_size < required_header) {
    const int n = esp_http_client_read(
        client, reinterpret_cast<char*>(buffer.data() + first_size),
        static_cast<int>(buffer.size() - first_size));
    if (n <= 0) {
      ESP_LOGE(TAG, "image header read failed");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    first_size += static_cast<std::size_t>(n);
  }
  const auto* image = reinterpret_cast<const esp_image_header_t*>(buffer.data());
  const auto* descriptor = reinterpret_cast<const esp_app_desc_t*>(
      buffer.data() + descriptor_offset);
  if (image->magic != ESP_IMAGE_HEADER_MAGIC ||
      descriptor->magic_word != ESP_APP_DESC_MAGIC_WORD ||
      !DescriptorFieldEquals(descriptor->project_name,
                             sizeof(descriptor->project_name), kOtaProject) ||
      !DescriptorFieldEquals(descriptor->version, sizeof(descriptor->version),
                             manifest.version.c_str()) ||
      !ota_has_suffix(manifest.version.c_str(), kOtaV2VersionSuffix)) {
    ESP_LOGE(TAG, "image descriptor rejected: project/board/version mismatch");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  esp_ota_handle_t handle = 0;
  err = esp_ota_begin(update, manifest.size, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "inactive-slot OTA begin failed: %s", esp_err_to_name(err));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool hash_started = mbedtls_sha256_starts(&sha, 0) == 0;
  std::size_t total = 0;
  auto write_chunk = [&](const uint8_t* data, std::size_t size) {
    if (!hash_started || total + size > manifest.size ||
        mbedtls_sha256_update(&sha, data, size) != 0 ||
        esp_ota_write(handle, data, size) != ESP_OK) return false;
    total += size;
    return true;
  };

  bool ok = write_chunk(buffer.data(), first_size);
  while (ok && total < manifest.size) {
    const std::size_t wanted = std::min(buffer.size(), manifest.size - total);
    const int n = esp_http_client_read(client, reinterpret_cast<char*>(buffer.data()),
                                       static_cast<int>(wanted));
    if (n <= 0) {
      ok = false;
      break;
    }
    ok = write_chunk(buffer.data(), static_cast<std::size_t>(n));
  }
  uint8_t actual[32]{}, expected[32]{};
  if (ok) ok = mbedtls_sha256_finish(&sha, actual) == 0 &&
               HexToDigest(manifest.sha256, expected) &&
               std::memcmp(actual, expected, sizeof(actual)) == 0;
  mbedtls_sha256_free(&sha);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (!ok) {
    esp_ota_abort(handle);
    ESP_LOGE(TAG, "image download/write/hash validation failed after %u bytes",
             static_cast<unsigned>(total));
    return false;
  }
  err = esp_ota_end(handle);  // Full ESP-IDF image validation before slot switch.
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "image validation failed: %s", esp_err_to_name(err));
    return false;
  }
  err = esp_ota_set_boot_partition(update);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "inactive slot selection failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGW(TAG, "V2 automatic OTA accepted: version=%s bytes=%u slot=%s; rebooting",
           manifest.version.c_str(), static_cast<unsigned>(manifest.size),
           update->label);
  vTaskDelay(pdMS_TO_TICKS(250));
  esp_restart();
  return true;
}

// Returns true when the authenticated manifest check completed normally (whether
// or not it offered an update). False causes a shorter retry interval.
bool CheckAndInstall() {
  const esp_app_desc_t* current = esp_app_get_description();
  const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);
  if (!current || !update) {
    ESP_LOGE(TAG, "no inactive OTA slot available");
    return false;
  }
  Manifest manifest;
  if (!FetchManifest(current->version, manifest)) return false;
  const OtaManifestDecision decision = validate_v2_ota_manifest(
      manifest.Fields(), current->version, OTA_ALLOWED_ORIGIN, update->size);
  if (decision == OtaManifestDecision::kInvalid) {
    ESP_LOGE(TAG, "authenticated manifest rejected by V2 policy");
    return false;
  }
  if (decision == OtaManifestDecision::kNoUpdate) {
    ESP_LOGI(TAG, "automatic OTA check complete: current=%s offered=%s",
             current->version, manifest.version.c_str());
    return true;
  }
  ESP_LOGW(TAG, "new authenticated V2 firmware available: %s -> %s",
           current->version, manifest.version.c_str());
  return DownloadAndInstall(manifest, current->version, update);
}

bool StationConnected() {
  wifi_ap_record_t access_point{};
  return esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
}
}  // namespace

void auto_ota_task(void*) {
#if BOARD_IS_V1
  ESP_LOGI(TAG, "automatic OTA disabled: release channel is V2-only");
  vTaskDelete(nullptr);
#else
  ESP_LOGI(TAG, "automatic OTA V2 policy active: authenticated HTTPS manifest, 6h cadence");
  vTaskDelay(kStartupDelay);
  while (true) {
    if (!StationConnected() || NetworkPortal::GetInstance().IsPortalActive()) {
      vTaskDelay(kOfflineRetry);
      continue;
    }
    bool attempted = false;
    bool checked = false;
    {
      RuntimeOperationLease operation(runtime_operation_gate(),
                                      RuntimeOperation::kAutomaticOta);
      if (!operation.Acquired()) {
        ESP_LOGI(TAG, "automatic OTA deferred: %s active",
                 runtime_operation_name(runtime_operation_gate().Current()));
      } else if (!StationConnected() ||
                 NetworkPortal::GetInstance().IsPortalActive()) {
        ESP_LOGI(TAG, "automatic OTA deferred after lock: network/portal changed");
      } else {
        attempted = true;
        checked = CheckAndInstall();
      }
    }
    vTaskDelay(!attempted ? kOfflineRetry
                          : (checked ? kNormalInterval : kFailureRetry));
  }
#endif
}

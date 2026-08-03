#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

static constexpr char kOtaProject[] = "coinbase_amoled_1_8";
static constexpr char kOtaV2Board[] = "v2-co5300-cst820";
static constexpr char kOtaV2VersionSuffix[] = "-v2";
static constexpr std::size_t kOtaAppVersionCapacity = 32;
static constexpr std::size_t kOtaMinimumImageBytes = 1024;
static constexpr std::size_t kOtaMaximumImageBytes = 7 * 1024 * 1024;

struct OtaManifestFields {
  const char* project = nullptr;
  const char* board = nullptr;
  const char* version = nullptr;
  const char* url = nullptr;
  const char* sha256 = nullptr;
  std::size_t size = 0;
};

enum class OtaManifestDecision {
  kInstall,
  kNoUpdate,
  kInvalid,
};

inline bool ota_string_equals(const char* value, const char* expected) {
  return value && expected && std::strcmp(value, expected) == 0;
}

inline bool ota_has_suffix(const char* value, const char* suffix) {
  if (!value || !suffix) return false;
  const std::size_t value_len = std::strlen(value);
  const std::size_t suffix_len = std::strlen(suffix);
  return value_len > suffix_len &&
         std::memcmp(value + value_len - suffix_len, suffix, suffix_len) == 0;
}

inline bool ota_copy_bounded_field(const char* field, std::size_t capacity,
                                   char* out, std::size_t out_capacity) {
  if (!field || !out || capacity == 0 || out_capacity == 0) return false;
  const std::size_t length = strnlen(field, capacity);
  if (length == capacity || length + 1 > out_capacity) return false;
  std::memcpy(out, field, length);
  out[length] = '\0';
  return true;
}

inline bool ota_version_fits_app_descriptor(const char* version) {
  return version && strnlen(version, kOtaAppVersionCapacity) <
                        kOtaAppVersionCapacity;
}

// Strict dotted numeric versions with the mandatory board suffix, for example
// 2.0.0-v2. Up to four numeric components are accepted.
inline bool ota_parse_v2_version(const char* version, uint32_t parts[4],
                                 std::size_t& count) {
  count = 0;
  for (int i = 0; i < 4; ++i) parts[i] = 0;
  if (!ota_version_fits_app_descriptor(version)) return false;
  if (!ota_has_suffix(version, kOtaV2VersionSuffix)) return false;
  const std::size_t core_len = std::strlen(version) - std::strlen(kOtaV2VersionSuffix);
  std::size_t pos = 0;
  while (pos < core_len && count < 4) {
    if (!std::isdigit(static_cast<unsigned char>(version[pos]))) return false;
    uint64_t value = 0;
    while (pos < core_len && std::isdigit(static_cast<unsigned char>(version[pos]))) {
      value = value * 10 + static_cast<unsigned>(version[pos] - '0');
      if (value > UINT32_MAX) return false;
      ++pos;
    }
    parts[count++] = static_cast<uint32_t>(value);
    if (pos == core_len) break;
    if (version[pos++] != '.' || pos == core_len) return false;
  }
  return count > 0 && pos == core_len;
}

inline int ota_compare_v2_versions(const char* current, const char* offered,
                                   bool& valid) {
  uint32_t a[4]{}, b[4]{};
  std::size_t ac = 0, bc = 0;
  valid = ota_parse_v2_version(current, a, ac) &&
          ota_parse_v2_version(offered, b, bc);
  if (!valid) return 0;
  for (int i = 0; i < 4; ++i) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}

inline bool ota_valid_sha256_hex(const char* sha) {
  if (!sha || std::strlen(sha) != 64) return false;
  for (int i = 0; i < 64; ++i)
    if (!std::isxdigit(static_cast<unsigned char>(sha[i]))) return false;
  return true;
}

inline bool ota_https_url_allowed(const char* url, const char* allowed_origin) {
  if (!url || !allowed_origin || std::strncmp(url, "https://", 8) != 0 ||
      std::strncmp(allowed_origin, "https://", 8) != 0) return false;
  const std::size_t origin_len = std::strlen(allowed_origin);
  if (origin_len < 9 || allowed_origin[origin_len - 1] == '/' ||
      std::strncmp(url, allowed_origin, origin_len) != 0) return false;
  // Exact HTTPS origin only. This prevents a manifest from redirecting the
  // embedded bearer token or image request to another host.
  return url[origin_len] == '/';
}

inline OtaManifestDecision validate_v2_ota_manifest(
    const OtaManifestFields& manifest, const char* current_version,
    const char* allowed_origin, std::size_t update_partition_size) {
  if (!ota_string_equals(manifest.project, kOtaProject) ||
      !ota_string_equals(manifest.board, kOtaV2Board) ||
      !ota_valid_sha256_hex(manifest.sha256) ||
      !ota_https_url_allowed(manifest.url, allowed_origin) ||
      manifest.size < kOtaMinimumImageBytes ||
      manifest.size > kOtaMaximumImageBytes ||
      manifest.size > update_partition_size) return OtaManifestDecision::kInvalid;
  bool valid = false;
  const int comparison = ota_compare_v2_versions(current_version,
                                                  manifest.version, valid);
  if (!valid) return OtaManifestDecision::kInvalid;
  return comparison < 0 ? OtaManifestDecision::kInstall
                        : OtaManifestDecision::kNoUpdate;
}

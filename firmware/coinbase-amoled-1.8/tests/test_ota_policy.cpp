#include <cassert>
#include <cstring>
#include "../main/ota_policy.h"

static const char* kHash =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

int main() {
  uint32_t parts[4]{};
  std::size_t count = 0;
  assert(ota_parse_v2_version("2.0.0-v2", parts, count));
  assert(count == 3 && parts[0] == 2 && parts[1] == 0 && parts[2] == 0);
  assert(!ota_parse_v2_version("2.0.0-v1", parts, count));
  assert(!ota_parse_v2_version("2.x.0-v2", parts, count));
  assert(!ota_parse_v2_version("4294967296.0-v2", parts, count));
  assert(!ota_parse_v2_version("1.2.3.4.5-v2", parts, count));
  assert(ota_parse_v2_version("4294967295.0-v2", parts, count));
  bool valid = false;
  assert(ota_compare_v2_versions("2.0.0-v2", "2.0.1-v2", valid) < 0 && valid);
  assert(ota_compare_v2_versions("2.1-v2", "2.0.9-v2", valid) > 0 && valid);
  assert(ota_compare_v2_versions("2.0.0-v2", "2.0.0-v2", valid) == 0 && valid);

  assert(ota_https_url_allowed("https://updates.example.test/v2/app.bin",
                               "https://updates.example.test"));
  assert(!ota_https_url_allowed("http://updates.example.test/v2/app.bin",
                                "https://updates.example.test"));
  assert(!ota_https_url_allowed("https://evil.example.test/app.bin",
                                "https://updates.example.test"));
  assert(!ota_https_url_allowed("https://updates.example.test.evil/app.bin",
                                "https://updates.example.test"));

  char bounded[33]{};
  const char valid_field[32] = "2.0.1-v2";
  assert(ota_copy_bounded_field(valid_field, sizeof(valid_field), bounded,
                                sizeof(bounded)));
  assert(std::strcmp(bounded, "2.0.1-v2") == 0);
  char unterminated[32];
  std::memset(unterminated, '9', sizeof(unterminated));
  assert(!ota_copy_bounded_field(unterminated, sizeof(unterminated), bounded,
                                 sizeof(bounded)));
  assert(!ota_version_fits_app_descriptor(unterminated));

  OtaManifestFields good{kOtaProject, kOtaV2Board, "2.0.1-v2",
                         "https://updates.example.test/v2/app.bin", kHash,
                         1024 * 1024};
  assert(validate_v2_ota_manifest(good, "2.0.0-v2",
                                  "https://updates.example.test", 7 * 1024 * 1024) ==
         OtaManifestDecision::kInstall);
  assert(validate_v2_ota_manifest(good, "2.0.1-v2",
                                  "https://updates.example.test", 7 * 1024 * 1024) ==
         OtaManifestDecision::kNoUpdate);

  OtaManifestFields bad = good;
  bad.board = "v1-sh8601-ft5x06";
  assert(validate_v2_ota_manifest(bad, "2.0.0-v2",
                                  "https://updates.example.test", 7 * 1024 * 1024) ==
         OtaManifestDecision::kInvalid);
  bad = good; bad.project = "other_project";
  assert(validate_v2_ota_manifest(bad, "2.0.0-v2",
                                  "https://updates.example.test", 7 * 1024 * 1024) ==
         OtaManifestDecision::kInvalid);
  bad = good; bad.version = "9.0.0-v1";
  assert(validate_v2_ota_manifest(bad, "2.0.0-v2",
                                  "https://updates.example.test", 7 * 1024 * 1024) ==
         OtaManifestDecision::kInvalid);
  bad = good; bad.sha256 = "abcd";
  assert(validate_v2_ota_manifest(bad, "2.0.0-v2",
                                  "https://updates.example.test", 7 * 1024 * 1024) ==
         OtaManifestDecision::kInvalid);
  bad = good; bad.size = 8 * 1024 * 1024;
  assert(validate_v2_ota_manifest(bad, "2.0.0-v2",
                                  "https://updates.example.test", 9 * 1024 * 1024) ==
         OtaManifestDecision::kInvalid);
  return 0;
}

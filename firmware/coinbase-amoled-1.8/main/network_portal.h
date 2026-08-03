#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class NetworkPortal {
public:
    static NetworkPortal& GetInstance();

    void Initialize(std::function<void(bool)> connection_callback,
                    std::function<void()> state_callback);
    void Suspend();
    void Resume();
    void ArmOta();
    void SaveCredential(const std::string& ssid, const std::string& password);

    bool IsConnected() const;
    bool IsPortalActive() const;
    bool IsOtaArmed() const;
    std::string GetApSsid() const;
    std::string GetOtaCode() const;
    bool ValidateOtaCode(const char* code) const;

private:
    NetworkPortal() = default;
    NetworkPortal(const NetworkPortal&) = delete;
    NetworkPortal& operator=(const NetworkPortal&) = delete;

    void StartPortal(bool allow_when_connected = false);
    void StopPortal();
    void LoadCredentials();
    void ApplyCredential(size_t index);
    void NotifyState();
    void RestartConnectionTimer();
    bool HasCredentials() const;

    static void EventHandler(void* arg, const char* event_base, int32_t event_id, void* event_data);
    static void ConnectionTimeout(void* arg);
    static void OtaTimeout(void* arg);

    std::function<void(bool)> connection_callback_;
    std::function<void()> state_callback_;
    bool connected_ = false;
    bool portal_active_ = false;
    bool ota_armed_ = false;
    bool suspended_ = false;
    bool resume_portal_ = false;
    size_t credential_index_ = 0;
    int64_t ota_deadline_us_ = 0;
    std::string ap_ssid_;
    std::string ota_code_;
    mutable SemaphoreHandle_t state_mutex_ = nullptr;
    SemaphoreHandle_t lifecycle_mutex_ = nullptr;
};

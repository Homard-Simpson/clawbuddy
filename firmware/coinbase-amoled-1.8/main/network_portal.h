#pragma once

#include <functional>
#include <string>

class NetworkPortal {
public:
    static NetworkPortal& GetInstance();

    void Initialize(std::function<void(bool)> connection_callback,
                    std::function<void()> state_callback);
    void ArmOta();
    void SaveCredential(const std::string& ssid, const std::string& password);

    bool IsConnected() const { return connected_; }
    bool IsPortalActive() const { return portal_active_; }
    bool IsOtaArmed() const { return ota_armed_; }
    const std::string& GetApSsid() const { return ap_ssid_; }
    const std::string& GetOtaCode() const { return ota_code_; }

private:
    NetworkPortal() = default;
    NetworkPortal(const NetworkPortal&) = delete;
    NetworkPortal& operator=(const NetworkPortal&) = delete;

    void StartPortal();
    void StopPortal();
    void LoadCredentials();
    void ApplyCredential(size_t index);
    void NotifyState();

    static void EventHandler(void* arg, const char* event_base, int32_t event_id, void* event_data);
    static void ConnectionTimeout(void* arg);
    static void OtaTimeout(void* arg);

    std::function<void(bool)> connection_callback_;
    std::function<void()> state_callback_;
    bool connected_ = false;
    bool portal_active_ = false;
    bool ota_armed_ = false;
    size_t credential_index_ = 0;
    std::string ap_ssid_;
    std::string ota_code_;
};

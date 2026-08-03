#pragma once

#include <atomic>
#include <cstdint>

enum class RuntimeOperation : uint8_t {
  kNone = 0,
  kManualOta,
  kAutomaticOta,
  kFullStandby,
};

inline const char* runtime_operation_name(RuntimeOperation operation) {
  switch (operation) {
    case RuntimeOperation::kManualOta: return "manual OTA";
    case RuntimeOperation::kAutomaticOta: return "automatic OTA";
    case RuntimeOperation::kFullStandby: return "full standby";
    default: return "none";
  }
}

class RuntimeOperationGate {
 public:
  bool TryAcquire(RuntimeOperation operation) {
    if (operation == RuntimeOperation::kNone) return false;
    uint8_t expected = static_cast<uint8_t>(RuntimeOperation::kNone);
    return owner_.compare_exchange_strong(
        expected, static_cast<uint8_t>(operation), std::memory_order_acq_rel,
        std::memory_order_acquire);
  }

  bool Release(RuntimeOperation operation) {
    if (operation == RuntimeOperation::kNone) return false;
    uint8_t expected = static_cast<uint8_t>(operation);
    return owner_.compare_exchange_strong(
        expected, static_cast<uint8_t>(RuntimeOperation::kNone),
        std::memory_order_acq_rel, std::memory_order_acquire);
  }

  RuntimeOperation Current() const {
    return static_cast<RuntimeOperation>(owner_.load(std::memory_order_acquire));
  }

  bool OtaInProgress() const {
    const RuntimeOperation operation = Current();
    return operation == RuntimeOperation::kManualOta ||
           operation == RuntimeOperation::kAutomaticOta;
  }

 private:
  std::atomic<uint8_t> owner_{static_cast<uint8_t>(RuntimeOperation::kNone)};
};

RuntimeOperationGate& runtime_operation_gate();

class RuntimeOperationLease {
 public:
  RuntimeOperationLease(RuntimeOperationGate& gate, RuntimeOperation operation)
      : gate_(gate), operation_(operation), acquired_(gate_.TryAcquire(operation)) {}

  ~RuntimeOperationLease() {
    if (acquired_ && release_on_destroy_) gate_.Release(operation_);
  }

  RuntimeOperationLease(const RuntimeOperationLease&) = delete;
  RuntimeOperationLease& operator=(const RuntimeOperationLease&) = delete;

  bool Acquired() const { return acquired_; }

  // Successful OTA keeps the gate closed until the scheduled reboot. This
  // prevents another writer or full standby from entering during the response
  // and restart window.
  void RetainUntilRestart() {
    if (acquired_) release_on_destroy_ = false;
  }

 private:
  RuntimeOperationGate& gate_;
  RuntimeOperation operation_;
  bool acquired_ = false;
  bool release_on_destroy_ = true;
};

#include <cassert>
#include "../main/runtime_operation.h"

int main() {
  RuntimeOperationGate gate;
  assert(gate.Current() == RuntimeOperation::kNone);
  assert(!gate.OtaInProgress());
  assert(gate.TryAcquire(RuntimeOperation::kAutomaticOta));
  assert(gate.Current() == RuntimeOperation::kAutomaticOta);
  assert(gate.OtaInProgress());
  assert(!gate.TryAcquire(RuntimeOperation::kManualOta));
  assert(!gate.TryAcquire(RuntimeOperation::kFullStandby));
  assert(!gate.Release(RuntimeOperation::kManualOta));
  assert(gate.Release(RuntimeOperation::kAutomaticOta));
  assert(gate.TryAcquire(RuntimeOperation::kFullStandby));
  assert(!gate.OtaInProgress());
  assert(!gate.TryAcquire(RuntimeOperation::kAutomaticOta));
  assert(gate.Release(RuntimeOperation::kFullStandby));

  {
    RuntimeOperationLease lease(gate, RuntimeOperation::kManualOta);
    assert(lease.Acquired());
    assert(gate.OtaInProgress());
  }
  assert(gate.Current() == RuntimeOperation::kNone);
  {
    RuntimeOperationLease lease(gate, RuntimeOperation::kManualOta);
    assert(lease.Acquired());
    lease.RetainUntilRestart();
  }
  assert(gate.Current() == RuntimeOperation::kManualOta);
  assert(gate.Release(RuntimeOperation::kManualOta));
  return 0;
}

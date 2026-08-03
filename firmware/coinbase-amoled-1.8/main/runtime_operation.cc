#include "runtime_operation.h"

RuntimeOperationGate& runtime_operation_gate() {
  static RuntimeOperationGate gate;
  return gate;
}

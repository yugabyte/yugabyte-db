// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_bfunc.h"
#include <glog/logging.h>

namespace yb {

using std::shared_ptr;
using std::vector;

using yb::bfyql::BFOpcode;
using YQLBfuncExecApi = yb::bfyql::BFExecApi<YQLValue, YQLValue>;
using YQLBfuncExecApiPB = yb::bfyql::BFExecApi<YQLValueWithPB, YQLValueWithPB>;

Status YQLBfunc::Exec(BFOpcode opcode,
                      const vector<shared_ptr<YQLValue>>& params,
                      const shared_ptr<YQLValue>& result) {
  return YQLBfuncExecApi::ExecYqlOpcode(opcode, params, result);
}

Status YQLBfunc::Exec(BFOpcode opcode,
                      const vector<YQLValue*>& params,
                      YQLValue *result) {
  return YQLBfuncExecApi::ExecYqlOpcode(opcode, params, result);
}

Status YQLBfunc::Exec(BFOpcode opcode,
                      vector<YQLValue> *params,
                      YQLValue *result) {
  return YQLBfuncExecApi::ExecYqlOpcode(opcode, params, result);
}

Status YQLBfunc::Exec(BFOpcode opcode,
                      const vector<shared_ptr<YQLValueWithPB>>& params,
                      const shared_ptr<YQLValueWithPB>& result) {
  return YQLBfuncExecApiPB::ExecYqlOpcode(opcode, params, result);
}

Status YQLBfunc::Exec(BFOpcode opcode,
                      const vector<YQLValueWithPB*>& params,
                      YQLValueWithPB *result) {
  return YQLBfuncExecApiPB::ExecYqlOpcode(opcode, params, result);
}

Status YQLBfunc::Exec(BFOpcode opcode,
                      vector<YQLValueWithPB> *params,
                      YQLValueWithPB *result) {
  return YQLBfuncExecApiPB::ExecYqlOpcode(opcode, params, result);
}

} // namespace yb

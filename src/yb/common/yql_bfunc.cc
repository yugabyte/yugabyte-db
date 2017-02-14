// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_bfunc.h"
#include <glog/logging.h>

namespace yb {

using yb::bfyql::BFOpcode;
using YQLBfuncExecApi = yb::bfyql::BFExecApi<YQLValue, YQLValue>;

Status YQLBfunc::Exec(BFOpcode opcode,
                      const std::vector<std::shared_ptr<YQLValue>>& params,
                      const std::shared_ptr<YQLValue>& result) {
  return YQLBfuncExecApi::ExecYqlOpcode(opcode, params, result);
}

Status YQLBfunc::Exec(BFOpcode opcode,
                      const std::vector<YQLValue*>& params,
                      YQLValue *result) {
  return YQLBfuncExecApi::ExecYqlOpcode(opcode, params, result);
}

} // namespace yb

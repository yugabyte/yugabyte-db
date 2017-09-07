// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

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

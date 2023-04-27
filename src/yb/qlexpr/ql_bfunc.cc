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

#include "yb/qlexpr/ql_bfunc.h"

#include "yb/bfpg/bfpg.h"
#include "yb/bfql/bfql.h"

namespace yb::qlexpr {

//--------------------------------------------------------------------------------------------------
// CQL support
Result<bfql::BFRetValue> ExecBfunc(bfql::BFOpcode opcode, const bfql::BFParams& params) {
  return bfql::BFExecApi::ExecQLOpcode(opcode, params);
}

//--------------------------------------------------------------------------------------------------
// PGSQL support
Result<bfpg::BFRetValue> ExecBfunc(bfpg::BFOpcode opcode, const bfpg::BFParams& params) {
  return bfpg::BFExecApi::ExecPgsqlOpcode(opcode, params);
}

}  // namespace yb::qlexpr

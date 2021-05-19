//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_analyze.h"
#include "yb/common/wire_protocol.h"
#include "yb/util/status.h"
#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/client/yb_op.h"
#include "yb/docdb/primitive_value.h"

namespace yb {
namespace pggate {

Status PgAnalyze::GetOkOrRespError() {
  return resp_.has_error() ? StatusFromPB(resp_.error().status()) : Status::OK();
}

Status PgAnalyze::Exec() {
  resp_ = VERIFY_RESULT(pg_session_->AnalyzeTable(table_id_));
  return GetOkOrRespError();
}

Result<int32_t> PgAnalyze::GetNumRows() {
  RETURN_NOT_OK(GetOkOrRespError());
  return resp_.rows();
}

}  // namespace pggate
}  // namespace yb

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

#include "yb/common/ql_value.h"
#include "yb/common/table_properties_constants.h"
#include "yb/common/typedefs.h"

#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/exec/executor.h"

#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/util/errcodes.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

Status Executor::PTExprToPBValidated(const PTExprPtr& expr,
                                     QLExpressionPB *expr_pb) {
  RETURN_NOT_OK(PTExprToPB(expr, expr_pb));
  if (expr_pb->has_value() && IsNull(expr_pb->value())) {
    return exec_context_->Error(expr, "Value cannot be null.", ErrorCode::INVALID_ARGUMENTS);
  }
  return Status::OK();
}

Status Executor::TimestampToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req) {
  if (tnode->user_timestamp_usec() != nullptr) {
    QLExpressionPB timestamp_pb;
    RETURN_NOT_OK(PTExprToPBValidated(tnode->user_timestamp_usec(), &timestamp_pb));

    // This should be ensured by checks before getting here.
    DCHECK(timestamp_pb.has_value() && timestamp_pb.value().has_int64_value())
        << "Integer constant expected for USING TIMESTAMP clause";

    UserTimeMicros user_timestamp = timestamp_pb.value().int64_value();
    if (user_timestamp == common::kInvalidTimestamp) {
      return exec_context_->Error(tnode->user_timestamp_usec(), "Invalid timestamp",
                                  ErrorCode::INVALID_ARGUMENTS);
    }
    req->set_user_timestamp_usec(timestamp_pb.value().int64_value());
  }
  return Status::OK();
}

Status Executor::TtlToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req) {
  if (tnode->ttl_seconds() != nullptr) {
    QLExpressionPB ttl_pb;
    RETURN_NOT_OK(PTExprToPBValidated(tnode->ttl_seconds(), &ttl_pb));

    // this should be ensured by checks before getting here
    DCHECK(ttl_pb.has_value() && ttl_pb.value().has_int32_value())
        << "Integer constant expected for USING TTL clause";

    int32_t ttl_seconds = ttl_pb.value().int32_value();

    if (!yb::common::IsValidTTLSeconds(ttl_seconds)) {
      return exec_context_->Error(tnode->ttl_seconds(),
                                  strings::Substitute("Valid ttl range : [$0, $1]",
                                                      yb::common::kCassandraMinTtlSeconds,
                                                      yb::common::kCassandraMaxTtlSeconds).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
    }
    req->set_ttl(static_cast<uint64_t>(ttl_seconds * MonoTime::kMillisecondsPerSecond));
  }
  return Status::OK();
}

}  // namespace ql
}  // namespace yb

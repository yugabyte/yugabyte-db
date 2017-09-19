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

#include "yb/ql/exec/executor.h"

namespace yb {
namespace ql {

using std::shared_ptr;

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::TtlToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req) {
  if (tnode->ttl_seconds() != nullptr) {
    QLExpressionPB ttl_pb;
    RETURN_NOT_OK(PTExprToPB(tnode->ttl_seconds(), &ttl_pb));
    if (ttl_pb.has_value() && QLValue::IsNull(ttl_pb.value())) {
      return exec_context_->Error("TTL value cannot be null.", ErrorCode::INVALID_ARGUMENTS);
    }

    // this should be ensured by checks before getting here
    DCHECK(ttl_pb.has_value() && ttl_pb.value().has_int64_value())
        << "Integer constant expected for USING TTL clause";

    int64_t ttl_seconds = ttl_pb.value().int64_value();

    if (!yb::common::IsValidTTLSeconds(ttl_seconds)) {
      return exec_context_->Error(tnode->ttl_seconds(),
                                  strings::Substitute("Valid ttl range : [$0, $1]",
                                                      yb::common::kMinTtlSeconds,
                                                      yb::common::kMaxTtlSeconds).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
    }
    req->set_ttl(static_cast<uint64_t>(ttl_seconds * MonoTime::kMillisecondsPerSecond));
  }
  return Status::OK();
}

}  // namespace ql
}  // namespace yb

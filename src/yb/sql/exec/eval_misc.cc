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

#include "yb/sql/exec/executor.h"

namespace yb {
namespace sql {

using std::shared_ptr;

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::TtlToPB(const PTDmlStmt *tnode, YQLWriteRequestPB *req) {
  if (tnode->has_ttl()) {
    YQLExpressionPB ttl_pb;
    RETURN_NOT_OK(PTExprToPB(tnode->ttl_seconds(), &ttl_pb));
    if (ttl_pb.has_value() && YQLValue::IsNull(ttl_pb.value())) {
      return exec_context_->Error(tnode->loc(),
                                  "TTL value cannot be null.",
                                  ErrorCode::INVALID_ARGUMENTS);
    }

    // this should be ensured by checks before getting here
    DCHECK(ttl_pb.has_value() && ttl_pb.value().has_int64_value())
        << "Integer constant expected for USING TTL clause";

    int64_t ttl_seconds = ttl_pb.value().int64_value();

    if (!yb::common::IsValidTTLSeconds(ttl_seconds)) {
      return exec_context_->Error(tnode->ttl_seconds()->loc(),
          strings::Substitute("Valid ttl range : [$0, $1]",
              yb::common::kMinTtlSeconds,
              yb::common::kMaxTtlSeconds).c_str(),
          ErrorCode::INVALID_ARGUMENTS);
    }
    req->set_ttl(static_cast<uint64_t>(ttl_seconds * MonoTime::kMillisecondsPerSecond));
  }
  return Status::OK();
}

}  // namespace sql
}  // namespace yb

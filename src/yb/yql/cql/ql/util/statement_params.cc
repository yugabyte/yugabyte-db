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
#include "yb/yql/cql/ql/util/statement_params.h"

#include "yb/common/read_hybrid_time.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
namespace ql {

using std::string;

StatementParameters::StatementParameters()
    : page_size_(INT64_MAX),
      yb_consistency_level_(YBConsistencyLevel::STRONG) {
}

StatementParameters::StatementParameters(const StatementParameters& other)
    : page_size_(other.page_size_),
      paging_state_(
        other.paging_state_ != nullptr ? new QLPagingStatePB(*other.paging_state_) : nullptr),
      yb_consistency_level_(YBConsistencyLevel::STRONG) {
}

StatementParameters::~StatementParameters() {
}

ReadHybridTime StatementParameters::read_time() const {
  if (!paging_state_) {
    return ReadHybridTime();
  }

  return ReadHybridTime::FromReadTimePB(*paging_state_);
}

Status StatementParameters::SetPagingState(const std::string& paging_state) {
  // For performance, create QLPagingStatePB on demand only when setting paging state because
  // only SELECT statements continuing from a previous page carry a paging state.
  if (paging_state_ == nullptr) {
    paging_state_.reset(new QLPagingStatePB());
  }
  if (!paging_state_->ParseFromString(paging_state)) {
    return STATUS(Corruption, "Invalid paging state");
  }

  if (paging_state_->has_original_request_id()) {
    request_id_ = paging_state_->original_request_id();
  }

  return Status::OK();
}

Result<bool> StatementParameters::IsBindVariableUnset(const std::string& name,
                                         int64_t pos) const {
  return STATUS(RuntimeError, "no bind variable available");
}

// Retrieve a bind variable for the execution of the statement. To be overridden by subclasses
// to return actual bind variables.
Status StatementParameters::GetBindVariable(const std::string& name,
                                            int64_t pos,
                                            const std::shared_ptr<QLType>& type,
                                            QLValue* value) const {
  return STATUS(RuntimeError, "no bind variable available");
}

} // namespace ql
} // namespace yb

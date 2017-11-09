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

#include <glog/logging.h>

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

} // namespace ql
} // namespace yb

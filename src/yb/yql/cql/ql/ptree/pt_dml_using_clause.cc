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

#include "yb/yql/cql/ql/ptree/pt_dml_using_clause.h"

namespace yb {
namespace ql {

Status PTDmlUsingClause::Analyze(SemContext* sem_context) {
  for (PTDmlUsingClauseElement::SharedPtr tnode : node_list()) {
    RETURN_NOT_OK(tnode->Analyze(sem_context));
    // The last one in the list wins.
    if (tnode->IsTTL()) {
      ttl_seconds_ = tnode->value();
    } else if (tnode->IsTimestamp()) {
      user_timestamp_usec_ = tnode->value();
    }
  }
  return Status::OK();
}

const PTExprPtr& PTDmlUsingClause::ttl_seconds() const {
  return ttl_seconds_;
}

const PTExprPtr& PTDmlUsingClause::user_timestamp_usec() const {
  return user_timestamp_usec_;
}

} // namespace ql
} // namespace yb

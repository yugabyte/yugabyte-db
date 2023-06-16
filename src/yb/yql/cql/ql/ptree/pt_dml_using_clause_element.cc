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

#include "yb/yql/cql/ql/ptree/pt_dml_using_clause_element.h"

#include "yb/common/ql_type.h"

#include "yb/util/status.h"

#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"
#include "yb/yql/cql/ql/util/errcodes.h"

namespace yb {
namespace ql {

namespace {

constexpr const char* const kTtl = "ttl";
constexpr const char* const kTimestamp = "timestamp";

}

PTDmlUsingClauseElement::PTDmlUsingClauseElement(MemoryContext *memctx,
                                                 YBLocationPtr loc,
                                                 const MCSharedPtr<MCString>& name,
                                                 const PTExprPtr& value)
    : TreeNode(memctx, loc),
      name_(name),
      value_(value) {
}

PTDmlUsingClauseElement::~PTDmlUsingClauseElement() {
}

Status PTDmlUsingClauseElement::Analyze(SemContext *sem_context) {
  if (name_ == nullptr) {
    return sem_context->Error(
        this,
        "Undefined parameter name in DML using clause element",
        ErrorCode::INVALID_ARGUMENTS);
  }

  if (strcmp(name_->c_str(), kTtl) != 0 && strcmp(name_->c_str(), kTimestamp) != 0) {
    return sem_context->Error(
        this,
        strings::Substitute("Invalid parameter $0, only $1 and $2 are supported",
                            name_->c_str(), kTtl, kTimestamp).c_str(),
        ErrorCode::INVALID_ARGUMENTS);
  }

  if (value_ == nullptr) {
    return sem_context->Error(
        this,
        strings::Substitute("Invalid value for parameter $0", name_->c_str()).c_str(),
        ErrorCode::INVALID_ARGUMENTS);
  }

  RETURN_NOT_OK(value_->CheckRhsExpr(sem_context));

  SemState sem_state(sem_context);
  if (strcmp(name_->c_str(), kTtl) == 0) {
    sem_state.SetExprState(QLType::Create(DataType::INT32), InternalType::kInt32Value);
    sem_state.set_bindvar_name(PTBindVar::ttl_bindvar_name());
  } else {
    // has to be timestamp.
    DCHECK_EQ(0, strcmp(name_->c_str(), kTimestamp));
    sem_state.SetExprState(QLType::Create(DataType::INT64), InternalType::kInt64Value);
    sem_state.set_bindvar_name(PTBindVar::timestamp_bindvar_name());
  }

  RETURN_NOT_OK(value_->Analyze(sem_context));

  return Status::OK();
}

bool PTDmlUsingClauseElement::IsTTL() const {
  return strcmp(name_->c_str(), kTtl) == 0;
}

bool PTDmlUsingClauseElement::IsTimestamp() const {
  return strcmp(name_->c_str(), kTimestamp) == 0;
}

} // namespace ql
} // namespace yb

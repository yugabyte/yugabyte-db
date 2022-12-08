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

#pragma once

#include "yb/yql/cql/ql/ptree/pt_dml_using_clause_element.h"
#include "yb/yql/cql/ql/ptree/list_node.h"

namespace yb {
namespace ql {

// USING clause for INSERT, UPDATE and DELETE statements.
class PTDmlUsingClause: public TreeListNode<PTDmlUsingClauseElement> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDmlUsingClause> SharedPtr;
  typedef MCSharedPtr<const PTDmlUsingClause> SharedPtrConst;

  explicit PTDmlUsingClause(MemoryContext *memory_context,
                            YBLocationPtr loc,
                            const MCSharedPtr<PTDmlUsingClauseElement>& tnode = nullptr)
      : TreeListNode<PTDmlUsingClauseElement>(memory_context, loc, tnode),
        ttl_seconds_(nullptr),
        user_timestamp_usec_(nullptr) {
  }

  virtual ~PTDmlUsingClause() {
  }

  template<typename... TypeArgs>
  inline static PTDmlUsingClause::SharedPtr MakeShared(MemoryContext *memctx,
                                                       TypeArgs&&...args) {
    return MCMakeShared<PTDmlUsingClause>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual Status Analyze(SemContext *sem_context) override;

  const PTExprPtr& ttl_seconds() const;

  const PTExprPtr& user_timestamp_usec() const;

  bool has_user_timestamp_usec() const {
    return user_timestamp_usec_ != nullptr;
  }

  bool has_ttl_seconds() const {
    return ttl_seconds_ != nullptr;
  }

 private:
  PTExprPtr ttl_seconds_;
  PTExprPtr user_timestamp_usec_;
};

} // namespace ql
} // namespace yb

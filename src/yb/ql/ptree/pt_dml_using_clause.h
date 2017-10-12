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

#ifndef YB_QL_PTREE_PT_DML_USING_CLAUSE_H
#define YB_QL_PTREE_PT_DML_USING_CLAUSE_H

#include "yb/ql/ptree/pt_dml_using_clause_element.h"

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
                            YBLocation::SharedPtr loc,
                            const MCSharedPtr<PTDmlUsingClauseElement>& tnode = nullptr)
      : TreeListNode<PTDmlUsingClauseElement>(memory_context, loc, tnode),
        ttl_seconds_(nullptr),
        user_timestamp_micros_(nullptr) {
  }

  virtual ~PTDmlUsingClause() {
  }

  template<typename... TypeArgs>
  inline static PTDmlUsingClause::SharedPtr MakeShared(MemoryContext *memctx,
                                                       TypeArgs&&...args) {
    return MCMakeShared<PTDmlUsingClause>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  const PTExpr::SharedPtr& ttl_seconds() const;

  const PTExpr::SharedPtr& user_timestamp_micros() const;

 private:
  PTExpr::SharedPtr ttl_seconds_;
  PTExpr::SharedPtr user_timestamp_micros_;
};

} // namespace ql
} // namespace yb

#endif // YB_QL_PTREE_PT_DML_USING_CLAUSE_H

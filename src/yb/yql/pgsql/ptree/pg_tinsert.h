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
//
// Tree node definitions for INSERT statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_TINSERT_H_
#define YB_YQL_PGSQL_PTREE_PG_TINSERT_H_

#include "yb/yql/pgsql/ptree/list_node.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_tselect.h"
#include "yb/yql/pgsql/ptree/column_desc.h"
#include "yb/yql/pgsql/ptree/pg_tdml.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

class PgTInsertStmt : public PgTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTInsertStmt> SharedPtr;
  typedef MCSharedPtr<const PgTInsertStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTInsertStmt(MemoryContext *memctx,
               PgTLocation::SharedPtr loc,
               PgTQualifiedName::SharedPtr relation,
               PgTQualifiedNameListNode::SharedPtr columns,
               PgTCollection::SharedPtr value_clause);
  virtual ~PgTInsertStmt();

  template<typename... TypeArgs>
  inline static PgTInsertStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PgTInsertStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTInsertStmt;
  }

  // Table name.
  client::YBTableName table_name() const override {
    return relation_->ToTableName();
  }

  // Returns location of table name.
  const PgTLocation& table_loc() const override {
    return relation_->loc();
  }

 private:
  // The parser will constructs the following tree nodes.
  PgTQualifiedName::SharedPtr relation_;
  PgTQualifiedNameListNode::SharedPtr columns_;
  PgTCollection::SharedPtr value_clause_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TINSERT_H_

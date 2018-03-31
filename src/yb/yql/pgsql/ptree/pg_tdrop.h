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
// Tree node definitions for DROP statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_TDROP_H_
#define YB_YQL_PGSQL_PTREE_PG_TDROP_H_

#include "yb/yql/pgsql/ptree/list_node.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_ttype.h"
#include "yb/yql/pgsql/ptree/pg_tname.h"
#include "yb/yql/pgsql/ptree/pg_option.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------
// DROP <OBJECT> statement (<OBJECT> can be TABLE, SCHEMA, etc.).

class PgTDropStmt : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTDropStmt> SharedPtr;
  typedef MCSharedPtr<const PgTDropStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTDropStmt(MemoryContext *memctx,
             PgTLocation::SharedPtr loc,
             ObjectType drop_type,
             PgTQualifiedNameListNode::SharedPtr names,
             bool drop_if_exists);
  virtual ~PgTDropStmt();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTDropStmt;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PgTDropStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTDropStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  ObjectType drop_type() const {
    return drop_type_;
  }

  bool drop_if_exists() const {
    return drop_if_exists_;
  }

  // Name of the object being dropped.
  const PgTQualifiedName::SharedPtr name() const {
    return names_->element(0);
  }

  client::YBTableName yb_table_name() const {
    return name()->ToTableName();
  }

 private:
  ObjectType drop_type_;
  PgTQualifiedNameListNode::SharedPtr names_;

  // Set to true for DROP IF EXISTS statements.
  bool drop_if_exists_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TDROP_H_

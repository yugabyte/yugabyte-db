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
// Tree node definitions for CREATE DATABASE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_TDATABASE_H_
#define YB_YQL_PGSQL_PTREE_PG_TDATABASE_H_

#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_tname.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------
// CREATE DATABASE statement.

class PgTCreateDatabase : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTCreateDatabase> SharedPtr;
  typedef MCSharedPtr<const PgTCreateDatabase> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTCreateDatabase(MemoryContext *memctx, PgTLocation::SharedPtr loc, PgTName::SharedPtr name);
  virtual ~PgTCreateDatabase();

  template<typename... TypeArgs>
  inline static PgTCreateDatabase::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTCreateDatabase>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTCreateDatabase;
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  // Database name.
  const PgTName::SharedPtr& name() const {
    return name_;
  }

 private:
  PgTName::SharedPtr name_;
};

//--------------------------------------------------------------------------------------------------
// DROP DATABASE statement.

class PgTDropDatabase : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTDropDatabase> SharedPtr;
  typedef MCSharedPtr<const PgTDropDatabase> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTDropDatabase(MemoryContext *memctx,
                  PgTLocation::SharedPtr loc,
                  PgTName::SharedPtr name,
                  bool drop_if_exists);
  virtual ~PgTDropDatabase();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTDropDatabase;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  static PgTDropDatabase::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTDropDatabase>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  bool drop_if_exists() const {
    return drop_if_exists_;
  }

  // Name of the database being dropped.
  const PgTName::SharedPtr& name() const {
    return name_;
  }

 private:
  // Database name.
  PgTName::SharedPtr name_;

  // Set to true for DROP IF EXISTS statements.
  bool drop_if_exists_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TDATABASE_H_

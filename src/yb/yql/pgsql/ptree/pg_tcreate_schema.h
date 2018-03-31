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
// Tree node definitions for CREATE SCHEMA statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_TCREATE_SCHEMA_H_
#define YB_YQL_PGSQL_PTREE_PG_TCREATE_SCHEMA_H_

#include "yb/yql/pgsql/ptree/tree_node.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------
// CREATE SCHEMA statement.

class PgTCreateSchema : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTCreateSchema> SharedPtr;
  typedef MCSharedPtr<const PgTCreateSchema> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTCreateSchema(MemoryContext *memctx,
                  PgTLocation::SharedPtr loc,
                  const MCSharedPtr<MCString>& name,
                  bool create_if_not_exists);
  virtual ~PgTCreateSchema();

  template<typename... TypeArgs>
  inline static PgTCreateSchema::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTCreateSchema>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTCreateSchema;
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;
  bool create_if_not_exists() const {
    return create_if_not_exists_;
  }

  // Keyspace name.
  const char* name() const {
    return name_->c_str();
  }

 private:
  MCSharedPtr<MCString> name_;
  const bool create_if_not_exists_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TCREATE_SCHEMA_H_

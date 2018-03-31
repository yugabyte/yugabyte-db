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
// Treenode definitions for DROP statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/pg_tdrop.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

PgTDropStmt::PgTDropStmt(MemoryContext *memctx,
                       PgTLocation::SharedPtr loc,
                       ObjectType drop_type,
                       PgTQualifiedNameListNode::SharedPtr names,
                       bool drop_if_exists)
    : TreeNode(memctx, loc),
      drop_type_(drop_type),
      names_(names),
      drop_if_exists_(drop_if_exists) {
}

PgTDropStmt::~PgTDropStmt() {
}

CHECKED_STATUS PgTDropStmt::Analyze(PgCompileContext *compile_context) {
  if (names_->size() > 1) {
    return compile_context->Error(names_, "Only one object name is allowed in a drop statement",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  // Processing object name.
  PgTQualifiedName::SharedPtr obj_name = name();
  obj_name->set_object_type(drop_type_);
  return obj_name->Analyze(compile_context);
}

}  // namespace pgsql
}  // namespace yb

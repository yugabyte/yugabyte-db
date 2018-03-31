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
// Treenode definitions for CREATE SCHEMA statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/pg_tcreate_schema.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

PgTCreateSchema::PgTCreateSchema(MemoryContext *memctx,
                                 PgTLocation::SharedPtr loc,
                                 const MCSharedPtr<MCString>& name,
                                 bool create_if_not_exists)
    : TreeNode(memctx, loc),
      name_(name),
      create_if_not_exists_(create_if_not_exists) {
}

PgTCreateSchema::~PgTCreateSchema() {
}

CHECKED_STATUS PgTCreateSchema::Analyze(PgCompileContext *compile_context) {
  if (*name_ == common::kRedisKeyspaceName) {
    return compile_context->Error(loc(),
                                  strings::Substitute("$0 is a reserved name",
                                                      common::kRedisKeyspaceName).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
  }
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb

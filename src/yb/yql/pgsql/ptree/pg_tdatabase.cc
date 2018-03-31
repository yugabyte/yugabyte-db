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
// Treenode definitions for CREATE DATABASE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/pg_tdatabase.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

PgTCreateDatabase::PgTCreateDatabase(MemoryContext *memctx,
                                     PgTLocation::SharedPtr loc,
                                     PgTName::SharedPtr name)
    : TreeNode(memctx, loc), name_(name) {
}

PgTCreateDatabase::~PgTCreateDatabase() {
}

CHECKED_STATUS PgTCreateDatabase::Analyze(PgCompileContext *compile_context) {
  RETURN_NOT_OK(name_->Analyze(compile_context));
  if (name_->name() == common::kRedisKeyspaceName) {
    return compile_context->Error(loc(),
                                  strings::Substitute("$0 is a reserved name",
                                                      common::kRedisKeyspaceName).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgTDropDatabase::PgTDropDatabase(MemoryContext *memctx,
                                 PgTLocation::SharedPtr loc,
                                 PgTName::SharedPtr name,
                                 bool drop_if_exists)
    : TreeNode(memctx, loc),
      name_(name),
      drop_if_exists_(drop_if_exists) {
}

PgTDropDatabase::~PgTDropDatabase() {
}

CHECKED_STATUS PgTDropDatabase::Analyze(PgCompileContext *compile_context) {
  RETURN_NOT_OK(name_->Analyze(compile_context));
  if (name_->name() == common::kRedisKeyspaceName) {
    return compile_context->Error(loc(),
                                  strings::Substitute("$0 is a reserved name",
                                                      common::kRedisKeyspaceName).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
  }
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb

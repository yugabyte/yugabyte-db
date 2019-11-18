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
// Treenode definitions for all name nodes.
//--------------------------------------------------------------------------------------------------

#include "yb/common/redis_constants_common.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTName::PTName(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               const MCSharedPtr<MCString>& name)
    : TreeNode(memctx, loc),
      name_(name) {
}

PTName::~PTName() {
}

//--------------------------------------------------------------------------------------------------

PTNameAll::PTNameAll(MemoryContext *memctx, YBLocation::SharedPtr loc)
    : PTName(memctx, loc, MCMakeShared<MCString>(memctx, "*")) {
}

PTNameAll::~PTNameAll() {
}

//--------------------------------------------------------------------------------------------------

PTQualifiedName::PTQualifiedName(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc,
                                 const PTName::SharedPtr& ptname)
    : PTName(memctx, loc),
      ptnames_(memctx) {
  Append(ptname);
}

PTQualifiedName::PTQualifiedName(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc,
                                 const MCSharedPtr<MCString>& name)
    : PTName(memctx, loc),
      ptnames_(memctx) {
  Append(PTName::MakeShared(memctx, loc, name));
}

PTQualifiedName::~PTQualifiedName() {
}

void PTQualifiedName::Append(const PTName::SharedPtr& ptname) {
  ptnames_.push_back(ptname);
}

void PTQualifiedName::Prepend(const PTName::SharedPtr& ptname) {
  ptnames_.push_front(ptname);
}

CHECKED_STATUS PTQualifiedName::Analyze(SemContext *sem_context) {
  // We don't support qualified name yet except for a keyspace.
  // Support only the names like: '<keyspace_name>.<table_name>'.
  if (ptnames_.size() >= 3) {
    return sem_context->Error(this, ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  return Status::OK();
}

CHECKED_STATUS PTQualifiedName::AnalyzeName(SemContext *sem_context, const ObjectType object_type) {
  switch (object_type) {
    case OBJECT_SCHEMA:
      if (ptnames_.size() != 1) {
        return sem_context->Error(this, "Invalid keyspace name", ErrorCode::INVALID_ARGUMENTS);
      }
      if (ptnames_.front()->name() == common::kRedisKeyspaceName) {
        return sem_context->Error(this,
                                  strings::Substitute("$0 is a reserved keyspace name",
                                                      common::kRedisKeyspaceName).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
      }
      return Status::OK();
    case OBJECT_ROLE:
      if (ptnames_.size() != 1) {
        return sem_context->Error(this,
                                  strings::Substitute("Invalid $0 name",
                                                      ObjectTypeName(object_type)).c_str(),
                                  ErrorCode::SQL_STATEMENT_INVALID);
      }
      return Status::OK();
    case OBJECT_TABLE: FALLTHROUGH_INTENDED;
    case OBJECT_TYPE: FALLTHROUGH_INTENDED;
    case OBJECT_INDEX:
      if (ptnames_.size() > 2) {
        return sem_context->Error(this,
                                  strings::Substitute("Invalid $0 name",
                                                      ObjectTypeName(object_type)).c_str(),
                                  ErrorCode::SQL_STATEMENT_INVALID);
      }
      if (ptnames_.size() == 2) {
        auto* create_table_stmt = sem_context->current_create_table_stmt();
        if (object_type == OBJECT_TYPE &&
            create_table_stmt != nullptr &&
            create_table_stmt->yb_table_name().namespace_name() != ptnames_.front()->name()) {
          return sem_context->Error(this,
              "User Defined Types can only be used in the same keyspace where they are defined",
              ErrorCode::INVALID_COLUMN_DEFINITION);
        }
      }

      if (ptnames_.size() == 1) {
        string keyspace_name = sem_context->CurrentKeyspace();
        // For user-defined types we prioritize using the table keyspace if available.
        auto* create_table_stmt = sem_context->current_create_table_stmt();
        if (object_type == OBJECT_TYPE &&
            create_table_stmt != nullptr &&
            create_table_stmt->yb_table_name().has_namespace()) {
          keyspace_name = create_table_stmt->yb_table_name().namespace_name();
        }

        if (keyspace_name.empty()) {
          return sem_context->Error(this, ErrorCode::NO_NAMESPACE_USED);
        }
        MemoryContext* memctx = sem_context->PSemMem();
        Prepend(PTName::MakeShared(memctx, loc_,
                                   MCMakeShared<MCString>(memctx, keyspace_name.c_str())));
      }
      if (ptnames_.front()->name() == common::kRedisKeyspaceName) {
        return sem_context->Error(this,
                                  strings::Substitute("$0 is a reserved keyspace name",
                                                      common::kRedisKeyspaceName).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
      }

      return Status::OK();

    case OBJECT_AGGREGATE: FALLTHROUGH_INTENDED;
    case OBJECT_AMOP: FALLTHROUGH_INTENDED;
    case OBJECT_AMPROC: FALLTHROUGH_INTENDED;
    case OBJECT_ATTRIBUTE: FALLTHROUGH_INTENDED;
    case OBJECT_CAST: FALLTHROUGH_INTENDED;
    case OBJECT_COLUMN: FALLTHROUGH_INTENDED;
    case OBJECT_COLLATION: FALLTHROUGH_INTENDED;
    case OBJECT_CONVERSION: FALLTHROUGH_INTENDED;
    case OBJECT_DATABASE: FALLTHROUGH_INTENDED;
    case OBJECT_DEFAULT: FALLTHROUGH_INTENDED;
    case OBJECT_DEFACL: FALLTHROUGH_INTENDED;
    case OBJECT_DOMAIN: FALLTHROUGH_INTENDED;
    case OBJECT_DOMCONSTRAINT: FALLTHROUGH_INTENDED;
    case OBJECT_EVENT_TRIGGER: FALLTHROUGH_INTENDED;
    case OBJECT_EXTENSION: FALLTHROUGH_INTENDED;
    case OBJECT_FDW: FALLTHROUGH_INTENDED;
    case OBJECT_FOREIGN_SERVER: FALLTHROUGH_INTENDED;
    case OBJECT_FOREIGN_TABLE: FALLTHROUGH_INTENDED;
    case OBJECT_FUNCTION: FALLTHROUGH_INTENDED;
    case OBJECT_LANGUAGE: FALLTHROUGH_INTENDED;
    case OBJECT_LARGEOBJECT: FALLTHROUGH_INTENDED;
    case OBJECT_MATVIEW: FALLTHROUGH_INTENDED;
    case OBJECT_OPCLASS: FALLTHROUGH_INTENDED;
    case OBJECT_OPERATOR: FALLTHROUGH_INTENDED;
    case OBJECT_OPFAMILY: FALLTHROUGH_INTENDED;
    case OBJECT_POLICY: FALLTHROUGH_INTENDED;
    case OBJECT_RULE: FALLTHROUGH_INTENDED;
    case OBJECT_SEQUENCE: FALLTHROUGH_INTENDED;
    case OBJECT_TABCONSTRAINT: FALLTHROUGH_INTENDED;
    case OBJECT_TABLESPACE: FALLTHROUGH_INTENDED;
    case OBJECT_TRANSFORM: FALLTHROUGH_INTENDED;
    case OBJECT_TRIGGER: FALLTHROUGH_INTENDED;
    case OBJECT_TSCONFIGURATION: FALLTHROUGH_INTENDED;
    case OBJECT_TSDICTIONARY: FALLTHROUGH_INTENDED;
    case OBJECT_TSPARSER: FALLTHROUGH_INTENDED;
    case OBJECT_TSTEMPLATE: FALLTHROUGH_INTENDED;
    case OBJECT_USER_MAPPING: FALLTHROUGH_INTENDED;
    case OBJECT_VIEW:
      return sem_context->Error(this, ErrorCode::FEATURE_NOT_SUPPORTED);
  }
  return Status::OK();
}

}  // namespace ql
}  // namespace yb

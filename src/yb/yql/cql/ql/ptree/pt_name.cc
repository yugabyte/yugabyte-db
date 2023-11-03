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

#include "yb/yql/cql/ql/ptree/pt_name.h"

#include "yb/common/redis_constants_common.h"

#include "yb/yql/cql/ql/ptree/pt_create_table.h"
#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

using std::string;

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTName::PTName(MemoryContext *memctx,
               YBLocationPtr loc,
               const MCSharedPtr<MCString>& name)
    : TreeNode(memctx, loc),
      name_(name) {
}

PTName::~PTName() {
}

//--------------------------------------------------------------------------------------------------

PTNameAll::PTNameAll(MemoryContext *memctx, YBLocationPtr loc)
    : PTName(memctx, loc, MCMakeShared<MCString>(memctx, "*")) {
}

PTNameAll::~PTNameAll() {
}

//--------------------------------------------------------------------------------------------------

PTQualifiedName::PTQualifiedName(MemoryContext *memctx,
                                 YBLocationPtr loc,
                                 const PTName::SharedPtr& ptname)
    : PTName(memctx, loc),
      ptnames_(memctx) {
  Append(ptname);
}

PTQualifiedName::PTQualifiedName(MemoryContext *memctx,
                                 YBLocationPtr loc,
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

Status PTQualifiedName::Analyze(SemContext *sem_context) {
  // We don't support qualified name yet except for a keyspace.
  // Support only the names like: '<keyspace_name>.<table_name>'.
  if (ptnames_.size() >= 3) {
    return sem_context->Error(this, ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  return Status::OK();
}

Status PTQualifiedName::AnalyzeName(SemContext *sem_context, const ObjectType object_type) {
  switch (object_type) {
    case ObjectType::SCHEMA:
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
    case ObjectType::ROLE:
      if (ptnames_.size() != 1) {
        return sem_context->Error(this,
                                  strings::Substitute("Invalid $0 name",
                                                      ObjectTypeName(object_type)).c_str(),
                                  ErrorCode::SQL_STATEMENT_INVALID);
      }
      return Status::OK();
    case ObjectType::TABLE: FALLTHROUGH_INTENDED;
    case ObjectType::TYPE: FALLTHROUGH_INTENDED;
    case ObjectType::INDEX:
      if (ptnames_.size() > 2) {
        return sem_context->Error(this,
                                  strings::Substitute("Invalid $0 name",
                                                      ObjectTypeName(object_type)).c_str(),
                                  ErrorCode::SQL_STATEMENT_INVALID);
      }
      if (ptnames_.size() == 2) {
        auto* create_table_stmt = sem_context->current_create_table_stmt();
        if (object_type == ObjectType::TYPE &&
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
        if (object_type == ObjectType::TYPE &&
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

    case ObjectType::AGGREGATE: FALLTHROUGH_INTENDED;
    case ObjectType::AMOP: FALLTHROUGH_INTENDED;
    case ObjectType::AMPROC: FALLTHROUGH_INTENDED;
    case ObjectType::ATTRIBUTE: FALLTHROUGH_INTENDED;
    case ObjectType::CAST: FALLTHROUGH_INTENDED;
    case ObjectType::COLUMN: FALLTHROUGH_INTENDED;
    case ObjectType::COLLATION: FALLTHROUGH_INTENDED;
    case ObjectType::CONVERSION: FALLTHROUGH_INTENDED;
    case ObjectType::DATABASE: FALLTHROUGH_INTENDED;
    case ObjectType::DEFAULT: FALLTHROUGH_INTENDED;
    case ObjectType::DEFACL: FALLTHROUGH_INTENDED;
    case ObjectType::DOMAIN: FALLTHROUGH_INTENDED;
    case ObjectType::DOMCONSTRAINT: FALLTHROUGH_INTENDED;
    case ObjectType::EVENT_TRIGGER: FALLTHROUGH_INTENDED;
    case ObjectType::EXTENSION: FALLTHROUGH_INTENDED;
    case ObjectType::FDW: FALLTHROUGH_INTENDED;
    case ObjectType::FOREIGN_SERVER: FALLTHROUGH_INTENDED;
    case ObjectType::FOREIGN_TABLE: FALLTHROUGH_INTENDED;
    case ObjectType::FUNCTION: FALLTHROUGH_INTENDED;
    case ObjectType::LANGUAGE: FALLTHROUGH_INTENDED;
    case ObjectType::LARGEOBJECT: FALLTHROUGH_INTENDED;
    case ObjectType::MATVIEW: FALLTHROUGH_INTENDED;
    case ObjectType::OPCLASS: FALLTHROUGH_INTENDED;
    case ObjectType::OPERATOR: FALLTHROUGH_INTENDED;
    case ObjectType::OPFAMILY: FALLTHROUGH_INTENDED;
    case ObjectType::POLICY: FALLTHROUGH_INTENDED;
    case ObjectType::RULE: FALLTHROUGH_INTENDED;
    case ObjectType::SEQUENCE: FALLTHROUGH_INTENDED;
    case ObjectType::TABCONSTRAINT: FALLTHROUGH_INTENDED;
    case ObjectType::TABLESPACE: FALLTHROUGH_INTENDED;
    case ObjectType::TRANSFORM: FALLTHROUGH_INTENDED;
    case ObjectType::TRIGGER: FALLTHROUGH_INTENDED;
    case ObjectType::TSCONFIGURATION: FALLTHROUGH_INTENDED;
    case ObjectType::TSDICTIONARY: FALLTHROUGH_INTENDED;
    case ObjectType::TSPARSER: FALLTHROUGH_INTENDED;
    case ObjectType::TSTEMPLATE: FALLTHROUGH_INTENDED;
    case ObjectType::USER_MAPPING: FALLTHROUGH_INTENDED;
    case ObjectType::VIEW:
      return sem_context->Error(this, ErrorCode::FEATURE_NOT_SUPPORTED);
  }
  return Status::OK();
}

}  // namespace ql
}  // namespace yb

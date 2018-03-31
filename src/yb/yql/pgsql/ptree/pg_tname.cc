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

#include "yb/yql/pgsql/ptree/pg_tname.h"

#include <boost/algorithm/string.hpp>

#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

// Postgresql reserved the following name.
// Names of user-defined object including quoted names cannot be one of the reserved name.
static const std::unordered_set<string> kReservedNames = {
  { "oid" },
  { "tableoid" },
  { "xmin" },
  { "cmin" },
  { "xmax" },
  { "cmax" },
  { "ctid" }
};

//--------------------------------------------------------------------------------------------------

PgTName::PgTName(MemoryContext *memctx,
                 PgTLocation::SharedPtr loc,
                 const MCSharedPtr<MCString>& name,
                 ObjectType object_type)
    : TreeNode(memctx, loc),
      name_(name),
      object_type_(object_type) {
}

PgTName::~PgTName() {
}

CHECKED_STATUS PgTName::Analyze(PgCompileContext *compile_context) {
  if (!IsLegalName(name_->c_str())) {
    return compile_context->Error(this, ErrorCode::ILLEGAL_NAME);
  }
  return Status::OK();
}

CHECKED_STATUS PgTName::SetupPrimaryKey(PgCompileContext *compile_context) {
  PgTColumnDefinition *column = compile_context->GetColumnDefinition(*name_);

  // Because we processed all column definitions first, all columns are already in the symbol table.
  if (column == nullptr) {
    return compile_context->Error(this, "Column does not exist", ErrorCode::UNDEFINED_COLUMN);
  }
  if (column->is_primary_key()) {
    return compile_context->Error(this, ErrorCode::DUPLICATE_COLUMN);
  }

  // Add the analyzed column to table. For CREATE INDEX, need to check for proper datatype and set
  // column location because column definition is loaded from the indexed table definition actually.
  PgTCreateTable *table = compile_context->current_create_table_stmt();
  RETURN_NOT_OK(table->AppendPrimaryColumn(compile_context, column));

  return Status::OK();
}

bool PgTName::IsLegalName(const string& user_defined_name) {
  // Names of user-defined object including quoted names cannot be one of the reserved name.
  string hash_name = boost::algorithm::to_lower_copy(user_defined_name);
  auto lookup = kReservedNames.find(hash_name);
  return (lookup == kReservedNames.end());
}

//--------------------------------------------------------------------------------------------------

PgTNameAll::PgTNameAll(MemoryContext *memctx, PgTLocation::SharedPtr loc)
    : PgTName(memctx, loc, MCMakeShared<MCString>(memctx, "*")) {
}

PgTNameAll::~PgTNameAll() {
}

//--------------------------------------------------------------------------------------------------

PgTQualifiedName::PgTQualifiedName(MemoryContext *memctx,
                                   PgTLocation::SharedPtr loc,
                                   const PgTName::SharedPtr& ptname)
    : PgTName(memctx, loc), ptnames_(memctx) {
  Append(ptname);
}

PgTQualifiedName::PgTQualifiedName(MemoryContext *memctx,
                                 PgTLocation::SharedPtr loc,
                                 const MCSharedPtr<MCString>& name)
    : PgTName(memctx, loc), ptnames_(memctx) {
  Append(PgTName::MakeShared(memctx, loc, name));
}

PgTQualifiedName::~PgTQualifiedName() {
}

void PgTQualifiedName::Append(const PgTName::SharedPtr& ptname) {
  ptnames_.push_back(ptname);
}

void PgTQualifiedName::Prepend(const PgTName::SharedPtr& ptname) {
  ptnames_.push_front(ptname);
}

CHECKED_STATUS PgTQualifiedName::Analyze(PgCompileContext *compile_context) {
  // We don't support qualified name yet except for a keyspace.
  // Support only the names like: '<schema_name>.<table_name>'.
  if (ptnames_.size() >= 4) {
    return compile_context->Error(this, ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  // Check if the name is legal. The parser only checks for reserved keyword, but Postgresql has
  // other reserved names that are not keywords.
  for (auto name : ptnames_) {
    RETURN_NOT_OK(name->Analyze(compile_context));
  }

  // TODO(neil) This function was copied from CQL module, but it should be corrected because of
  // different requirements.
  //   Postgresql full name = database.schema.table.column
  //   CQL full name = keyspace.table.column
  switch (object_type_) {
    case OBJECT_DEFAULT:
      // TODO(neil) Each name should be set to the correct object type.
      // Skip the error check for this name.
      return Status::OK();

    case OBJECT_SCHEMA:
      if (ptnames_.size() != 1) {
        return compile_context->Error(this, "Invalid keyspace name", ErrorCode::INVALID_ARGUMENTS);
      }
      if (ptnames_.front()->name() == common::kRedisKeyspaceName) {
        return compile_context->Error(this,
                                      strings::Substitute("$0 is a reserved keyspace name",
                                                          common::kRedisKeyspaceName).c_str(),
                                      ErrorCode::INVALID_ARGUMENTS);
      }
      return Status::OK();

    case OBJECT_TABLE: FALLTHROUGH_INTENDED;
    case OBJECT_TYPE: FALLTHROUGH_INTENDED;
    case OBJECT_INDEX:
      if (ptnames_.size() > 2) {
        return compile_context->Error(this,
                                      strings::Substitute("Invalid $0 name",
                                                          ObjectTypeName(object_type_)).c_str(),
                                      ErrorCode::SQL_STATEMENT_INVALID);
      }
      if (ptnames_.size() == 2) {
        auto* create_table_stmt = compile_context->current_create_table_stmt();
        if (object_type_ == OBJECT_TYPE &&
            create_table_stmt != nullptr &&
            create_table_stmt->yb_table_name().namespace_name() != ptnames_.front()->name()) {
          return compile_context->Error(this,
              "User Defined Types can only be used in the same keyspace where they are defined",
              ErrorCode::INVALID_COLUMN_DEFINITION);
        }
      }

      if (ptnames_.size() == 1) {
        string session_database = compile_context->session_database();
        if (session_database.empty()) {
          return compile_context->Error(this, ErrorCode::NO_NAMESPACE_USED);
        }
        MemoryContext* memctx = compile_context->PSemMem();
        Prepend(PgTName::MakeShared(memctx, loc_,
                                    MCMakeShared<MCString>(memctx, session_database.c_str())));
      }
      if (ptnames_.front()->name() == common::kRedisKeyspaceName) {
        return compile_context->Error(this,
                                      strings::Substitute("$0 is a reserved keyspace name",
                                                          common::kRedisKeyspaceName).c_str(),
                                      ErrorCode::INVALID_ARGUMENTS);
      }
      return Status::OK();

    case OBJECT_COLUMN:
      if (!IsSimpleName()) {
        return compile_context->Error(this, "Qualified name not allowed for column reference",
                                      ErrorCode::SQL_STATEMENT_INVALID);
      }
      return Status::OK();

    case OBJECT_AGGREGATE: FALLTHROUGH_INTENDED;
    case OBJECT_AMOP: FALLTHROUGH_INTENDED;
    case OBJECT_AMPROC: FALLTHROUGH_INTENDED;
    case OBJECT_ATTRIBUTE: FALLTHROUGH_INTENDED;
    case OBJECT_CAST: FALLTHROUGH_INTENDED;
    case OBJECT_COLLATION: FALLTHROUGH_INTENDED;
    case OBJECT_CONVERSION: FALLTHROUGH_INTENDED;
    case OBJECT_DATABASE: FALLTHROUGH_INTENDED;
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
    case OBJECT_ROLE: FALLTHROUGH_INTENDED;
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
      return compile_context->Error(this, ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb

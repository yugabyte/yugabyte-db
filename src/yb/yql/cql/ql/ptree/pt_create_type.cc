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
// Treenode definitions for CREATE TYPE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_create_type.h"

#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

DECLARE_bool(use_cassandra_authentication);

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTTypeField::PTTypeField(MemoryContext *memctx,
                         YBLocation::SharedPtr loc,
                         const MCSharedPtr<MCString>& name,
                         const PTBaseType::SharedPtr& datatype)
    : TreeNode(memctx, loc),
      name_(name),
      datatype_(datatype) {
}

PTTypeField::~PTTypeField() {
}


Status PTTypeField::Analyze(SemContext *sem_context) {

  // Save context state, and set "this" as current type field in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_type_field(this);

  // Add field to symbol table (checks for duplicate entries)
  RETURN_NOT_OK(sem_context->MapSymbol(*name_, this));

  // Check that the type of the field is valid.
  RETURN_NOT_OK(datatype_->Analyze(sem_context));

  if (datatype_->ql_type()->IsCollection()) {
    return sem_context->Error(this, "UDT field types cannot be (un-frozen) collections",
                              ErrorCode::INVALID_TYPE_DEFINITION);
  }

  if (!datatype_->ql_type()->GetUserDefinedTypeIds().empty() && !datatype_->ql_type()->IsFrozen()) {
    return sem_context->Error(this, "A user-defined type cannot contain non-frozen UDTs",
                              ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  // Restore the context value as we are done with this colummn.
  sem_context->set_current_processing_id(cached_entry);

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTCreateType::PTCreateType(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           const PTQualifiedName::SharedPtr& name,
                           const PTTypeFieldListNode::SharedPtr& fields,
                           bool create_if_not_exists)
    : TreeNode(memctx, loc),
      name_(name),
      fields_(fields),
      create_if_not_exists_(create_if_not_exists) {
}

PTCreateType::~PTCreateType() {
}

Status PTCreateType::Analyze(SemContext *sem_context) {
  SemState sem_state(sem_context);

  // Processing type name.
  RETURN_NOT_OK(name_->AnalyzeName(sem_context, ObjectType::TYPE));

  if (FLAGS_use_cassandra_authentication) {
    if (!sem_context->CheckHasAllKeyspacesPermission(loc(),
        PermissionType::CREATE_PERMISSION).ok()) {
      RETURN_NOT_OK(sem_context->CheckHasKeyspacePermission(loc(),
          PermissionType::CREATE_PERMISSION, yb_type_name().namespace_name()));
    }
  }

  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_create_type_stmt(this);

  // Processing statement elements (type fields)
  sem_context->set_current_create_type_stmt(this);
  RETURN_NOT_OK(fields_->Analyze(sem_context));

  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  // Restore the context value as we are done with this colummn.
  sem_context->set_current_processing_id(cached_entry);

  return Status::OK();
}

void PTCreateType::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output("\tType ", sem_context->PTempMem());
  sem_output += yb_type_name().ToString().c_str();
  sem_output += "(";

  bool is_first = true;
  for (auto field : fields_->node_list()) {
    if (is_first) {
      is_first = false;
    } else {
      sem_output += ", ";
    }
    sem_output += field->yb_name();
    sem_output += " <Type = ";
    sem_output += field->ql_type()->ToString().c_str();
    sem_output += ">";
  }

  sem_output += ")";
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
}

}  // namespace ql
}  // namespace yb

//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for CREATE TYPE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_create_type.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

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


CHECKED_STATUS PTTypeField::Analyze(SemContext *sem_context) {

  // Save context state, and set "this" as current type field in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_type_field(this);

  // Add field to symbol table (checks for duplicate entries)
  RETURN_NOT_OK(sem_context->MapSymbol(*name_, this));

  // Check that the type of the field is valid.
  RETURN_NOT_OK(datatype_->Analyze(sem_context));

  if (datatype_->yql_type()->IsCollection()) {
    return sem_context->Error(loc(), ErrorCode::INVALID_TYPE_DEFINITION,
        "UDT field types cannot be (un-frozen) collections");
  }

  if (!datatype_->yql_type()->GetUserDefinedTypeIds().empty()) {
    return sem_context->Error(loc(), ErrorCode::FEATURE_NOT_SUPPORTED,
        "UDT field types cannot refer to other user-defined types");
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

CHECKED_STATUS PTCreateType::Analyze(SemContext *sem_context) {
  SemState sem_state(sem_context);

  // Processing type name.
  RETURN_NOT_OK(name_->Analyze(sem_context));

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
    sem_output += field->yql_type()->ToString().c_str();
    sem_output += ">";
  }

  sem_output += ")";
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
}

}  // namespace sql
}  // namespace yb

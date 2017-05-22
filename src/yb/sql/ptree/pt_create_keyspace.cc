//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for CREATE KEYSPACE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_create_keyspace.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTCreateKeyspace::PTCreateKeyspace(MemoryContext *memctx,
    YBLocation::SharedPtr loc,
    const MCSharedPtr<MCString>& name,
    bool create_if_not_exists,
    const PTKeyspacePropertyListNode::SharedPtr& keyspace_properties)
    : TreeNode(memctx, loc),
      name_(name),
      create_if_not_exists_(create_if_not_exists),
      keyspace_properties_(keyspace_properties) {
}

PTCreateKeyspace::~PTCreateKeyspace() {
}

CHECKED_STATUS PTCreateKeyspace::Analyze(SemContext *sem_context) {
  if (keyspace_properties_ != nullptr) {
    // Process table properties.
    RETURN_NOT_OK(keyspace_properties_->Analyze(sem_context));
  }

  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

void PTCreateKeyspace::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output("\tKeyspace ", sem_context->PTempMem());
  sem_output += name();
  sem_output += (create_if_not_exists()? " IF NOT EXISTS" : "");
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << loc() << "):\n" << sem_output;
}

}  // namespace sql
}  // namespace yb

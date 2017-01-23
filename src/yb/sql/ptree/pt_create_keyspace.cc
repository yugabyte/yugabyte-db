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
                                   const MCString::SharedPtr& name,
                                   bool create_if_not_exists)
    : TreeNode(memctx, loc),
      name_(name),
      create_if_not_exists_(create_if_not_exists) {
}

PTCreateKeyspace::~PTCreateKeyspace() {
}

CHECKED_STATUS PTCreateKeyspace::Analyze(SemContext *sem_context) {
  // DDL statement is not allowed to be retry.
  if (sem_context->retry_count() > 0) {
    return sem_context->Error(loc(), ErrorCode::DDL_EXECUTION_RERUN_NOT_ALLOWED);
  }

  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

void PTCreateKeyspace::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output(sem_context->PTempMem(), "\tKeyspace ");
  sem_output += name();
  sem_output += (create_if_not_exists()? " IF NOT EXISTS" : "");
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << loc() << "):\n" << sem_output;
}

}  // namespace sql
}  // namespace yb

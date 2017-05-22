//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for USE KEYSPACE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_use_keyspace.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTUseKeyspace::PTUseKeyspace(MemoryContext *memctx,
                             YBLocation::SharedPtr loc,
                             const MCSharedPtr<MCString>& name)
    : TreeNode(memctx, loc),
      name_(name) {
}

PTUseKeyspace::~PTUseKeyspace() {
}

CHECKED_STATUS PTUseKeyspace::Analyze(SemContext *sem_context) {
  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

void PTUseKeyspace::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output("\tKeyspace ", sem_context->PTempMem());
  sem_output += name();
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << loc() << "):\n" << sem_output;
}

}  // namespace sql
}  // namespace yb

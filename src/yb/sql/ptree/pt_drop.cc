//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for DROP statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_drop.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

PTDropStmt::PTDropStmt(MemoryContext *memctx,
                       YBLocation::SharedPtr loc,
                       ObjectType drop_type,
                       PTQualifiedNameListNode::SharedPtr names,
                       bool drop_if_exists)
    : TreeNode(memctx, loc),
      drop_type_(drop_type),
      names_(names),
      drop_if_exists_(drop_if_exists) {
}

PTDropStmt::~PTDropStmt() {
}

CHECKED_STATUS PTDropStmt::Analyze(SemContext *sem_context) {
  if (names_->size() > 1) {
    return sem_context->Error(names_->loc(),
                              "Only one object name is allowed in a drop statement",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  // Processing object name.
  RETURN_NOT_OK(names_->element(0)->Analyze(sem_context));
  return Status::OK();
}

void PTDropStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output("\t", sem_context->PTempMem());

  switch (drop_type()) {
    case OBJECT_TABLE: sem_output += "Table "; break;
    case OBJECT_SCHEMA: sem_output += "Keyspace "; break;
    case OBJECT_TYPE: sem_output += "Type "; break;

    default: sem_output += "UNKNOWN OBJECT ";
  }

  sem_output += name()->last_name();
  sem_output += (drop_if_exists()? " IF EXISTS" : "");
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
}

}  // namespace sql
}  // namespace yb

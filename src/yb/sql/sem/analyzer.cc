//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/sem/analyzer.h"
#include "yb/util/logging.h"

namespace yb {
namespace sql {

using std::string;

//--------------------------------------------------------------------------------------------------

Analyzer::Analyzer() {
}

Analyzer::~Analyzer() {
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Analyzer::Analyze(const string& sql_stmt,
                                 ParseTree::UniPtr parse_tree,
                                 SqlEnv *sql_env) {
  ParseTree *ptree = parse_tree.get();
  DCHECK(ptree != nullptr) << "Parse tree is null";
  sem_context_ = SemContext::UniPtr(new SemContext(sql_stmt.c_str(),
                                                   sql_stmt.length(),
                                                   std::move(parse_tree),
                                                   sql_env));

  if (!ptree->Analyze(sem_context_.get()).ok()) {
    // Before leaving the semantic step, collect all errors and place them in return status.
    VLOG(3) << "Failed to analyze parse-tree <" << ptree << ">";
    return sem_context_->GetStatus();
  }

  VLOG(3) << "Successfully analyzed parse-tree <" << ptree << ">";
  return Status::OK();
}

ParseTree::UniPtr Analyzer::Done() {
  // When releasing the parse tree, we must free the context because it has references to the tree
  // which doesn't belong to this context any longer.
  ParseTree::UniPtr ptree = sem_context_->AcquireParseTree();
  sem_context_ = nullptr;
  return ptree;
}

}  // namespace sql
}  // namespace yb

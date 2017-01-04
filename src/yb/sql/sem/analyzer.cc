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
  sem_context_ = nullptr;
}

//--------------------------------------------------------------------------------------------------

ErrorCode Analyzer::Analyze(const string& sql_stmt,
                            ParseTree::UniPtr parse_tree,
                            SqlEnv *sql_env,
                            int retry_count) {
  ParseTree *ptree = parse_tree.get();
  DCHECK(ptree != nullptr) << "Parse tree is null";
  sem_context_ = SemContext::UniPtr(new SemContext(sql_stmt.c_str(),
                                                   sql_stmt.length(),
                                                   move(parse_tree),
                                                   sql_env,
                                                   retry_count));
  if (ptree->Analyze(sem_context_.get()) == ErrorCode::SUCCESSFUL_COMPLETION) {
    VLOG(3) << "Successfully analyzed parse-tree <" << ptree << ">";
  } else {
    VLOG(3) << "Failed to analyze parse-tree <" << ptree << ">";
  }
  return sem_context_->error_code();
}

ParseTree::UniPtr Analyzer::Done() {
  ParseTree::UniPtr ptree = sem_context_->AcquireParseTree();
  sem_context_ = nullptr;
  return ptree;
}

}  // namespace sql
}  // namespace yb

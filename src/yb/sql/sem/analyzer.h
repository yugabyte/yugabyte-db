//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the semantic analytical process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_SEM_ANALYZER_H_
#define YB_SQL_SEM_ANALYZER_H_

#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

class Analyzer {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<Analyzer> UniPtr;
  typedef std::unique_ptr<const Analyzer> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  Analyzer();
  virtual ~Analyzer();

  // Run semantics analysis on the given parse tree and decorate it with semantics information such
  // as datatype or object-type of a database object.
  CHECKED_STATUS Analyze(const std::string& sql_stmt,
                         ParseTree::UniPtr ptree,
                         SqlEnv *sql_env,
                         bool refresh_cache);

  // Return if metadata cache is used during semantic analysis.
  bool cache_used() const { return sem_context_->cache_used(); }

  // Returns decorated parse tree from the semantic analysis.
  ParseTree::UniPtr Done();

  // Access to error code.
  ErrorCode error_code() {
    return sem_context_->error_code();
  }

 private:
  SemContext::UniPtr sem_context_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_SEM_ANALYZER_H_

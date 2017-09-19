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
// Entry point for the semantic analytical process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_QL_SEM_ANALYZER_H_
#define YB_QL_SEM_ANALYZER_H_

#include "yb/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

class Analyzer {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<Analyzer> UniPtr;
  typedef std::unique_ptr<const Analyzer> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  explicit Analyzer(QLEnv *ql_env);
  virtual ~Analyzer();

  // Run semantics analysis on the given parse tree and decorate it with semantics information such
  // as datatype or object-type of a database object.
  CHECKED_STATUS Analyze(const std::string& ql_stmt, ParseTree::UniPtr ptree);

  // Returns decorated parse tree from the semantic analysis and destroys the context.
  ParseTree::UniPtr Done();

  // Return if metadata cache is used during semantic analysis.
  bool cache_used() const {
    return sem_context_->cache_used();
  }

 private:
  // Environment (YBClient) for analyzing statements.
  QLEnv *ql_env_;

  SemContext::UniPtr sem_context_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_QL_SEM_ANALYZER_H_

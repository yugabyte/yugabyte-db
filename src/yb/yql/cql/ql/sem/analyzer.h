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

#pragma once

#include "yb/yql/cql/ql/ptree/ptree_fwd.h"
#include "yb/yql/cql/ql/util/util_fwd.h"

#include "yb/util/status_fwd.h"

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
  Status Analyze(ParseTreePtr ptree);

  // Returns decorated parse tree from the semantic analysis and destroys the context.
  ParseTreePtr Done();

  // Return if metadata cache is used during semantic analysis.
  bool cache_used() const;

 private:
  // Environment (YBClient) for analyzing statements.
  QLEnv *ql_env_;

  std::unique_ptr<SemContext> sem_context_;
};

}  // namespace ql
}  // namespace yb

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

#ifndef YB_YQL_PGSQL_SEM_PG_ANALYZER_H_
#define YB_YQL_PGSQL_SEM_PG_ANALYZER_H_

#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

class PgAnalyzer {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<PgAnalyzer> UniPtr;
  typedef std::unique_ptr<const PgAnalyzer> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  PgAnalyzer();
  virtual ~PgAnalyzer();

  // Run semantics analysis on the given parse tree and decorate it with semantics information such
  // as datatype or object-type of a database object.
  CHECKED_STATUS Analyze(const PgCompileContext::SharedPtr& compile_context);
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_SEM_PG_ANALYZER_H_

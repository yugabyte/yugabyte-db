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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/sem/pg_analyzer.h"
#include "yb/util/logging.h"

namespace yb {
namespace pgsql {

using std::string;

//--------------------------------------------------------------------------------------------------

PgAnalyzer::PgAnalyzer() {
}

PgAnalyzer::~PgAnalyzer() {
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgAnalyzer::Analyze(const PgCompileContext::SharedPtr& compile_context) {
  ParseTree::SharedPtr ptree = compile_context->parse_tree();

  DCHECK(ptree != nullptr) << "Parse tree is null";
  RETURN_NOT_OK(ptree->Analyze(compile_context.get()));

  VLOG(3) << "Successfully analyzed parse-tree <" << ptree << ">";
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb

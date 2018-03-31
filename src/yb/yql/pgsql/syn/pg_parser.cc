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

#include <cstring>

#include "yb/yql/pgsql/syn/pg_parser.h"
#include "yb/util/logging.h"

namespace yb {
namespace pgsql {

using std::endl;
using std::shared_ptr;
using std::string;
using std::to_string;

//--------------------------------------------------------------------------------------------------
// Class PgParser.
//--------------------------------------------------------------------------------------------------
PgParser::PgParser() : lex_processor_(), gram_processor_(this) {
}

PgParser::~PgParser() {
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgParser::Parse(const PgCompileContext::SharedPtr& ctx) {
  compile_context_ = ctx;
  lex_processor_.ScanInit(compile_context());
  gram_processor_.set_debug_level(compile_context_->trace_parsing());

  if (gram_processor_.parse() == 0 &&
      compile_context_->error_code() == ErrorCode::SUCCESS) {
    VLOG(3) << "Successfully parsed statement \"" << compile_context_->stmt()
            << "\". Result = <" << compile_context_->parse_tree() << ">" << endl;
  } else {
    VLOG(3) << kErrorFontStart << "Failed to parse \"" << compile_context_->stmt() << "\""
            << kErrorFontEnd << endl;
  }

  return compile_context_->GetStatus();
}

//--------------------------------------------------------------------------------------------------

GramProcessor::symbol_type PgParser::Scan() {
  return lex_processor_.Scan();
}

}  // namespace pgsql
}  // namespace yb

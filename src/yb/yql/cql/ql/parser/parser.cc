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
#include "yb/yql/cql/ql/parser/parser.h"

namespace yb {
namespace ql {

using std::endl;
using std::string;

//--------------------------------------------------------------------------------------------------
// Class Parser.
//--------------------------------------------------------------------------------------------------
Parser::Parser()
    : lex_processor_(),
      gram_processor_(this) {
}

Parser::~Parser() {
}

//--------------------------------------------------------------------------------------------------

Status Parser::Parse(const string& stmt, const bool reparsed, const MemTrackerPtr& mem_tracker,
                     const bool internal) {
  parse_context_ = ParseContext::UniPtr(new ParseContext(stmt, reparsed, mem_tracker, internal));
  lex_processor_.ScanInit(parse_context());
  gram_processor_.set_debug_level(parse_context_->trace_parsing());

  if (gram_processor_.parse() == 0 &&
      parse_context_->error_code() == ErrorCode::SUCCESS) {
    VLOG(3) << "Successfully parsed statement \"" << parse_context_->stmt()
            << "\". Result = <" << parse_context_->parse_tree() << ">" << endl;
  } else {
    VLOG(3) << kErrorFontStart << "Failed to parse \"" << parse_context_->stmt() << "\""
            << kErrorFontEnd << endl;
  }

  return parse_context_->GetStatus();
}

//--------------------------------------------------------------------------------------------------

ParseTree::UniPtr Parser::Done() {
  // When releasing the parse tree, we must free the context because it has references to the tree
  // which doesn't belong to this context any longer.
  ParseTree::UniPtr ptree = parse_context_->AcquireParseTree();
  parse_context_ = nullptr;
  return ptree;
}

//--------------------------------------------------------------------------------------------------

GramProcessor::symbol_type Parser::Scan() {
  return lex_processor_.Scan();
}

}  // namespace ql
}  // namespace yb

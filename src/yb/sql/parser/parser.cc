//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <cstring>

#include "yb/sql/parser/parser.h"
#include "yb/util/logging.h"

namespace yb {
namespace sql {

using std::endl;
using std::shared_ptr;
using std::string;
using std::to_string;

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

CHECKED_STATUS Parser::Parse(const string& sql_stmt, shared_ptr<MemTracker> mem_tracker) {
  parse_context_ = ParseContext::UniPtr(new ParseContext(sql_stmt.c_str(),
                                                         sql_stmt.length(),
                                                         mem_tracker));
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

}  // namespace sql
}  // namespace yb

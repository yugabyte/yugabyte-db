//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <cstring>

#include "yb/sql/errcodes.h"
#include "yb/sql/parser/parser.h"
#include "yb/util/logging.h"

namespace yb {
namespace sql {

using std::to_string;
using std::endl;

//--------------------------------------------------------------------------------------------------
// Class Parser.
//--------------------------------------------------------------------------------------------------
Parser::Parser()
    : lex_processor_(),
      gram_processor_(this) {

  // TODO(neil) Complete this prototype.
}

Parser::~Parser() {
  // TODO(neil) Complete this prototype.
}

//--------------------------------------------------------------------------------------------------

int Parser::Parse(ParseContext *parse_context) {
  LOG(INFO) << "Parsing statement: " << parse_context->stmt();

  parse_context_ = parse_context;
  lex_processor_.ScanInit(parse_context);
  gram_processor_.set_debug_level(parse_context_->trace_parsing());
  const int error_code = gram_processor_.parse();

  // TODO(neil) Redefine enum ERRCODE to class and use it properly.
  if (error_code == ERRCODE_SUCCESSFUL_COMPLETION) {
    LOG(INFO) << "Compiled successfully";
  } else {
    LOG(ERROR) << "Failed to compile. Errorcode = " << error_code;
  }
  return error_code;
}

//--------------------------------------------------------------------------------------------------

GramProcessor::symbol_type Parser::Scan() {
  return lex_processor_.Scan();
}

//--------------------------------------------------------------------------------------------------

void Parser::Error(const location& l, const std::string& m) {
  LOG(ERROR) << "\033[31m" << l << ": " << m << "\033[0m" << endl;
}

void Parser::Error(const std::string& m) {
  LOG(ERROR) << "\033[31m" << m << "\033[0m" << endl;
}

}  // namespace sql.
}  // namespace yb.

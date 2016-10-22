//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <stdio.h>

#include "yb/sql/parser/parser.h"
#include "yb/sql/parser/parse_context.h"
#include "yb/util/logging.h"

namespace yb {
namespace sql {

using std::endl;
using std::istream;
using std::min;
using std::string;

//--------------------------------------------------------------------------------------------------
// ParseContext
//--------------------------------------------------------------------------------------------------

ParseContext::ParseContext(const string& sql_stmt)
    : parse_tree_(new ParseTree()),
      ptemp_mem_(new MemoryContext()),
      error_code_(ErrorCode::SUCCESSFUL_COMPLETION),
      stmt_(MCStrdup(ptemp_mem_.get(), sql_stmt.c_str())),
      stmt_len_(sql_stmt.size()),
      stmt_offset_(0),
      trace_scanning_(false),
      trace_parsing_(false) {

  // The MAC version of FLEX requires empty or valid input stream. It does not allow input file to
  // be nullptr.
  sql_file_ = std::unique_ptr<istream>(new istream(nullptr));

  if (VLOG_IS_ON(3)) {
    trace_scanning_ = true;
    trace_parsing_ = true;
  }
}

ParseContext::~ParseContext() {
}

//--------------------------------------------------------------------------------------------------

size_t ParseContext::Read(char* buf, size_t max_size) {
  const size_t copy_size = min<size_t>(stmt_len_ - stmt_offset_, max_size);
  if (copy_size > 0) {
    memcpy(buf, stmt_ + stmt_offset_, copy_size);
    stmt_offset_ += copy_size;
    return copy_size;
  }
  return 0;
}

//--------------------------------------------------------------------------------------------------

void ParseContext::SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree) {
  parse_tree_->set_root(generated_parse_tree);
}

//--------------------------------------------------------------------------------------------------

void ParseContext::Warn(const location& l, const string& m, ErrorCode error_code) {
  error_code_ = error_code;
  LOG(WARNING) << kErrorFontStart << "SQL Warning (" << l << "): " << m << kErrorFontEnd << endl;
}

//--------------------------------------------------------------------------------------------------

void ParseContext::ScanError(const location& l, const std::string& token) {
  LOG(ERROR) << "SQL Error (" << l << "): Lexical error at or near " << token;
}

//--------------------------------------------------------------------------------------------------

void ParseContext::Error(const location& l,
                         const string& m,
                         ErrorCode error_code,
                         const char* token) {
  error_code_ = error_code;
  if (token == nullptr) {
    string token;
    const pair<const char *, const size_t> token_bounds = ReadToken(l);
    if (token_bounds.first != nullptr) {
      token.assign(token_bounds.first, token_bounds.second);
    }
    LOG(ERROR) << kErrorFontStart << "SQL Error (" << l << "): "
               << ErrorText(error_code) << ". " << m << kErrorFontEnd << endl
               << "\t" << kErrorFontStart << token << kErrorFontEnd
               << token_bounds.first + token_bounds.second << endl;
  } else {
    LOG(ERROR) << kErrorFontStart << "SQL Error (" << l << "): " << m << kErrorFontEnd << endl
               << "\t<< " << kErrorFontStart << token << kErrorFontEnd << " >>" << endl;
  }
}

void ParseContext::Error(const location& l, const string& m, const char* token) {
  Error(l, m, ErrorCode::SYNTAX_ERROR, token);
}

void ParseContext::Error(const location& l, ErrorCode error_code, const char* token) {
  Error(l, "", error_code, token);
}

void ParseContext::Error(const string& m) {
  error_code_ = ErrorCode::SYNTAX_ERROR;
  LOG(ERROR) << kErrorFontStart << "SQL Error: " << m << kErrorFontEnd << endl;
}

//--------------------------------------------------------------------------------------------------

const pair<const char *, const size_t> ParseContext::ReadToken(const location& l) {
  const position& err_begin = l.begin;
  const position& err_end = l.end;

  // Find the start of erroneous token.
  int line = 1;
  char *line_start = stmt_;
  while (line < err_begin.line) {
    line_start = strchr(line_start, '\n');
    if (line_start == nullptr) {
      return make_pair(nullptr, 0);
    }
    line_start++;
    line++;
  }
  char *start = line_start + err_begin.column - 1;
  if (start > stmt_ + stmt_len_) {
    return make_pair(nullptr, 0);
  }
  while (isspace(*start)) {
    start++;
  }

  // Find the end of erroneous token.
  while (line < err_end.line) {
    line_start = strchr(line_start, '\n');
    if (line_start == nullptr) {
      return make_pair(nullptr, 0);
    }
    line_start++;
    line++;
  }
  const char *end = line_start + err_end.column - 1;
  if (end > stmt_ + stmt_len_) {
    return make_pair(nullptr, 0);
  }
  while (end > start && isspace(*(end-1))) {
    end--;
  }

  return make_pair(start, end - start);
}

}  // namespace sql
}  // namespace yb

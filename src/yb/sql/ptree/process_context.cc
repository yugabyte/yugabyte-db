//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/process_context.h"
#include "yb/util/logging.h"

namespace yb {
namespace sql {

using std::endl;
using std::istream;
using std::min;
using std::string;

//--------------------------------------------------------------------------------------------------
// ProcessContext
//--------------------------------------------------------------------------------------------------

ProcessContext::ProcessContext(const char *stmt,
                               size_t stmt_len,
                               ParseTree::UniPtr parse_tree)
    : stmt_(stmt),
      stmt_len_(stmt_len),
      parse_tree_(move(parse_tree)),
      ptemp_mem_(new MemoryContext()),
      error_code_(ErrorCode::SUCCESSFUL_COMPLETION) {

  if (parse_tree_ == nullptr) {
    parse_tree_ = ParseTree::UniPtr(new ParseTree());
  }
}

ProcessContext::~ProcessContext() {
}

//--------------------------------------------------------------------------------------------------

void ProcessContext::SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree) {
  CHECK(parse_tree_.get() != nullptr) << "Context is already associated with a parse tree";
  parse_tree_->set_root(generated_parse_tree);
}

//--------------------------------------------------------------------------------------------------

void ProcessContext::Warn(const YBLocation& l, const string& m, ErrorCode error_code) {
  error_code_ = error_code;
  LOG(WARNING) << kErrorFontStart << "SQL Warning (" << l << "): " << m << kErrorFontEnd << endl;
}

//--------------------------------------------------------------------------------------------------

void ProcessContext::Error(const YBLocation& l,
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

void ProcessContext::Error(const YBLocation& l, const string& m, const char* token) {
  Error(l, m, ErrorCode::SYNTAX_ERROR, token);
}

void ProcessContext::Error(const YBLocation& l, ErrorCode error_code, const char* token) {
  Error(l, "", error_code, token);
}

void ProcessContext::Error(const string& m) {
  error_code_ = ErrorCode::SYNTAX_ERROR;
  LOG(ERROR) << kErrorFontStart << "SQL Error: " << m << kErrorFontEnd << endl;
}

//--------------------------------------------------------------------------------------------------

const pair<const char *, const size_t> ProcessContext::ReadToken(const YBLocation& l) {
  const int err_begin_line = l.BeginLine();
  const int err_begin_column = l.BeginColumn();
  const int err_end_line = l.EndLine();
  const int err_end_column = l.EndColumn();

  // Find the start of erroneous token.
  int line = 1;
  const char *line_start = stmt_;
  while (line < err_begin_line) {
    line_start = strchr(line_start, '\n');
    if (line_start == nullptr) {
      return make_pair(nullptr, 0);
    }
    line_start++;
    line++;
  }
  const char *start = line_start + err_begin_column - 1;
  if (start > stmt_ + stmt_len_) {
    return make_pair(nullptr, 0);
  }
  while (isspace(*start)) {
    start++;
  }

  // Find the end of erroneous token.
  while (line < err_end_line) {
    line_start = strchr(line_start, '\n');
    if (line_start == nullptr) {
      return make_pair(nullptr, 0);
    }
    line_start++;
    line++;
  }
  const char *end = line_start + err_end_column - 1;
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

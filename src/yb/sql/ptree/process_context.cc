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
// ProcessContextBase
//--------------------------------------------------------------------------------------------------

ProcessContextBase::ProcessContextBase(const char *stmt, size_t stmt_len)
    : stmt_(stmt),
      stmt_len_(stmt_len),
      error_code_(ErrorCode::SUCCESS) {
}

ProcessContextBase::~ProcessContextBase() {
}

//--------------------------------------------------------------------------------------------------

MCString* ProcessContextBase::error_msgs() {
  if (error_msgs_ == nullptr) {
    error_msgs_.reset(new MCString(PTempMem()));
  }
  return error_msgs_.get();
}

CHECKED_STATUS ProcessContextBase::GetStatus() {
  // Erroneous index is negative while successful index is non-negative.
  if (error_code_ < ErrorCode::SUCCESS) {
    return STATUS(SqlError, error_msgs()->c_str(), Slice(), static_cast<int64_t>(error_code_));
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

void ProcessContextBase::Warn(const YBLocation& l, const string& m, ErrorCode error_code) {
  error_code_ = error_code;
  LOG(WARNING) << kErrorFontStart << "SQL Warning (" << l << "): " << m << kErrorFontEnd << endl;
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS ProcessContextBase::Error(const YBLocation& l,
                                         const char *m,
                                         ErrorCode error_code,
                                         const char* token) {
  error_code_ = error_code;

  // Form an error message.
  MCString msg(PTempMem());

  // Start with location.
  msg += "SQL Error (";
  l.ToString<MCString>(&msg);
  msg += "): ";

  // Concatenate error code.
  msg += ErrorText(error_code);
  msg += " - ";

  // Concatenate error message.
  msg += m;

  // Concantenate token.
  msg += "\n\t";
  if (token == nullptr) {
    const pair<const char *, const size_t> token_bounds = ReadToken(l);
    if (token_bounds.first != nullptr) {
      msg += token_bounds.first;
      msg += "\n\t";

      // Concantenate pointer to erroneous token.
      msg.append(token_bounds.second, '^');
    }
  } else {
    msg += token;
  }
  msg += "\n";

  // Append this error message to the context.
  error_msgs()->append(msg);
  VLOG(3) << msg;
  return STATUS(SqlError, msg.c_str(), Slice(), static_cast<int64_t>(error_code_));
}

CHECKED_STATUS ProcessContextBase::Error(const YBLocation& l, const char *m, const char* token) {
  return Error(l, m, ErrorCode::SQL_STATEMENT_INVALID, token);
}

CHECKED_STATUS ProcessContextBase::Error(const YBLocation& l,
                                         ErrorCode error_code,
                                         const char* token) {
  return Error(l, "", error_code, token);
}

//--------------------------------------------------------------------------------------------------

const pair<const char *, const size_t> ProcessContextBase::ReadToken(const YBLocation& l) {
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

//--------------------------------------------------------------------------------------------------
// ProcessContext
//--------------------------------------------------------------------------------------------------

ProcessContext::ProcessContext(const char *stmt,
                               size_t stmt_len,
                               ParseTree::UniPtr parse_tree)
    : ProcessContextBase(stmt, stmt_len),
      parse_tree_(move(parse_tree)) {
}

ProcessContext::~ProcessContext() {
}

//--------------------------------------------------------------------------------------------------

void ProcessContext::SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree) {
  CHECK(parse_tree_.get() != nullptr) << "Context is not associated with a parse tree";
  parse_tree_->set_root(generated_parse_tree);
}

}  // namespace sql
}  // namespace yb

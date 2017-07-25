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

  // Concatenate error code.
  msg += ErrorText(error_code);

  if (strcmp(m, "") != 0) {
    msg += " - ";
    // Concatenate error message.
    msg += m;
  }

  // Concantenate token.
  msg += "\n";
  if (token == nullptr) {
    const int err_begin_line = l.BeginLine();
    const int err_begin_column = l.BeginColumn();
    const int err_end_line = l.EndLine();
    const int err_end_column = l.EndColumn();

    int curr_line = 1;
    int curr_col = 1;
    const char *curr_char = stmt_;
    bool wrote_token = false;

    while (curr_char <= stmt_ + stmt_len_) {
      if (curr_char == stmt_ + stmt_len_ || *curr_char == '\n') { // end of line
        msg += '\n';

        // If in error-token range, try writing line marking error location with '^'
        if (curr_line >= err_begin_line && curr_line <= err_end_line) {
          const char *line_start = curr_char - curr_col + 1;
          const char *line_end = curr_char - 1;

          // finding token start - left-trim spaces
          const char *start = line_start;
          if (curr_line == err_begin_line) {
            start += err_begin_column - 1;
          }
          if (!wrote_token) {
            while (start < line_end && isspace(*start)) {
              start++;
            }
          }

          // finding token end - right-trim spaces
          const char *end = line_end;
          if (curr_line == err_end_line) {
            end = line_start + err_end_column - 1;
            while (end > start && isspace(*end - 1)) {
              end--;
            }
          }

          // if found a valid token range write a marker line
          if (end > start) {
            msg.append(start - line_start, ' ');
            msg.append(end - start, '^');
            msg += '\n';
            wrote_token = true;
          }
        }

        curr_line++;
        curr_col = 1;
      } else {
        msg += *curr_char;
        curr_col++;
      }
      curr_char++;
    }

    // In rare cases Bison reports the wrong error location which may contain only spaces and be
    // skipped entirely because of the trimming done above.
    // In that case we append the reported location directly
    if (!wrote_token) {
      msg += "At location: (";
      l.ToString<MCString>(&msg, false /* starting_location_only */);
      msg += ")\n";
    }
  } else {
    msg += token;
  }

  // Append this error message to the context.
  error_msgs()->append(msg);
  LOG(ERROR) << "SQL Error: " << msg;
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

//--------------------------------------------------------------------------------------------------
// ProcessContext
//--------------------------------------------------------------------------------------------------

ProcessContext::ProcessContext(const char *stmt,
                               size_t stmt_len,
                               ParseTree::UniPtr parse_tree)
    : ProcessContextBase(stmt, stmt_len),
      parse_tree_(std::move(parse_tree)) {
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

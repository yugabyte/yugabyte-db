//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the parsing process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PARSER_PARSE_CONTEXT_H_
#define YB_SQL_PARSER_PARSE_CONTEXT_H_

#include "yb/sql/parser/location.h"
#include "yb/sql/ptree/process_context.h"

namespace yb {
namespace sql {

// Parsing context.
class ParseContext : public ProcessContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ParseContext> UniPtr;
  typedef std::unique_ptr<const ParseContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  explicit ParseContext(const char *stmt = "",
                        size_t stmt_len = 0,
                        std::shared_ptr<MemTracker> mem_tracker = nullptr);
  virtual ~ParseContext();

  // Read a maximum of 'max_size' bytes from SQL statement of this parsing context into the
  // provided buffer 'buf'. Scanner will call this function when looking for next token.
  size_t Read(char* buf, size_t max_size);

  // Handling parsing warning.
  void Warn(const location& l, const char *m, ErrorCode error_code) {
    ProcessContext::Warn(Location(l), m, error_code);
  }

  // Handling parsing error.
  CHECKED_STATUS Error(const location& l,
                       const char *m,
                       ErrorCode error_code,
                       const char* token = nullptr) {
    return ProcessContext::Error(Location(l), m, error_code, token);
  }
  CHECKED_STATUS Error(const location& l, const char *m, const char* token = nullptr) {
    return ProcessContext::Error(Location(l), m, token);
  }
  CHECKED_STATUS Error(const location& l, ErrorCode error_code, const char* token = nullptr) {
    return ProcessContext::Error(Location(l), error_code, token);
  }

  // Access function for sql_file_.
  std::istream *sql_file() {
    return sql_file_ == nullptr ? nullptr : sql_file_.get();
  }

  // Access function for trace_scanning_.
  bool trace_scanning() const {
    return trace_scanning_;
  }

  // Access function for trace_parsing_.
  bool trace_parsing() const {
    return trace_parsing_;
  }

 private:
  //------------------------------------------------------------------------------------------------
  // We don't use istream (i.e. file) as input when parsing. In the future, if we also support file
  // as an SQL input, we need to define a constructor that takes a file as input and initializes
  // "sql_file_" accordingly.
  std::unique_ptr<std::istream> sql_file_;

  //------------------------------------------------------------------------------------------------
  // NOTE: All entities below this line in this modules are copies of PostgreSql's code. We made
  // some minor changes to avoid lint errors such as using '{' for if blocks, change the comment
  // style from '/**/' to '//', and post-fix data members with "_".
  //------------------------------------------------------------------------------------------------
  size_t stmt_offset_;         // SQL statement to be scanned.
  bool trace_scanning_;        // Scanner trace flag.
  bool trace_parsing_;         // Parser trace flag.
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PARSER_PARSE_CONTEXT_H_

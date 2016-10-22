//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the parsing process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PARSER_PARSE_CONTEXT_H_
#define YB_SQL_PARSER_PARSE_CONTEXT_H_

#include <cstddef>
#include <iostream>
#include <memory>

#include "yb/sql/parser/location.hh"
#include "yb/sql/ptree/parse_tree.h"
#include "yb/sql/util/errcodes.h"
#include "yb/sql/util/memory_context.h"

namespace yb {
namespace sql {

// Parsing context.
class ParseContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ParseContext> UniPtr;
  typedef std::unique_ptr<const ParseContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  explicit ParseContext(const std::string& sql_stmt);
  virtual ~ParseContext();

  // Read a maximum of 'max_size' bytes from SQL statement of this parsing context into the
  // provided buffer 'buf'. Scanner will call this function when looking for next token.
  size_t Read(char* buf, size_t max_size);

  // Saves the generated parse tree from the parsing process to this context.
  void SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree);

  // Handling parsing warning.
  void Warn(const location& l, const std::string& m, ErrorCode error_code);

  // Handling scanning error.
  void ScanError(const location& l, const std::string& token);

  // Handling parsing error.
  void Error(const location& l,
             const std::string& m,
             ErrorCode error_code,
             const char* token = nullptr);
  void Error(const location& l, const std::string& m, const char* token = nullptr);
  void Error(const location& l, ErrorCode error_code, const char* token = nullptr);
  void Error(const std::string& m);

  // Returns the token at location 'l' of the input SQL statement stmt_.
  const pair<const char *, const size_t> ReadToken(const location& l);

  // Returns the generated parse tree and release the ownership from this context.
  ParseTree::UniPtr AcquireParseTree() {
    return move(parse_tree_);
  }

  // Memory pool for constructing the parse tree of a statement.
  MemoryContext *PTreeMem() const {
    return parse_tree_->PTreeMem();
  }

  // Memory pool for allocating and deallocating operating memory spaces during parsing process.
  MemoryContext *PTempMem() const {
    return ptemp_mem_.get();
  }

  // Access function for trace_scanning_.
  bool trace_scanning() const {
    return trace_scanning_;
  }

  // Access function for trace_parsing_.
  bool trace_parsing() const {
    return trace_parsing_;
  }

  // Read and write access functions for error_code_.
  ErrorCode error_code() const {
    return error_code_;
  }

  void set_error_code(ErrorCode error_code) {
    error_code_ = error_code;
  }

  //------------------------------------------------------------------------------------------------
  // NOTE: The below functions are a translation from PostgreSql's code who uses char* buffer
  // instead of string. We can reuse the same buffer over and over again to parse all statements
  // one at a time.
  //------------------------------------------------------------------------------------------------

  // Access function for stmt_.
  char *stmt() const {
    return stmt_;
  }

  // Access function for stmt_len_.
  size_t stmt_len() const {
    return stmt_len_;
  }

  // Access function for sql_file_.
  std::istream *sql_file() {
    return sql_file_ == nullptr ? nullptr : sql_file_.get();
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Generated parse tree (output).
  ParseTree::UniPtr parse_tree_;

  // Temporary memory pool is used during the parsing process. This pool is deleted as soon as the
  // parsing process is completed.
  MemoryContext::UniPtr ptemp_mem_;

  // Latest parsing or scanning error code.
  ErrorCode error_code_;

  // We don't use istream (i.e. file) as input when parsing. In the future, if we also support file
  // as an SQL input, we need to define a constructor that takes a file as input and initializes
  // "sql_file_" accordingly.
  std::unique_ptr<std::istream> sql_file_;

  //------------------------------------------------------------------------------------------------
  // NOTE: All entities below this line in this modules are copies of PostgreSql's code. We made
  // some minor changes to avoid lint errors such as using '{' for if blocks, change the comment
  // style from '/**/' to '//', and post-fix data members with "_".
  //------------------------------------------------------------------------------------------------
  char *stmt_;                 // SQL statement to be scanned.
  const size_t stmt_len_;      // SQL statement length.
  size_t stmt_offset_;         // SQL statement to be scanned.
  bool trace_scanning_;        // Scanner trace flag.
  bool trace_parsing_;         // Parser trace flag.
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PARSER_PARSE_CONTEXT_H_

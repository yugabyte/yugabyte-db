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

#include "yb/sql/util/memory_context.h"
#include "yb/sql/parser/parse_tree.h"

namespace yb {
namespace sql {

// Parsing context.
class ParseContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<ParseContext> SharedPtr;
  typedef std::shared_ptr<const ParseContext> SharedPtrConst;

  typedef std::unique_ptr<ParseContext> UniPtr;
  typedef std::unique_ptr<const ParseContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  ParseContext(const std::string& sql_stmt,
               MemoryContext *parse_mem,
               MemoryContext *ptree_mem);
  virtual ~ParseContext();

  // Read a maximum of 'max_size' bytes from SQL statement of this parsing context into the
  // provided buffer 'buf'. Scanner will call this function when looking for next token.
  size_t Read(char* buf, size_t max_size);

  // Memory pool for constructing the parse tree of a statement.
  MemoryContext *PTreeMem() const {
    return ptree_mem_;
  }

  // Memory pool for allocating and deallocating operating memory spaces during parsing process.
  MemoryContext *ParseMem() const {
    return parse_mem_;
  }

  // Access function for trace_scanning_.
  bool trace_scanning() const {
    return trace_scanning_;
  }

  // Access function for trace_parsing_.
  bool trace_parsing() const {
    return trace_parsing_;
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
  ParseTree *parse_tree_;      // Generated parse tree (output).
  MemoryContext *parse_mem_;   // MemPool for spaces used during scanning and parsing.
  MemoryContext *ptree_mem_;   // Memory pool for the resulted parse tree.

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

}  // namespace sql.
}  // namespace yb.

#endif  // YB_SQL_PARSER_PARSE_CONTEXT_H_

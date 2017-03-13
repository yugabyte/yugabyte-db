//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the parsing process. Conducting the whole scanning and parsing of SQL statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PARSER_PARSER_H_
#define YB_SQL_PARSER_PARSER_H_

#include <cstddef>

#include "yb/sql/parser/parse_context.h"
#include "yb/sql/parser/scanner.h"
#include "yb/sql/util/errcodes.h"
#include "yb/sql/util/memory_context.h"

namespace yb {
namespace sql {

class Parser {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<Parser> UniPtr;
  typedef std::unique_ptr<const Parser> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  Parser();
  virtual ~Parser();

  // Returns 0 if Bison successfully parses SQL statements, and the compiler can continue on to
  // semantic analysis. Otherwise, it returns one of the errcodes that are defined in file
  // "yb/sql/errcodes.h", and the caller (YbSql API) should stop the compiling process.
  CHECKED_STATUS Parse(const std::string& sql_stmt,
                       std::shared_ptr<MemTracker> mem_tracker = nullptr);

  // Returns the generated parse tree.
  ParseTree::UniPtr Done();

  // Entry function to scan the next token. Bison parser will call this function.
  GramProcessor::symbol_type Scan();

  // Saves the generated parse tree from the parsing process.
  void SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree) {
    parse_context_->SaveGeneratedParseTree(generated_parse_tree);
  }

  // Access function for parse_context_.
  ParseContext *parse_context() const {
    return parse_context_.get();
  }

  // Converts a char* to MCString.
  MCString::SharedPtr MakeString(const char *str) {
    return MCString::MakeShared(PTreeMem(), str);
  }

  // Memory pool for allocating and deallocating operating memory spaces during parsing process.
  MemoryContext *PTempMem() const {
    return parse_context_->PTempMem();
  }

  // Memory pool for constructing the parse tree of a statement. This context will have the same
  // lifetime as the parse tree.
  MemoryContext *PTreeMem() const {
    return parse_context_->PTreeMem();
  }

  // Raise parsing error.
  void Error(const location& l, ErrorCode error_code) {
    // Bison parser will raise exception, so we don't return Status::Error here.
    Status s = parse_context_->Error(l, error_code);
    VLOG(3) << s.ToString();
  }

  void Error(const location& l, const char *m, ErrorCode error_code) {
    // Bison parser will raise exception, so we don't return Status::Error here.
    Status s = parse_context_->Error(l, m, error_code);
    VLOG(3) << s.ToString();
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Parse context which consists of state variables and results.
  // NOTE: parse context must be FIRST class field to be destroyed by the class destructor
  //       after all other dependent class fields (e.g. processors below).
  ParseContext::UniPtr parse_context_;

  // Lexical scanner.
  LexProcessor lex_processor_;

  // Grammar parser.
  GramProcessor gram_processor_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PARSER_PARSER_H_

//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the parsing process. Conducting the whole scanning and parsing of SQL statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PARSER_PARSER_H_
#define YB_SQL_PARSER_PARSER_H_

#include <cstddef>

#include "yb/sql/util/memory_context.h"
#include "yb/sql/parser/parse_context.h"
#include "yb/sql/parser/scanner.h"

namespace yb {
namespace sql {

class Parser {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<Parser> SharedPtr;
  typedef std::shared_ptr<const Parser> SharedPtrConst;

  typedef std::unique_ptr<Parser> UniPtr;
  typedef std::unique_ptr<const Parser> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  Parser();
  virtual ~Parser();

  // Returns 0 if Bison successfully parses SQL statements, and the compiler can continue on to
  // semantic analysis. Otherwise, it returns one of the errcodes that are defined in file
  // "yb/sql/errcodes.h", and the caller (YbSql API) should stop the compiling process.
  int Parse(ParseContext *parse_context);

  // Entry function to scan the next token. Bison parser will call this function.
  GramProcessor::symbol_type Scan();

  // Memory pool for allocating and deallocating operating memory spaces during parsing process.
  MemoryContext *ParseMem() const {
    return parse_context_->ParseMem();
  }

  // Memory pool for constructing the parse tree of a statement.
  MemoryContext *PTreeMem() const {
    return parse_context_->PTreeMem();
  }

  // Error handling.
  void Error(const location& l, const std::string& m);
  void Error(const std::string& m);

 private:
  //------------------------------------------------------------------------------------------------
  // Lexical scanner.
  LexProcessor lex_processor_;

  // Grammar parser.
  GramProcessor gram_processor_;

  // Parse context which consists of state variables and results.
  ParseContext *parse_context_;
};

}  // namespace sql.
}  // namespace yb.

#endif  // YB_SQL_PARSER_PARSER_H_

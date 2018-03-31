//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
// Entry point for the parsing process. Conducting the whole scanning and parsing of SQL statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_SYN_PG_PARSER_H_
#define YB_YQL_PGSQL_SYN_PG_PARSER_H_

#include <cstddef>

#include "yb/yql/pgsql/ptree/pg_compile_context.h"
#include "yb/yql/pgsql/syn/pg_scanner.h"
#include "yb/yql/pgsql/syn/pg_location.h"
#include "yb/yql/pgsql/util/pg_errcodes.h"

#include "yb/util/memory/arena.h"

namespace yb {
namespace pgsql {

class PgParser {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<PgParser> UniPtr;
  typedef std::unique_ptr<const PgParser> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  PgParser();
  virtual ~PgParser();

  // Returns 0 if Bison successfully parses SQL statements, and the compiler can continue on to
  // semantic analysis. Otherwise, it returns one of the errcodes that are defined in file
  // "yb/yql/pgsql/errcodes.h", and the caller (QL API) should stop the compiling process.
  CHECKED_STATUS Parse(const PgCompileContext::SharedPtr& compile_ctx);

  // Entry function to scan the next token. Bison parser will call this function.
  GramProcessor::symbol_type Scan();

  // Saves the generated parse tree from the parsing process.
  void SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree) {
    compile_context_->SaveGeneratedParseTree(generated_parse_tree);
  }

  // Access function for compile_context_.
  PgCompileContext *compile_context() const {
    return compile_context_.get();
  }

  // Converts a char* to MCString.
  MCSharedPtr<MCString> MakeString(const char *str) {
    return MCMakeShared<MCString>(PTreeMem(), str);
  }

  // Memory pool for allocating and deallocating operating memory spaces during parsing process.
  MemoryContext *PTempMem() const {
    return compile_context_->PTempMem();
  }

  // Memory pool for constructing the parse tree of a statement. This context will have the same
  // lifetime as the parse tree.
  MemoryContext *PTreeMem() const {
    return compile_context_->PTreeMem();
  }

  //------------------------------------------------------------------------------------------------
  // Report warning.
  void Warn(const location& l, const char *m, ErrorCode error_code) {
    compile_context_->Warn(Location(l), m, error_code);
  }

  // Raise parsing error.
  void Error(const location& l, const char *m, const char* token = nullptr) {
    Status s = compile_context_->Error(Location(l), m, token);
    VLOG(3) << s.ToString();
  }

  void Error(const location& l, ErrorCode error_code, const char* token = nullptr) {
    // Bison parser will raise exception, so we don't return Status::Error here.
    Status s = compile_context_->Error(Location(l), error_code, token);
    VLOG(3) << s.ToString();
  }

  void Error(const location& l, const char *m, ErrorCode error_code, const char* token = nullptr) {
    // Bison parser will raise exception, so we don't return Status::Error here.
    Status s = compile_context_->Error(Location(l), m, error_code, token);
    VLOG(3) << s.ToString();
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Compile context consists of state variables and results.
  // NOTE: compile context must be FIRST class field to be destroyed by the class destructor
  //       after all other dependent class fields (e.g. processors below).
  PgCompileContext::SharedPtr compile_context_;

  // Lexical scanner.
  LexProcessor lex_processor_;

  // Grammar parser.
  GramProcessor gram_processor_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_SYN_PG_PARSER_H_

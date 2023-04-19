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

#pragma once

#include <cstddef>

#include "yb/yql/cql/ql/parser/parse_context.h"
#include "yb/yql/cql/ql/parser/scanner.h"
#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/util/memory/arena.h"

namespace yb {
namespace ql {

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
  // "yb/yql/cql/ql/errcodes.h", and the caller (QL API) should stop the compiling process.
  Status Parse(const std::string& stmt, bool reparsed,
               const MemTrackerPtr& mem_tracker = nullptr, const bool internal = false);

  // Returns the generated parse tree.
  ParseTree::UniPtr Done();

  // Entry function to scan the next token. Bison parser will call this function.
  GramProcessor::symbol_type Scan();

  // Saves the generated parse tree from the parsing process.
  void SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree) {
    parse_context_->SaveGeneratedParseTree(generated_parse_tree);
  }

  // Add a bind variable.
  void AddBindVariable(PTBindVar *var) {
    parse_context_->AddBindVariable(var);
  }

  // Set the list of bind variables found during parsing in a DML statement.
  void SetBindVariables(PTDmlStmt *stmt) {
    parse_context_->GetBindVariables(&stmt->bind_variables());
  }

  // Access function for parse_context_.
  ParseContext *parse_context() const {
    return parse_context_.get();
  }

  // Converts a char* to MCString.
  MCSharedPtr<MCString> MakeString(const char *str) {
    return MCMakeShared<MCString>(PTreeMem(), str);
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
  void Error(const location& loc, ErrorCode error_code) {
    // Bison parser will raise exception, so we don't return Status::Error here.
    Status s = parse_context_->Error(loc, error_code);
    VLOG(3) << s.ToString();
  }

  void Error(const location& loc, const char *msg, ErrorCode error_code) {
    // Bison parser will raise exception, so we don't return Status::Error here.
    Status s = parse_context_->Error(loc, msg, error_code);
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

}  // namespace ql
}  // namespace yb

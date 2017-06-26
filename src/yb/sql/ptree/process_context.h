//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This generic context is used for all processes on parse tree such as parsing, semantics analysis,
// and code generation.
//
// The execution step operates on a read-only (const) parse tree and does not hold a unique_ptr to
// it. Accordingly, the execution context subclasses from ProcessContextBase which does not have
// the parse tree.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PROCESS_CONTEXT_H_
#define YB_SQL_PTREE_PROCESS_CONTEXT_H_

#include "yb/sql/ptree/parse_tree.h"
#include "yb/sql/ptree/yb_location.h"
#include "yb/sql/util/errcodes.h"
#include "yb/util/memory/mc_types.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

class ProcessContextBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ProcessContextBase> UniPtr;
  typedef std::unique_ptr<const ProcessContextBase> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  ProcessContextBase(const char *stmt, size_t stmt_len);
  virtual ~ProcessContextBase();

  // Handling parsing warning.
  void Warn(const YBLocation& l, const std::string& m, ErrorCode error_code);

  // Handling parsing error.
  CHECKED_STATUS Error(const YBLocation& l,
                       const char *m,
                       ErrorCode error_code,
                       const char* token = nullptr);
  CHECKED_STATUS Error(const YBLocation& l, const char *m, const char* token = nullptr);
  CHECKED_STATUS Error(const YBLocation& l, ErrorCode error_code, const char* token = nullptr);


  // Memory pool for allocating and deallocating operating memory spaces during a process.
  MemoryContext *PTempMem() const {
    if (ptemp_mem_ == nullptr) {
      ptemp_mem_.reset(new Arena());
    }
    return ptemp_mem_.get();
  }

  // Access function for stmt_.
  const char *stmt() const {
    return stmt_;
  }

  // Access function for stmt_len_.
  size_t stmt_len() const {
    return stmt_len_;
  }

  // Read and write access functions for error_code_.
  ErrorCode error_code() const {
    return error_code_;
  }
  void set_error_code(ErrorCode error_code) {
    error_code_ = error_code;
  }

  // Return status of a process.
  CHECKED_STATUS GetStatus();

 protected:
  MCString* error_msgs();

  //------------------------------------------------------------------------------------------------
  // SQL statement to be scanned.
  const char *stmt_;

  // SQL statement length.
  const size_t stmt_len_;

  // Temporary memory pool is used during a process. This pool is deleted as soon as the process is
  // completed.
  //
  // For performance, the temp arena and the error message that depends on it are created only when
  // needed.
  mutable std::unique_ptr<Arena> ptemp_mem_;

  // Latest parsing or scanning error code.
  ErrorCode error_code_;

  // Error messages. All reported error messages will be concatenated to the end.
  std::unique_ptr<MCString> error_msgs_;
};

//--------------------------------------------------------------------------------------------------

class ProcessContext : public ProcessContextBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ProcessContext> UniPtr;
  typedef std::unique_ptr<const ProcessContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  ProcessContext(const char *stmt, size_t stmt_len, ParseTree::UniPtr parse_tree);
  virtual ~ProcessContext();

  // Saves the generated parse tree from the parsing process to this context.
  void SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree);

  // Returns the generated parse tree and release the ownership from this context.
  ParseTree::UniPtr AcquireParseTree() {
    return move(parse_tree_);
  }

  ParseTree *parse_tree() {
    return parse_tree_.get();
  }

  // Memory pool for constructing the parse tree of a statement.
  MemoryContext *PTreeMem() const {
    return parse_tree_->PTreeMem();
  }

 protected:
  //------------------------------------------------------------------------------------------------
  // Generated parse tree (output).
  ParseTree::UniPtr parse_tree_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PROCESS_CONTEXT_H_

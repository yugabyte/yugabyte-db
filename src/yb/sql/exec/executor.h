//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the execution process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_EXEC_EXECUTOR_H_
#define YB_SQL_EXEC_EXECUTOR_H_

#include "yb/sql/exec/exec_context.h"
#include "yb/sql/ptree/pt_create_table.h"
#include "yb/sql/ptree/pt_select.h"
#include "yb/sql/ptree/pt_insert.h"
#include "yb/sql/ptree/pt_delete.h"
#include "yb/sql/ptree/pt_update.h"

namespace yb {
namespace sql {

class Executor {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<Executor> UniPtr;
  typedef std::unique_ptr<const Executor> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  Executor();
  virtual ~Executor();

  // Execute the given parse tree.
  ErrorCode Execute(const std::string& sql_stmt,
                    ParseTree::UniPtr ptree,
                    SessionContext *session_context);

  // Complete execution and release the parse tree from the process.
  ParseTree::UniPtr Done();

  //------------------------------------------------------------------------------------------------
  // Currently, we don't yet have code generator into byte code, so the following ExecTNode()
  // functions are operating directly on the parse tree.
  ErrorCode ExecPTree(const ParseTree *ptree);

  // Execute any TreeNode. This function determines how to execute a node.
  ErrorCode ExecTreeNode(const TreeNode *tnode);

  // Returns unsupported error for generic tree node execution. We only get to this overloaded
  // function if the execution of a specific treenode is not yet supported or defined.
  ErrorCode ExecPTNode(const TreeNode *tnode);

  // Runs the execution on all of the entries in the list node.
  ErrorCode ExecPTNode(const PTListNode *tnode);

  // Creates table.
  ErrorCode ExecPTNode(const PTCreateTable *tnode);

  // Insert statement.
  ErrorCode ExecPTNode(const PTInsertStmt *tnode);

 private:
  // Execution context which are created and destroyed for each execution.
  ExecContext::UniPtr exec_context_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXECUTOR_H_

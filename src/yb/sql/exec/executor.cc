//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"

namespace yb {
namespace sql {

using std::string;
using std::shared_ptr;

using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTable;
using client::YBTableCreator;
using client::YBTableType;

//--------------------------------------------------------------------------------------------------

Executor::Executor() {
}

Executor::~Executor() {
  exec_context_ = nullptr;
}

//--------------------------------------------------------------------------------------------------

ErrorCode Executor::Execute(const string& sql_stmt,
                            ParseTree::UniPtr parse_tree,
                            SessionContext *session_context) {
  ParseTree *ptree = parse_tree.get();
  exec_context_ = ExecContext::UniPtr(new ExecContext(sql_stmt.c_str(),
                                                      sql_stmt.length(),
                                                      move(parse_tree),
                                                      session_context));
  if (ExecPTree(ptree) == ErrorCode::SUCCESSFUL_COMPLETION) {
    VLOG(3) << "Successfully executed parse-tree <" << ptree << ">";
  } else {
    VLOG(3) << "Failed to execute parse-tree <" << ptree << ">";
  }
  return exec_context_->error_code();
}

ParseTree::UniPtr Executor::Done() {
  ParseTree::UniPtr ptree = exec_context_->AcquireParseTree();
  exec_context_ = nullptr;
  return ptree;
}

//--------------------------------------------------------------------------------------------------

ErrorCode Executor::ExecPTree(const ParseTree *ptree) {
  return ExecTreeNode(ptree->root().get());
}

ErrorCode Executor::ExecTreeNode(const TreeNode *tnode) {
  switch (tnode->opcode()) {
    case TreeNodeOpcode::kPTListNode:
      return ExecPTNode(static_cast<const PTListNode*>(tnode));

    case TreeNodeOpcode::kPTCreateTable:
      return ExecPTNode(static_cast<const PTCreateTable*>(tnode));

    case TreeNodeOpcode::kPTInsertStmt:
      return ExecPTNode(static_cast<const PTInsertStmt*>(tnode));

    default:
      return ExecPTNode(tnode);
  }
}

//--------------------------------------------------------------------------------------------------

ErrorCode Executor::ExecPTNode(const TreeNode *tnode) {
  exec_context_->Error(tnode->loc(), ErrorCode::FEATURE_NOT_SUPPORTED);
  return ErrorCode::FEATURE_NOT_SUPPORTED;
}

//--------------------------------------------------------------------------------------------------

ErrorCode Executor::ExecPTNode(const PTListNode *lnode) {
  ErrorCode errcode = ErrorCode::SUCCESSFUL_COMPLETION;

  for (TreeNode::SharedPtr nodeptr : lnode->node_list()) {
    errcode = ExecTreeNode(nodeptr.get());
    if (errcode != ErrorCode::SUCCESSFUL_COMPLETION) {
      break;
    }
  }
  return errcode;
}

//--------------------------------------------------------------------------------------------------

ErrorCode Executor::ExecPTNode(const PTCreateTable *tnode) {
  const char *table_name = tnode->yb_table_name();

  // Setting up columns.
  Status exec_status;
  YBSchema schema;
  YBSchemaBuilder b;
  const MCList<PTColumnDefinition *>& hash_columns = tnode->hash_columns();
  for (auto column : hash_columns) {
    b.AddColumn(column->yb_name())->Type(column->yb_data_type())->NotNull()->HashPrimaryKey();
  }
  const MCList<PTColumnDefinition *>& primary_columns = tnode->primary_columns();
  for (auto column : primary_columns) {
    b.AddColumn(column->yb_name())->Type(column->yb_data_type())->NotNull()->PrimaryKey();
  }
  const MCList<PTColumnDefinition *>& columns = tnode->columns();
  for (auto column : columns) {
    b.AddColumn(column->yb_name())->Type(column->yb_data_type());
  }
  exec_status = b.Build(&schema);
  if (!exec_status.ok()) {
    exec_context_->Error(tnode->columns_loc(), ErrorCode::INVALID_TABLE_DEFINITION);
    WARN_NOT_OK(exec_status, "SQL EXEC");
    return ErrorCode::INVALID_TABLE_DEFINITION;
  }

  // Create table.
  // TODO(neil): Number of replica should be automatically computed by the master, but it hasn't.
  // We passed '1' for now. Once server is fixed, num_replicas should be removed here.
  shared_ptr<YBTableCreator> table_creator(exec_context_->NewTableCreator());
  exec_status = table_creator->table_name(table_name).table_type(YBTableType::YSQL_TABLE_TYPE)
                                                     .schema(&schema)
                                                     .num_replicas(1)
                                                     .Create();
  if (!exec_status.ok()) {
    exec_context_->Error(tnode->name_loc(), ErrorCode::INVALID_TABLE_DEFINITION);
    WARN_NOT_OK(exec_status, "SQL EXEC");
    return ErrorCode::INVALID_TABLE_DEFINITION;
  }
  return ErrorCode::SUCCESSFUL_COMPLETION;
}

//--------------------------------------------------------------------------------------------------

ErrorCode Executor::ExecPTNode(const PTInsertStmt *tnode) {
  // Generate protobuf and send it.
  return ErrorCode::SUCCESSFUL_COMPLETION;
}

}  // namespace sql
}  // namespace yb

//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/statement.h"

#include "yb/sql/sql_processor.h"

namespace yb {
namespace sql {

using std::list;
using std::shared_ptr;
using std::string;
using std::unique_ptr;

Statement::Statement(const string& keyspace, const string& text)
    : keyspace_(keyspace), text_(text) {
}

Statement::~Statement() {
}

CHECKED_STATUS Statement::Prepare(
    SqlProcessor *processor, shared_ptr<MemTracker> mem_tracker, PreparedResult::UniPtr *result) {
  // Prepare the statement (parse and semantically analysis). Do so within an exclusive lock.
  {
    boost::lock_guard<boost::shared_mutex> l(lock_);
    if (parse_tree_ == nullptr) {

      ParseTree::UniPtr parse_tree;
      bool reparse = false;
      RETURN_NOT_OK(processor->Parse(text_, &parse_tree, mem_tracker));
      RETURN_NOT_OK(processor->Analyze(text_, &parse_tree, &reparse));
      if (reparse) {
        RETURN_NOT_OK(processor->Parse(text_, &parse_tree, mem_tracker));
        RETURN_NOT_OK(processor->Analyze(text_, &parse_tree));
      }
      parse_tree_ = std::move(parse_tree);
    }
  }

  // Return prepared result if requested and the statement is a SELECT statement. Do not need a
  // lock here because we have verified that the parse tree is either present already or we have
  // successfully prepared the statement above. The parse tree is guaranteed read-only afterwards.
  if (result != nullptr) {
    const TreeNode *root = parse_tree_->root().get();
    if (root->opcode() != TreeNodeOpcode::kPTListNode) {
      return STATUS(Corruption, "Internal error: statement list expected");
    }
    const PTListNode *stmts = static_cast<const PTListNode*>(root);
    if (stmts->size() != 1) {
      return STATUS(Corruption, "Internal error: only one statement expected");
    }
    const TreeNode *stmt = stmts->element(0).get();
    if (stmt->opcode() == TreeNodeOpcode::kPTSelectStmt ||
        stmt->opcode() == TreeNodeOpcode::kPTInsertStmt ||
        stmt->opcode() == TreeNodeOpcode::kPTUpdateStmt ||
        stmt->opcode() == TreeNodeOpcode::kPTDeleteStmt) {
      result->reset(new PreparedResult(static_cast<const PTDmlStmt*>(stmt)));
    }
  }

  return Status::OK();
}

bool Statement::ExecuteAsync(
    SqlProcessor* processor, const StatementParameters& params, StatementExecutedCallback cb) {
  {
    // Ensure the statement has been prepared successfully and parse tree is present. Do so within
    // a shared lock. Once verified, we do not need the lock when executing the statement below
    // because the parse-tree is guaranteed read-only after having been prepared successfully.
    boost::shared_lock<boost::shared_mutex> l(lock_);

    // Return false if there is no parse tree.
    if (parse_tree_ == nullptr) {
      return false;
    }
  }

  processor->ExecuteAsync(text_, *parse_tree_.get(), params, cb);
  return true;
}

}  // namespace sql
}  // namespace yb

//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/statement.h"

#include "yb/sql/sql_processor.h"

namespace yb {
namespace sql {

using std::shared_ptr;
using std::string;
using std::unique_ptr;

const MonoTime Statement::kNoLastPrepareTime = MonoTime::Min();

Statement::Statement(const string& keyspace, const string& text)
    : keyspace_(keyspace), text_(text), prepare_time_(kNoLastPrepareTime) {
}

Statement::~Statement() {
}

CHECKED_STATUS Statement::Prepare(SqlProcessor *processor,
                                  const MonoTime& last_prepare_time,
                                  bool refresh_cache,
                                  shared_ptr<MemTracker> mem_tracker,
                                  PreparedResult::UniPtr *result) {
  // Prepare the statement or reprepare if it hasn't been since last_prepare_time. Do so within an
  // exclusive lock.
  {
    boost::lock_guard<boost::shared_mutex> l(lock_);
    if (prepare_time_.Equals(last_prepare_time)) {

      // Parse the statement if the parse tree hasn't been generated (not parsed) yet.
      if (parse_tree_.get() == nullptr) {
        RETURN_NOT_OK(processor->Parse(text_, &parse_tree_, mem_tracker));
      }

      // Analyze the statement (or re-analyze with new metadata).
      RETURN_NOT_OK(processor->Analyze(text_, &parse_tree_, refresh_cache));

      // Update parse time.
      prepare_time_ = MonoTime::Now(MonoTime::FINE);
    }
  }

  // Return prepared result if requested and the statement is a SELECT statement. Do so within a
  // shared lock.
  {
    boost::shared_lock<boost::shared_mutex> l(lock_);
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
  }

  return Status::OK();
}

CHECKED_STATUS Statement::Execute(SqlProcessor *processor,
                                  const StatementParameters& params,
                                  ExecuteResult::UniPtr *result) {
  MonoTime last_prepare_time = kNoLastPrepareTime;
  bool new_analysis_needed = false;
  Status s;

  // Execute the statement.
  s = DoExecute(processor, params, &last_prepare_time, &new_analysis_needed, result);

  // If new analysis is needed, reprepare the statement with new metadata and re-execute.
  if (new_analysis_needed) {
    RETURN_NOT_OK(Prepare(
        processor, last_prepare_time, true /* refresh_cache */, nullptr /* mem_tracker */,
        nullptr /* result */));
    s = DoExecute(processor, params, &last_prepare_time, &new_analysis_needed, result);
  }

  return s;
}

CHECKED_STATUS Statement::DoExecute(SqlProcessor *processor,
                                    const StatementParameters& params,
                                    MonoTime *last_prepare_time,
                                    bool *new_analysis_needed,
                                    ExecuteResult::UniPtr *result) {
  // Save the last prepare time and execute the parse tree. Do so within a shared lock.
  boost::shared_lock<boost::shared_mutex> l(lock_);
  if (parse_tree_ == nullptr) {
    // CQLProcessor should have ensured the statement has been parsed and analyzed before
    // attempting execution.
    return STATUS(Corruption, "Internal error: null parse tree");
  }
  *last_prepare_time = prepare_time_;
  return processor->Execute(text_, *parse_tree_.get(), params, new_analysis_needed, result);
}


CHECKED_STATUS Statement::Run(SqlProcessor *processor,
                              const StatementParameters& params,
                              ExecuteResult::UniPtr *result) {
  RETURN_NOT_OK(Prepare(processor));
  RETURN_NOT_OK(Execute(processor, params, result));
  return Status::OK();
}

} // namespace sql
} // namespace yb

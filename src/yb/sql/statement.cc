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

// Runs the StatementExecutedCallback cb and returns if the status s is not OK.
#define CB_RETURN_NOT_OK(cb, s)    \
  do {                             \
    ::yb::Status _s = (s);         \
    if (PREDICT_FALSE(!_s.ok())) { \
      (cb).Run(_s, nullptr);       \
      return;                      \
    }                              \
  } while (0)

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
      // Clear last prepare time to reinitialize the statement to unprepared state. Otherwise, if
      // the statement needs to be reprepared and the semantic analysis fails, the statement may be
      // treated as prepared when accessed next time, but semantic analysis info has been purged.
      prepare_time_ = kNoLastPrepareTime;

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

void Statement::ExecuteAsync(
    SqlProcessor* processor, const StatementParameters& params, StatementExecutedCallback cb) {
  // Execute the statement.
  DoExecuteAsync(processor, params, kNoLastPrepareTime,
                 Bind(&Statement::ExecuteAsyncDone, Unretained(this),
                      Request(processor, &params), cb));
}

void RedoExecuteAsyncDone(
    StatementExecutedCallback cb, const MonoTime& ignored1, bool ignored2,
    const Status& s, ExecutedResult::SharedPtr result) {
  cb.Run(s, result);
}

void Statement::ExecuteAsyncDone(
    Request req, StatementExecutedCallback cb, const MonoTime &updated_last_prepare_time,
    bool new_analysis_needed, const Status &s, ExecutedResult::SharedPtr result) {
  // If new analysis is needed, reprepare the statement with new metadata and re-execute.
  if (new_analysis_needed) {
    CB_RETURN_NOT_OK(cb,
                     Prepare(
                         req.processor, updated_last_prepare_time, true /* refresh_cache */,
                         nullptr /* mem_tracker */, nullptr /* result */));
    // Re-execute the statement.
    DoExecuteAsync(req.processor, *req.params, updated_last_prepare_time,
                   Bind(&RedoExecuteAsyncDone, cb));
  } else {
    cb.Run(s, result);
  }
}

void DoExecuteAsyncDone(
    Callback<void(const MonoTime&, bool, const Status&, ExecutedResult::SharedPtr)> cb,
    const MonoTime& last_prepare_time, bool new_analysis_needed, const Status& s,
    ExecutedResult::SharedPtr result) {
  cb.Run(last_prepare_time, new_analysis_needed, s, result);
}

void Statement::DoExecuteAsync(SqlProcessor* processor,
                               const StatementParameters& params,
                               const MonoTime &last_prepare_time,
                               Callback<void(const MonoTime &last_prepare_time,
                                             bool new_analysis_needed,
                                             const Status &s,
                                             ExecutedResult::SharedPtr result)> cb) {
  // Save the last prepare time and execute the parse tree. Do so within a shared lock until
  // SqlProcessor::ExecuteAsync() returns by when we are done with the parse tree and the execute
  // request has been queued.
  boost::shared_lock<boost::shared_mutex> l(lock_);
  // CQLProcessor should have ensured the statement has been parsed and analyzed before
  // attempting execution.
  CHECK(parse_tree_ != nullptr) << "Internal error: null parse tree";
  processor->ExecuteAsync(text_, *parse_tree_.get(), params,
                          Bind(&DoExecuteAsyncDone, cb, prepare_time_));
}

void Statement::RunAsync(
    SqlProcessor* processor, const StatementParameters& params, StatementExecutedCallback cb) {
  CB_RETURN_NOT_OK(cb, Prepare(processor));
  ExecuteAsync(processor, params, cb);
}

}  // namespace sql
}  // namespace yb

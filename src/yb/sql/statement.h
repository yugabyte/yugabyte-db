//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This class represents a SQL statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_STATEMENT_H_
#define YB_SQL_STATEMENT_H_

#include <boost/thread/shared_mutex.hpp>

#include "yb/common/yql_protocol.pb.h"
#include "yb/sql/ptree/parse_tree.h"

#include "yb/common/yql_value.h"

namespace yb {
namespace sql {

class SqlProcessor;
class PreparedResult;

// This class represents the parameters for executing a SQL statement.
class StatementParameters {
 public:
  StatementParameters();
  StatementParameters(StatementParameters&& other);

  // Accessors
  uint64_t page_size() const { return page_size_; }
  void set_page_size(const uint64_t page_size) { page_size_ = page_size; }

  const YQLPagingStatePB& paging_state() const { return paging_state_pb_; }
  CHECKED_STATUS set_paging_state(const std::string& paging_state) {
    return paging_state_pb_.ParseFromString(paging_state) ?
        Status::OK() : STATUS(Corruption, "invalid paging state");
  }

  const std::string& next_row_key_to_read() const {
    return paging_state_pb_.next_row_key_to_read();
  }
  const std::string& table_id() const { return paging_state_pb_.table_id(); }
  const std::string& next_partition_key() const { return paging_state_pb_.next_partition_key(); }
  int64_t total_num_rows_read() const { return paging_state_pb_.total_num_rows_read(); }

  // Retrieve a bind variable for the execution of the statement.
  virtual CHECKED_STATUS GetBindVariable(
      const std::string& name, int64_t pos, DataType type, YQLValue* value) const {
    return STATUS(RuntimeError, "no bind variable available");
  }

 private:
  // Limit of the number of rows to return set as page size.
  uint64_t page_size_;

  // Paging State.
  YQLPagingStatePB paging_state_pb_;
};

class Statement {
 public:
  //------------------------------------------------------------------------------------------------
  explicit Statement(const std::string& keyspace, const std::string& text);

  // Returns statement text.
  const std::string& text() const { return text_; }

  // Prepare the statement for execution. Reprepare it if it hasn't been since last_prepare_time.
  // Use MonoTime::Min() if it doesn't need to be reprepared. Optionally return prepared result
  // if requested.
  CHECKED_STATUS Prepare(SqlProcessor* processor,
                         const MonoTime& last_prepare_time = MonoTime::Min(),
                         bool refresh_cache = false,
                         std::shared_ptr<MemTracker> mem_tracker = nullptr,
                         std::unique_ptr<PreparedResult>* prepared_result = nullptr);

  // Execute the prepared statement.
  CHECKED_STATUS Execute(SqlProcessor* processor, const StatementParameters& params);

  // Run the statement (i.e. prepare and execute).
  CHECKED_STATUS Run(SqlProcessor* processor, const StatementParameters& params);

 protected:
  // Execute the prepared statement. Don't reprepare.
  CHECKED_STATUS DoExecute(SqlProcessor* processor,
                           const StatementParameters& params,
                           MonoTime* last_prepare_time,
                           bool* new_analysis_needed);

  // The keyspace this statement is parsed in.
  const std::string keyspace_;

  // The text of the SQL statement.
  const std::string text_;

  // The prepare time.
  MonoTime prepare_time_;

  // The parse tree.
  ParseTree::UniPtr parse_tree_;

  // Shared/exclusive lock on the parse tree and parse time.
  boost::shared_mutex lock_;
};

} // namespace sql
} // namespace yb

#endif  // YB_SQL_STATEMENT_H_

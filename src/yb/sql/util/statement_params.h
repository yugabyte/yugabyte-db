//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Parameters for executing a SQL statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_UTIL_STATEMENT_PARAMS_H_
#define YB_SQL_UTIL_STATEMENT_PARAMS_H_

#include <boost/thread/shared_mutex.hpp>

#include "yb/common/yql_protocol.pb.h"
#include "yb/common/yql_value.h"

namespace yb {
namespace sql {

// This class represents the parameters for executing a SQL statement.
class StatementParameters {
 public:
  // Public types.
  typedef std::unique_ptr<StatementParameters> UniPtr;
  typedef std::unique_ptr<const StatementParameters> UniPtrConst;

  // Constructors
  StatementParameters();
  virtual ~StatementParameters();

  // Accessor functions.
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

  // Retrieve a bind variable for the execution of the statement. To be overridden by subclasses
  // to return actual bind variables.
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

} // namespace sql
} // namespace yb

#endif  // YB_SQL_UTIL_STATEMENT_PARAMS_H_

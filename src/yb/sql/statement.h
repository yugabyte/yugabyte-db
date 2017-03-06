//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This class represents a SQL statement (to be adde).
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_STATEMENT_H_
#define YB_SQL_STATEMENT_H_

#include <string>
#include <glog/logging.h>
#include "yb/common/yql_protocol.pb.h"

namespace yb {
namespace sql {

// This class represents the parameters for executing a SQL statement.
class StatementParameters {
 public:
  StatementParameters(int64_t page_size = INT64_MAX,
                      const std::string& paging_state = "");
  StatementParameters(StatementParameters&& other);

  // Accessors
  uint64_t page_size() const { return page_size_; }
  const YQLPagingStatePB& paging_state() const { return paging_state_pb_; }
  const std::string& next_row_key_to_read() const {
    return paging_state_pb_.next_row_key_to_read();
  }
  const std::string& table_id() const { return paging_state_pb_.table_id(); }
  const std::string& next_partition_key() const { return paging_state_pb_.next_partition_key(); }
  int64_t total_num_rows_read() const { return paging_state_pb_.total_num_rows_read(); }

 private:
  // Limit of the number of rows to return set as page size.
  const uint64_t page_size_;

  // Paging State.
  YQLPagingStatePB paging_state_pb_;
};

} // namespace sql
} // namespace yb

#endif  // YB_SQL_STATEMENT_H_

//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry to SQL module. It takes SQL statements and uses the given YBClient to execute them. Each
// SqlProcessor runs on one and only one thread, so all function in SQL modules don't need to be
// thread-safe.
//--------------------------------------------------------------------------------------------------
#ifndef YB_SQL_SQL_PROCESSOR_H_
#define YB_SQL_SQL_PROCESSOR_H_

#include <yb/util/metrics.h>
#include "yb/sql/util/sql_env.h"
#include "yb/sql/ybsql.h"

namespace yb {
namespace sql {

class SQLMetrics {
 public:
  explicit SQLMetrics(const scoped_refptr<yb::MetricEntity>& metric_entity);

  scoped_refptr<yb::Histogram> time_to_process_request_;
  scoped_refptr<yb::Histogram> time_to_get_processor_;
  scoped_refptr<yb::Histogram> time_to_parse_request_;
  scoped_refptr<yb::Histogram> time_to_execute_request_;
  scoped_refptr<yb::Histogram> time_to_queue_response_;

  scoped_refptr<yb::Counter> num_errors_parsing_;
};

class SqlProcessor {
 public:
  // Public types.
  typedef std::unique_ptr<SqlProcessor> UniPtr;
  typedef std::unique_ptr<const SqlProcessor> UniPtrConst;
  static const int kSessionTimeoutMs = 60000;

  // Constructors.
  explicit SqlProcessor(std::shared_ptr<client::YBClient> client);
  virtual ~SqlProcessor();

  // Execute the given statement.
  CHECKED_STATUS Run(const std::string& sql_stmt, YbSqlMetrics* yb_metrics = nullptr);

  // Send the rows_result back for processing. If there's an error, the rows_result is set to
  // nullptr.
  const RowsResult* rows_result() const {
    return sql_env_->rows_result();
  }

  // Construct a row_block and send it back.
  std::shared_ptr<YQLRowBlock> row_block() const {
    return sql_env_->row_block();
  }

  // Claim this processor for a request.
  void used() {
    is_used_ = true;
  }
  // Unclaim this processor.
  void unused() {
    is_used_ = false;
  }
  // Check if the processor is currently working on a statement.
  bool is_used() const {
    return is_used_;
  }

 protected:
  // SQL engine.
  YbSql::UniPtr ybsql_;

  // Environment (YBClient) that processor uses to execute statement.
  std::shared_ptr<client::YBClient> client_;
  std::shared_ptr<client::YBSession> write_session_;
  std::shared_ptr<client::YBSession> read_session_;
  SqlEnv::UniPtr sql_env_;

  // Processing state.
  bool is_used_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_SQL_PROCESSOR_H_

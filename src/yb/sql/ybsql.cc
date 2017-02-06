//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql.h"
#include <gflags/gflags.h>

METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_ParseRequest,
    "Time spent parsing the SQL query", yb::MetricUnit::kMicroseconds,
    "Time spent parsing the SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_AnalyseRequest,
    "Time spent to analyse the parsed SQL query", yb::MetricUnit::kMicroseconds,
    "Time spent to analyse the parsed SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_ExecuteRequest,
    "Time spent executing the parsed SQL query", yb::MetricUnit::kMicroseconds,
    "Time spent executing the parsed SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_NumRoundsToAnalyse,
    "Number of rounds to successfully parse a SQL query", yb::MetricUnit::kOperations,
    "Number of rounds to successfully parse a SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_ResponseSize,
    "Size of the returned response blob (in bytes)", yb::MetricUnit::kBytes,
    "Size of the returned response blob (in bytes)", 60000000LU, 2);

// Currently, the default is set to 0 because we don't cache the metadata. There's no need to
// refresh and clear the stale metadata.
DEFINE_int32(YB_SQL_EXEC_MAX_METADATA_REFRESH_COUNT, 0,
             "The maximum number of times YbSql engine can refresh metadata cache due to schema "
             "version mismatch error when executing a SQL statement");

namespace yb {
namespace sql {

using std::string;
using std::unique_ptr;

YbSqlMetrics::YbSqlMetrics(const scoped_refptr<yb::MetricEntity> &metric_entity) {
  time_to_parse_sql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ParseRequest.Instantiate(metric_entity);
  time_to_analyse_sql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_AnalyseRequest.Instantiate(metric_entity);
  time_to_execute_sql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ExecuteRequest.Instantiate(metric_entity);
  num_rounds_to_analyse_sql_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_NumRoundsToAnalyse.Instantiate(
          metric_entity);
  sql_response_size_bytes_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ResponseSize.Instantiate(metric_entity);
}

YbSql::YbSql()
    : parser_(new Parser()),
      analyzer_(new Analyzer()),
      executor_(new Executor()) {
}

YbSql::~YbSql() {
}

CHECKED_STATUS YbSql::Process(SqlEnv *sql_env, const string &sql_stmt, YbSqlMetrics *sql_metrics) {
  // Parse the statement and get the generated parse tree.
  MonoTime begin_parse = MonoTime::Now(MonoTime::FINE);
  RETURN_NOT_OK(parser_->Parse(sql_stmt));
  ParseTree::UniPtr parse_tree = parser_->Done();
  DCHECK(parse_tree.get() != nullptr) << "Parse tree is null";
  MonoTime end_parse = MonoTime::Now(MonoTime::FINE);
  if (sql_metrics != nullptr) {
    sql_metrics->time_to_parse_sql_query_->Increment(
        end_parse.GetDeltaSince(begin_parse).ToMicroseconds());
  }

  Status s;
  int refresh_count = 0;
  while (refresh_count <= FLAGS_YB_SQL_EXEC_MAX_METADATA_REFRESH_COUNT) {
    // Semantic analysis.
    // Traverse, error-check, and decorate the parse tree nodes with datatypes.
    MonoTime begin_analyze = MonoTime::Now(MonoTime::FINE);
    Status sem_status = analyzer_->Analyze(sql_stmt, move(parse_tree), sql_env, refresh_count);
    ErrorCode sem_errcode = analyzer_->error_code();
    MonoTime end_analyze = MonoTime::Now(MonoTime::FINE);
    if (sql_metrics != nullptr) {
      sql_metrics->time_to_analyse_sql_query_->Increment(
          end_analyze.GetDeltaSince(begin_analyze).ToMicroseconds());
    }

    // Release the parse tree for the next step in the process.
    parse_tree = analyzer_->Done();
    CHECK(parse_tree.get() != nullptr) << "SEM tree is null";

    // Check error code.
    if (sem_errcode == ErrorCode::DDL_EXECUTION_RERUN_NOT_ALLOWED) {
      // If rerun failed, we report the previous status to users.
      if (sql_metrics != nullptr) {
        sql_metrics->num_rounds_to_analyse_sql_->Increment(refresh_count + 1);
      }
      return s;
    }

    // Increment the refresh count.
    refresh_count++;

    // If failure occurs, it could be because the cached descriptors are stale. In that case, we
    // reload the table descriptor and analyze the statement again.
    s = sem_status;
    if (sem_errcode != ErrorCode::SUCCESS) {
      if (sem_errcode != ErrorCode::TABLE_NOT_FOUND) {
        continue;
      }
      if (sql_metrics != nullptr) {
        sql_metrics->num_rounds_to_analyse_sql_->Increment(refresh_count);
      }
      return s;
    }

    // TODO(neil) Code generation. We bypass this step at this time.

    // Code execution.
    MonoTime begin_execute = MonoTime::Now(MonoTime::FINE);
    s = executor_->Execute(sql_stmt, move(parse_tree), sql_env);
    ErrorCode exec_errcode = executor_->error_code();
    MonoTime end_execute = MonoTime::Now(MonoTime::FINE);
    if (sql_metrics != nullptr) {
      sql_metrics->time_to_execute_sql_query_->Increment(
          end_execute.GetDeltaSince(begin_execute).ToMicroseconds());
    }

    // Release the parse tree as we are done.
    parse_tree = executor_->Done();
    CHECK(parse_tree.get() != nullptr) << "Exec tree is null";

    // If the failure occurs because of stale metadata cache, rerun with latest metadata.
    if (exec_errcode != ErrorCode::WRONG_METADATA_VERSION) {
      if (sql_metrics != nullptr) {
        sql_metrics->num_rounds_to_analyse_sql_->Increment(refresh_count);
      }
      return s;
    }
  }

  // Return status.
  if (sql_metrics != nullptr) {
    sql_metrics->num_rounds_to_analyse_sql_->Increment(refresh_count);
  }
  return s;
}

CHECKED_STATUS YbSql::GenerateParseTree(const std::string& sql_stmt,
                                        ParseTree::UniPtr *parse_tree) {
  // Parse the statement and get the generated parse tree.
  RETURN_NOT_OK(parser_->Parse(sql_stmt));
  *parse_tree = parser_->Done();
  DCHECK((*parse_tree).get() != nullptr) << "Parse tree is null";

  return Status::OK();
}

CHECKED_STATUS YbSql::TestParser(const string& sql_stmt) {
  ParseTree::UniPtr parse_tree;
  return GenerateParseTree(sql_stmt, &parse_tree);
}

CHECKED_STATUS YbSql::TestAnalyzer(SqlEnv *sql_env, const string& sql_stmt,
                                   ParseTree::UniPtr *parse_tree) {
  RETURN_NOT_OK(GenerateParseTree(sql_stmt, parse_tree));

  RETURN_NOT_OK(analyzer_->Analyze(sql_stmt, move(*parse_tree), sql_env, 0));

  *parse_tree = analyzer_->Done();

  return Status::OK();
}

}  // namespace sql
}  // namespace yb

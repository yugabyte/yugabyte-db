//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Primary include file for YugaByte SQL engine. This should be the first file included by YugaByte
// backend modules.
//
// NOTE
// - YbSql object is not thread safe.
// - When server is processing requests, it dedicate a thread to create a YbSql object to process
//   the requests and deletes this object when the process is completed. The YbSql object can be
//   reused to process different SQL statements at different time.
//--------------------------------------------------------------------------------------------------
#ifndef YB_SQL_YBSQL_H_
#define YB_SQL_YBSQL_H_

#include <yb/util/metrics.h>
#include "yb/sql/exec/executor.h"
#include "yb/sql/parser/parser.h"
#include "yb/sql/sem/analyzer.h"
#include "yb/sql/util/errcodes.h"
#include "yb/sql/util/sql_env.h"

namespace yb {
namespace sql {

class YbSqlMetrics {
 public:
  explicit YbSqlMetrics(const scoped_refptr<yb::MetricEntity>& metric_entity);

  scoped_refptr<yb::Histogram> time_to_parse_sql_query_;
  scoped_refptr<yb::Histogram> time_to_analyse_sql_query_;
  scoped_refptr<yb::Histogram> time_to_execute_sql_query_;
  scoped_refptr<yb::Histogram> num_rounds_to_analyse_sql_;

  scoped_refptr<yb::Histogram> sql_response_size_bytes_;
};

class YbSql {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<YbSql> UniPtr;
  typedef std::unique_ptr<const YbSql> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  // Constructor & desructor.
  YbSql();
  virtual ~YbSql();

  // Process a SQL statement and return error codes.
  CHECKED_STATUS Process(SqlEnv* sql_env, const std::string& sql_stmt, YbSqlMetrics* yb_metrics);

 private:
  //------------------------------------------------------------------------------------------------
  // Friends.
  friend class YbSqlTestBase;

  //------------------------------------------------------------------------------------------------
  // Parse SQL statements.
  CHECKED_STATUS TestParser(const std::string& sql_stmt);

  //------------------------------------------------------------------------------------------------
  // Analyze SQL statements.
  CHECKED_STATUS TestAnalyzer(SqlEnv *sql_env, const std::string& sql_stmt,
                              ParseTree::UniPtr *parse_tree);

  // Generate parse tree.
  CHECKED_STATUS GenerateParseTree(const std::string& sql_stmt,
                                   ParseTree::UniPtr *parse_tree);

  //------------------------------------------------------------------------------------------------
  // Parsing processor.
  Parser::UniPtr parser_;

  // Semantic analysis processor.
  Analyzer::UniPtr analyzer_;

  // Tree execution.
  Executor::UniPtr executor_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_YBSQL_H_

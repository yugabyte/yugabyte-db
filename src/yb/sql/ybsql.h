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

#include "yb/sql/session_context.h"
#include "yb/sql/parser/parser.h"
#include "yb/sql/sem/analyzer.h"
#include "yb/sql/exec/executor.h"
#include "yb/sql/util/errcodes.h"

namespace yb {
namespace sql {

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
  ErrorCode Process(SessionContext *session_context, const std::string& sql_stmt);

 private:
  //------------------------------------------------------------------------------------------------
  // Friends.
  friend class YbSqlTestBase;

  //------------------------------------------------------------------------------------------------
  // Parse SQL statements.
  ErrorCode TestParser(const std::string& sql_stmt);

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

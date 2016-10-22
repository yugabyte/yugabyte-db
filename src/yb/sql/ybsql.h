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

#include <stdio.h>

#include <cstddef>

#include "yb/sql/parser/parser.h"
#include "yb/sql/util/errcodes.h"
#include "yb/sql/util/memory_context.h"

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
  ErrorCode Process(const std::string& sql_stmt);

 private:
  //------------------------------------------------------------------------------------------------
  // Parsing processor.
  Parser::UniPtr parser_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_YBSQL_H_

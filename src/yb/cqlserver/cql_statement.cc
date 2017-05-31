//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/cqlserver/cql_statement.h"

#include <sasl/md5global.h>
#include <sasl/md5.h>

namespace yb {
namespace cqlserver {

//------------------------------------------------------------------------------------------------
CQLStatement::CQLStatement(const string& keyspace, const string& sql_stmt)
    : Statement(keyspace, sql_stmt) {
}

CQLStatement::~CQLStatement() {
}

CQLMessage::QueryId CQLStatement::GetQueryId(const string& keyspace, const string& sql_stmt) {
  unsigned char md5[16];
  MD5_CTX md5ctx;
  _sasl_MD5Init(&md5ctx);
  _sasl_MD5Update(&md5ctx, util::to_uchar_ptr(keyspace.data()), keyspace.length());
  _sasl_MD5Update(&md5ctx, util::to_uchar_ptr(sql_stmt.data()), sql_stmt.length());
  _sasl_MD5Final(md5, &md5ctx);
  return CQLMessage::QueryId(util::to_char_ptr(md5), sizeof(md5));
}

}  // namespace cqlserver
}  // namespace yb

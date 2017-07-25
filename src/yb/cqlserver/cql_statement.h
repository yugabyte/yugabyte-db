//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This class defines a CQL statement. A CQL statement extends from a SQL statement to handle query
// ID and caching prepared statements in a list.
//--------------------------------------------------------------------------------------------------

#ifndef YB_CQLSERVER_CQL_STATEMENT_H_
#define YB_CQLSERVER_CQL_STATEMENT_H_

#include <list>

#include "yb/cqlserver/cql_message.h"
#include "yb/sql/statement.h"

namespace yb {
namespace cqlserver {

class CQLStatement;

// A map of CQL query id to the prepared statement for caching the prepared statments. Shared_ptr
// is used so that a prepared statement can be aged out and removed from the cache without deleting
// it when it is being executed by another client in another thread.
using CQLStatementMap = std::unordered_map<CQLMessage::QueryId, std::shared_ptr<CQLStatement>>;

// A LRU list of CQL statements and position in the list.
using CQLStatementList = std::list<std::shared_ptr<CQLStatement>>;
using CQLStatementListPos = CQLStatementList::iterator;

// A CQL statement that is prepared and cached.
class CQLStatement : public sql::Statement {
 public:
  CQLStatement(const std::string& keyspace, const std::string& sql_stmt, CQLStatementListPos pos);
  ~CQLStatement();

  // Return the query id.
  CQLMessage::QueryId query_id() const { return GetQueryId(keyspace_, text_); }

  // Get/set position of the statement in the LRU.
  CQLStatementListPos pos() const { return pos_; }
  void set_pos(CQLStatementListPos pos) const { pos_ = pos; }

  // Return the query id of a statement.
  static CQLMessage::QueryId GetQueryId(const std::string& keyspace, const std::string& sql_stmt);

 private:
  // Position of the statement in the LRU.
  mutable CQLStatementListPos pos_;
};

}  // namespace cqlserver
}  // namespace yb

#endif  // YB_CQLSERVER_CQL_STATEMENT_H_

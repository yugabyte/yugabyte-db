//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
// This class defines a CQL statement. A CQL statement extends from a SQL statement to handle query
// ID and caching prepared statements in a list.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_CQLSERVER_CQL_STATEMENT_H_
#define YB_YQL_CQL_CQLSERVER_CQL_STATEMENT_H_

#include <list>

#include "yb/yql/cql/ql/statement.h"
#include "yb/yql/cql/ql/util/cql_message.h"

namespace yb {
namespace cqlserver {

class CQLStatement;

// A map of CQL query id to the prepared statement for caching the prepared statments. Shared_ptr
// is used so that a prepared statement can be aged out and removed from the cache without deleting
// it when it is being executed by another client in another thread.
using CQLStatementMap = std::unordered_map<ql::CQLMessage::QueryId, std::shared_ptr<CQLStatement>>;

// A LRU list of CQL statements and position in the list.
using CQLStatementList = std::list<std::shared_ptr<CQLStatement>>;
using CQLStatementListPos = CQLStatementList::iterator;

// A CQL statement that is prepared and cached.
class CQLStatement : public ql::Statement {
 public:
  CQLStatement(const std::string& keyspace, const std::string& query, CQLStatementListPos pos);
  ~CQLStatement();

  // Return the query id.
  ql::CQLMessage::QueryId query_id() const { return GetQueryId(keyspace_, text_); }

  // Get/set position of the statement in the LRU.
  CQLStatementListPos pos() const { return pos_; }
  void set_pos(CQLStatementListPos pos) const { pos_ = pos; }

  // Get schema version this statement used for preparation.
  Result<SchemaVersion> GetYBTableSchemaVersion() const {
    const ql::ParseTree& parser_tree = VERIFY_RESULT(GetParseTree());
    return parser_tree.GetYBTableSchemaVersion();
  }

  // Check if the used schema version is up to date with the Master.
  Result<bool> IsYBTableAltered(ql::QLEnv* ql_env) const {
    const ql::ParseTree& parser_tree = VERIFY_RESULT(GetParseTree());
    return parser_tree.IsYBTableAltered(ql_env);
  }

  // Return the query id of a statement.
  static ql::CQLMessage::QueryId GetQueryId(const std::string& keyspace, const std::string& query);

 private:
  // Position of the statement in the LRU.
  mutable CQLStatementListPos pos_;
};

}  // namespace cqlserver
}  // namespace yb

#endif  // YB_YQL_CQL_CQLSERVER_CQL_STATEMENT_H_

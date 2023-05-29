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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/cqlserver/cql_statement.h"

#include <openssl/md5.h>

DEFINE_RUNTIME_bool(cql_use_metadata_cache_for_schema_version_check, true,
                    "Use the internal Table Metadata Cache in TS to check the Table "
                    "Schema Version when processing the YCQL PREPARE query."
                    "If disabled - the Table Schema Version is requested from the Master.");
TAG_FLAG(cql_use_metadata_cache_for_schema_version_check, advanced);

using std::string;

namespace yb {
namespace cqlserver {

//------------------------------------------------------------------------------------------------
CQLStatement::CQLStatement(
    const string& keyspace, const string& query, const CQLStatementListPos pos,
    const MemTrackerPtr& mem_tracker)
    : Statement(keyspace, query), pos_(pos), consumption_(mem_tracker, DynamicMemoryUsage()) {}

CQLStatement::~CQLStatement() {
}

Result<bool> CQLStatement::IsYBTableAltered(ql::QLEnv* ql_env) const {
  const ql::ParseTree& parser_tree = VERIFY_RESULT(GetParseTree());
  const bool use_cache = FLAGS_cql_use_metadata_cache_for_schema_version_check;
  return parser_tree.IsYBTableAltered(ql_env, use_cache);
}

ql::CQLMessage::QueryId CQLStatement::GetQueryId(const string& keyspace, const string& query) {
  unsigned char md5[MD5_DIGEST_LENGTH];
  MD5_CTX md5ctx;
  MD5_Init(&md5ctx);
  MD5_Update(&md5ctx, to_uchar_ptr(keyspace.data()), keyspace.length());
  MD5_Update(&md5ctx, to_uchar_ptr(query.data()), query.length());
  MD5_Final(md5, &md5ctx);
  return ql::CQLMessage::QueryId(to_char_ptr(md5), sizeof(md5));
}

}  // namespace cqlserver
}  // namespace yb

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
// Responsible for logging audit records in YCQL.
// Audit is controlled through gflags. If the audit is not enabled, logging methods return
// immediately without imposing overhead.
// This class should be used for a single request at a time, as a part of (C)QLProcessor internals,
// and thus isn't thread-safe.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <boost/uuid/uuid_generators.hpp>

#include "yb/yql/cql/ql/ql_fwd.h"
#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/util/cql_message.h"

namespace yb {
namespace ql {
namespace audit {

// Whether the statement being logged is being PREPARE'd rather than executed.
YB_STRONGLY_TYPED_BOOL(IsPrepare)
YB_STRONGLY_TYPED_BOOL(ErrorIsFormatted)

class Type;
struct LogEntry;

class AuditLogger {
 public:
  explicit AuditLogger(const QLEnv& ql_env);

  // Sets a connection for the current (new) user operation.
  // Not called for internal requests, resulting in them not being audited.
  void SetConnection(const std::shared_ptr<const rpc::Connection>& conn) {
    conn_ = conn;
  }

  // Enters the batch request mode, should be called when driver-level batch is received.
  // This generates a UUID to identify the current batch in audit.
  //
  // Note that this is only used for batch requests, not for explicit START TRANSACTION commands
  // because in that case separate commands might arrive to different tservers.
  //
  // If this returns non-OK status, batch mode isn't activated.
  Status StartBatchRequest(size_t statements_count,
                           IsRescheduled is_rescheduled);

  // Exits the batch request mode. Does nothing outside of a batch request.
  Status EndBatchRequest();

  // Log the response to a user's authentication request.
  Status LogAuthResponse(const CQLResponse& response);

  // Log the statement execution start.
  // tnode might be nullptr, in which case this does nothing.
  Status LogStatement(const TreeNode* tnode,
                      const std::string& statement,
                      IsPrepare is_prepare);

  // Log the statement analysis/execution failure.
  // tnode might be nullptr, in which case this does nothing.
  Status LogStatementError(const TreeNode* tnode,
                           const std::string& statement,
                           const Status& error_status,
                           ErrorIsFormatted error_is_formatted);

  // Log a general statement processing failure.
  // We should only use this directly when the parse tree is not present.
  Status LogStatementError(const std::string& statement,
                           const Status& error_status,
                           ErrorIsFormatted error_is_formatted);

 private:
  using GflagName = std::string;
  using GflagStringValue = std::string;
  using GflagListValue = std::unordered_set<std::string>;
  using GflagsCache = std::unordered_map<GflagName, std::pair<GflagStringValue, GflagListValue>>;

  const QLEnv& ql_env_;

  // Currently audited connection, if any.
  std::shared_ptr<const rpc::Connection> conn_;

  // Empty string means not in a batch processing mode.
  std::string batch_id_;

  boost::uuids::random_generator batch_id_gen_;

  // Cache of parsed gflags, to avoid re-parsing unchanged values.
  GflagsCache gflags_cache_;

  // Checks whether a given predicate holds on the comma-separated list gflag.
  // This uses gflag library helper to access a gflag by name, to avoid concurrently accessing
  // string gflags that may change at runtime.
  template<class Pred>
  bool SatisfiesGFlag(const LogEntry& e,
                      const std::string& gflag_name,
                      const Pred& predicate);

  // Determine whether this entry should be logged given current audit configuration.
  // Note that we reevaluate gflags to allow changing them dynamically.
  bool ShouldBeLogged(const LogEntry& e);

  Result<LogEntry> CreateLogEntry(const Type& type,
                                  std::string keyspace,
                                  std::string scope,
                                  std::string operation,
                                  std::string error_message);
};

} // namespace audit
} // namespace ql
} // namespace yb

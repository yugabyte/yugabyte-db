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


#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"

#include "yb/common/common.pb.h"

using namespace std::literals;

DECLARE_int64(external_mini_cluster_max_log_bytes);

namespace yb {
namespace pgwrapper {

void LibPqTestBase::SetUp() {
  // YSQL has very verbose logging in case of conflicts
  // TODO: reduce the verbosity of that logging.
  FLAGS_external_mini_cluster_max_log_bytes = 512_MB;
  PgWrapperTestBase::SetUp();
}

Result<PGConnPtr> LibPqTestBase::Connect() {
  auto start = CoarseMonoClock::now();
  auto deadline = start + 60s;
  for (;;) {
    PGConnPtr result(PQconnectdb(Format(
        "host=$0 port=$1 user=postgres", pg_ts->bind_host(), pg_ts->pgsql_rpc_port()).c_str()));
    auto status = PQstatus(result.get());
    if (status == ConnStatusType::CONNECTION_OK) {
      return result;
    }
    auto now = CoarseMonoClock::now();
    if (now >= deadline) {
      return STATUS_FORMAT(NetworkError, "Connect failed: $0, passed: $1",
                           status, MonoDelta(now - start));
    }
  }
}

bool LibPqTestBase::TransactionalFailure(const Status& status) {
  auto message = status.ToString();
  return message.find("Restart read required at") != std::string::npos ||
         message.find("Transaction expired") != std::string::npos ||
         message.find("Transaction aborted") != std::string::npos ||
         message.find("Unknown transaction") != std::string::npos ||
         message.find("Transaction metadata missing") != std::string::npos ||
         message.find("Transaction was recently aborted") != std::string::npos ||
         message.find("Conflicts with committed transaction") != std::string::npos ||
         message.find("Value write after transaction start") != std::string::npos ||
         message.find("Conflicts with higher priority transaction") != std::string::npos;
}

} // namespace pgwrapper
} // namespace yb

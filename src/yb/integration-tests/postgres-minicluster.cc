// Copyright (c) YugabyteDB, Inc.
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

#include "yb/integration-tests/postgres-minicluster.h"

namespace yb {

Result<pgwrapper::PGConn> PostgresMiniCluster::Connect() {
  return ConnectToDB(std::string() /* dbname */);
}

Result<pgwrapper::PGConn> PostgresMiniCluster::ConnectToDB(
    const std::string& dbname, bool simple_query_protocol) {
  return pgwrapper::PGConnBuilder(
             {.host = pg_host_port_.host(), .port = pg_host_port_.port(), .dbname = dbname})
      .Connect(simple_query_protocol);
}

Result<pgwrapper::PGConn> PostgresMiniCluster::ConnectToDBWithReplication(
    const std::string& dbname) {
  return pgwrapper::PGConnBuilder({
                                      .host = pg_host_port_.host(),
                                      .port = pg_host_port_.port(),
                                      .dbname = dbname,
                                      .replication = "database",
                                  })
      .Connect(/*simple_query_protocol=*/true);
}

}  // namespace yb

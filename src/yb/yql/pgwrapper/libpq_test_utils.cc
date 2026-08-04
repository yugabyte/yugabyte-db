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

#include "yb/yql/pgwrapper/libpq_test_utils.h"

namespace yb::pgwrapper {

Result<uint64_t> GetCatalogVersion(PGConn* conn, bool db_catalog_version_mode) {
  return conn->FetchRow<PGUint64>(
      !db_catalog_version_mode
          ? "SELECT current_version FROM pg_yb_catalog_version"
          : "SELECT current_version FROM"
            "    pg_yb_catalog_version AS v INNER JOIN pg_database AS d ON (v.db_oid = d.oid)"
            "    WHERE datname = current_database()");
}

} // namespace yb::pgwrapper

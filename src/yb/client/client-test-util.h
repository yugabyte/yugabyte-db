// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef YB_CLIENT_CLIENT_TEST_UTIL_H
#define YB_CLIENT_CLIENT_TEST_UTIL_H

#include <string>
#include <vector>

#include "yb/client/client.h"
#include "yb/gutil/macros.h"
#include "yb/util/status.h"

namespace yb {
class Schema;

namespace client {
class YBSchema;

// Log any pending errors in the given session, and then crash the current
// process.
void LogSessionErrorsAndDie(const sp::shared_ptr<YBSession>& session,
                            const Status& s);

// Flush the given session. If any errors occur, log them and crash
// the process.
inline void FlushSessionOrDie(const sp::shared_ptr<YBSession>& session) {
  Status s = session->Flush();
  if (PREDICT_FALSE(!s.ok())) {
    LogSessionErrorsAndDie(session, s);
  }
}

// Scans in LEADER_ONLY mode, returning stringified rows in the given vector.
void ScanTableToStrings(YBTable* table, std::vector<std::string>* row_strings);

// Count the number of rows in the table in LEADER_ONLY mode.
int64_t CountTableRows(YBTable* table);

void ScanToStrings(YBScanner* scanner, std::vector<std::string>* row_strings);

// Convert a yb::Schema to a yb::client::YBSchema.
YBSchema YBSchemaFromSchema(const Schema& schema);

} // namespace client
} // namespace yb

#endif /* YB_CLIENT_CLIENT_TEST_UTIL_H */

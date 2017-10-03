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
#ifndef KUDU_CLIENT_CLIENT_TEST_UTIL_H
#define KUDU_CLIENT_CLIENT_TEST_UTIL_H

#include <string>
#include <vector>

#include "kudu/client/client.h"
#include "kudu/gutil/macros.h"
#include "kudu/util/status.h"

namespace kudu {
class Schema;

namespace client {
class KuduSchema;

// Log any pending errors in the given session, and then crash the current
// process.
void LogSessionErrorsAndDie(const sp::shared_ptr<KuduSession>& session,
                            const Status& s);

// Flush the given session. If any errors occur, log them and crash
// the process.
inline void FlushSessionOrDie(const sp::shared_ptr<KuduSession>& session) {
  Status s = session->Flush();
  if (PREDICT_FALSE(!s.ok())) {
    LogSessionErrorsAndDie(session, s);
  }
}

// Scans in LEADER_ONLY mode, returning stringified rows in the given vector.
void ScanTableToStrings(KuduTable* table, std::vector<std::string>* row_strings);

// Count the number of rows in the table in LEADER_ONLY mode.
int64_t CountTableRows(KuduTable* table);

void ScanToStrings(KuduScanner* scanner, std::vector<std::string>* row_strings);

// Convert a kudu::Schema to a kudu::client::KuduSchema.
KuduSchema KuduSchemaFromSchema(const Schema& schema);

} // namespace client
} // namespace kudu

#endif /* KUDU_CLIENT_CLIENT_TEST_UTIL_H */

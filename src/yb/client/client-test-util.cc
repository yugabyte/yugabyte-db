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

#include "yb/client/client-test-util.h"

#include <vector>

#include "yb/gutil/stl_util.h"
#include "yb/util/test_util.h"

namespace yb {
namespace client {

void LogSessionErrorsAndDie(const std::shared_ptr<YBSession>& session,
                            const Status& s) {
  CHECK(!s.ok());
  CollectedErrors errors;
  bool overflow;
  session->GetPendingErrors(&errors, &overflow);
  CHECK(!overflow);

  // Log only the first 10 errors.
  LOG(INFO) << errors.size() << " failed ops. First 10 errors follow";
  int i = 0;
  for (const auto& e : errors) {
    if (i == 10) {
      break;
    }
    LOG(INFO) << "Op " << e->failed_op().ToString()
              << " had status " << e->status().ToString();
    i++;
  }
  CHECK_OK(s); // will fail
}

void ScanTableToStrings(YBTable* table, vector<string>* row_strings) {
  row_strings->clear();
  YBScanner scanner(table);
  ASSERT_OK(scanner.SetSelection(YBClient::LEADER_ONLY));
  ASSERT_OK(scanner.SetTimeoutMillis(60000));
  ScanToStrings(&scanner, row_strings);
}

int64_t CountTableRows(YBTable* table) {
  vector<string> rows;
  client::ScanTableToStrings(table, &rows);
  return rows.size();
}

void ScanToStrings(YBScanner* scanner, vector<string>* row_strings) {
  ASSERT_OK(scanner->Open());
  vector<YBRowResult> rows;
  while (scanner->HasMoreRows()) {
    ASSERT_OK(scanner->NextBatch(&rows));
    for (const YBRowResult& row : rows) {
      row_strings->push_back(row.ToString());
    }
  }
}

YBSchema YBSchemaFromSchema(const Schema& schema) {
  return YBSchema(schema);
}

}  // namespace client
}  // namespace yb

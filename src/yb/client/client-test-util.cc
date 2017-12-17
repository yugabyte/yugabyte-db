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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/client/client-test-util.h"

#include <vector>

#include "yb/client/client.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/stl_util.h"
#include "yb/util/test_util.h"

namespace yb {
namespace client {

void LogSessionErrorsAndDie(const std::shared_ptr<YBSession>& session,
                            const Status& s) {
  CHECK(!s.ok());
  auto errors = session->GetPendingErrors();

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

void FlushSessionOrDie(const std::shared_ptr<YBSession>& session,
                       const std::vector<std::shared_ptr<YBqlOp>>& ops) {
  Status s = session->Flush();
  if (PREDICT_FALSE(!s.ok())) {
    LogSessionErrorsAndDie(session, s);
  }
  for (auto& op : ops) {
    CHECK_EQ(QLResponsePB::YQL_STATUS_OK, op->response().status())
        << "Status: " << QLResponsePB::QLStatus_Name(op->response().status());
  }
}

void ScanTableToStrings(YBTable* table, vector<string>* row_strings) {
  row_strings->clear();
  YBScanner scanner(table);
  ASSERT_OK(scanner.SetSelection(YBClient::LEADER_ONLY));
  ASSERT_OK(scanner.SetTimeoutMillis(60000));
  ScanToStrings(&scanner, row_strings);
}

std::vector<std::string> ScanTableToStrings(YBTable* table) {
  YBScanner scanner(table);
  EXPECT_OK(scanner.SetSelection(YBClient::LEADER_ONLY));
  EXPECT_OK(scanner.SetTimeoutMillis(60000));
  return ScanToStrings(&scanner);
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

std::vector<std::string> ScanToStrings(YBScanner* scanner) {
  EXPECT_OK(scanner->Open());
  YBScanBatch batch;
  std::vector<std::string> result;
  while (scanner->HasMoreRows()) {
    EXPECT_OK(scanner->NextBatch(&batch));
    for (const auto& row : batch) {
      result.push_back(row.ToString());
    }
  }
  return result;
}

YBSchema YBSchemaFromSchema(const Schema& schema) {
  return YBSchema(schema);
}

}  // namespace client
}  // namespace yb

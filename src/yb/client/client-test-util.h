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
#ifndef YB_CLIENT_CLIENT_TEST_UTIL_H_
#define YB_CLIENT_CLIENT_TEST_UTIL_H_

#include <string>
#include <vector>

#include "yb/client/client_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {
class Schema;

namespace client {

class TableRange;
class YBSchema;


// Log any pending errors in the given session flush status, and then crash the current
// process.
void LogSessionErrorsAndDie(const FlushStatus& flush_status);

// Flush the given session. If any errors occur, log them and crash
// the process.
void FlushSessionOrDie(const YBSessionPtr& session, const std::vector<YBqlOpPtr>& ops = {});

// Scans in LEADER_ONLY mode, returning stringified rows in the given vector.
void ScanTableToStrings(const TableHandle& table, std::vector<std::string>* row_strings);

// Scans in LEADER_ONLY mode, returning stringified rows.
std::vector<std::string> ScanTableToStrings(const TableHandle& table);

// Count the number of rows in the table in LEADER_ONLY mode.
int64_t CountTableRows(const TableHandle& table);

std::vector<std::string> ScanToStrings(const TableRange& range);

// Convert a yb::Schema to a yb::client::YBSchema.
YBSchema YBSchemaFromSchema(const Schema& schema);

// Creates an operation to read value from `value_column` for hashed key `key` for the `table`.
std::shared_ptr<YBqlReadOp> CreateReadOp(
    int32_t key, const TableHandle& table, const std::string& value_column);

Result<std::string> GetTableIdByTableName(
    YBClient* client, const std::string& namespace_name, const std::string& table_name);

void VerifyTableNotExists(
    YBClient* client, const std::string& db_name, const std::string& table_name, int timeout_secs);

}  // namespace client
}  // namespace yb

#endif // YB_CLIENT_CLIENT_TEST_UTIL_H_

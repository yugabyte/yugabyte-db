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

#pragma once

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_protocol.pb.h"

namespace yb {
namespace tablet {

struct QLReadRequestResult {
  QLResponsePB response;
  HybridTime restart_read_ht;
};

struct PgsqlReadRequestResult {
  explicit PgsqlReadRequestResult(WriteBuffer* rows_data_) : rows_data(rows_data_) {}

  PgsqlResponsePB response;
  HybridTime restart_read_ht;
  size_t num_rows_read;
  WriteBuffer* rows_data;
};

} // namespace tablet
} // namespace yb

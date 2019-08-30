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

#include "yb/common/ybc-internal.h"
#include "yb/yql/pggate/pggate_if_cxx_decl.h"

namespace yb {
namespace pggate {

PgApiExample::PgApiExample(
    const char* database_name,
    const char* table_name,
    const char** column_names) {
}

PgApiExample::~PgApiExample() {
}

bool PgApiExample::HasNext() {
  return false;
}

Result<int32_t> PgApiExample::GetInt32Column(int column_index) {
  return STATUS_FORMAT(InvalidArgument, "Invalid column $0", column_index);
}

Status PgApiExample::GetStringColumn(
    int column_index,
    const char** result) {
  *result = nullptr;
  return STATUS_FORMAT(InvalidArgument, "Invalid string column index $0", column_index);
}

} // namespace pggate
} // namespace yb

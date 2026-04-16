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

#pragma once

#include <google/protobuf/repeated_ptr_field.h>

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/memory/arena_list.h"

namespace yb::qlexpr {

Result<std::vector<dockv::KeyEntryValue>> InitKeyColumnValues(
    const google::protobuf::RepeatedPtrField<PgsqlExpressionPB>& column_values,
    const Schema& schema, size_t start_idx);

Result<std::vector<dockv::KeyEntryValue>> InitKeyColumnValues(
    const ArenaList<LWPgsqlExpressionPB> &column_values, const Schema &schema, size_t start_idx);

Result<std::vector<Slice>> InitKeyColumnValueSlices(
    Arena& arena,
    const google::protobuf::RepeatedPtrField<PgsqlExpressionPB>& column_values,
    const Schema& schema,
    size_t start_idx);

Result<std::vector<Slice>> InitKeyColumnValueSlices(
    Arena& arena,
    const ArenaList<LWPgsqlExpressionPB>& column_values,
    const Schema &schema,
    size_t start_idx);

}  // namespace yb::qlexpr

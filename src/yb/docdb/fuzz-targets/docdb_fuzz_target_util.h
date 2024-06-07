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

#pragma once
// Using FuzzedDataProvider for splitting a single fuzzer-generated input into
// several parts. See
// https://github.com/google/fuzzing/blob/master/docs/split-inputs.md#fuzzed-data-provider
#include <fuzzer/FuzzedDataProvider.h>

#include "yb/dockv/doc_key.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/util/decimal.h"
#include "yb/util/test_util.h"

namespace yb {
namespace docdb {
namespace fuzz {

enum class FuzzKeyEntryValue {
  Int32 = 0,
  String,
  Int64,
  Timestamp,
  Decimal,
  Double,
  Float,
  VarInt,
  ArrayIndex,
  GinNull,
  UInt32,
  UInt64,
  ColumnId,
  NullValue  // Must be the last one
};

dockv::KeyEntryValue GetKeyEntryValue(FuzzedDataProvider *fdp);
dockv::KeyEntryValues GetKeyEntryValueVector(FuzzedDataProvider *fdp, bool consume_all);
dockv::DocKey GetDocKey(FuzzedDataProvider *fdp, bool consume_all);

}  // namespace fuzz
}  // namespace docdb
}  // namespace yb

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

#include "yb/docdb/fuzz-targets/docdb_fuzz_target_util.h"

namespace yb {
namespace docdb {
namespace fuzz {

using dockv::KeyEntryValue;

KeyEntryValue GetKeyEntryValue(FuzzedDataProvider *fdp) {
  uint8_t max_enum_value = static_cast<uint8_t>(FuzzKeyEntryValue::NullValue);
  FuzzKeyEntryValue type_switch = static_cast<FuzzKeyEntryValue>(
      fdp->ConsumeIntegralInRange<uint8_t>(0, max_enum_value));
  switch (type_switch) {
    case FuzzKeyEntryValue::Int32:
      return KeyEntryValue::Int32(fdp->ConsumeIntegral<int32_t>());
    case FuzzKeyEntryValue::String:  // max length 50
      return KeyEntryValue(fdp->ConsumeRandomLengthString(50));
    case FuzzKeyEntryValue::Int64:
      return KeyEntryValue::Int64(fdp->ConsumeIntegral<int64_t>());
    case FuzzKeyEntryValue::Timestamp:
      return KeyEntryValue::MakeTimestamp(
          Timestamp(fdp->ConsumeIntegral<int64_t>()));
    case FuzzKeyEntryValue::Decimal: {  // in range (-999.99, 999.99)
      auto left = fdp->ConsumeIntegralInRange<int16_t>(-999, 999);
      auto right = fdp->ConsumeIntegralInRange<uint8_t>(0, 99);
      auto ds = std::to_string(left) + "." + std::to_string(right);
      return KeyEntryValue::Decimal(util::Decimal(ds).EncodeToComparable(),
                                    SortOrder::kDescending);
    }
    case FuzzKeyEntryValue::Double:
      return KeyEntryValue::Double(fdp->ConsumeFloatingPoint<double>());
    case FuzzKeyEntryValue::Float:
      return KeyEntryValue::Float(fdp->ConsumeFloatingPoint<float>());
    case FuzzKeyEntryValue::VarInt:
      return KeyEntryValue::VarInt(
          VarInt(fdp->ConsumeIntegral<uint64_t>()).EncodeToComparable(),
          SortOrder::kDescending);
    case FuzzKeyEntryValue::ArrayIndex:
      return KeyEntryValue::ArrayIndex(fdp->ConsumeIntegral<int64_t>());
    case FuzzKeyEntryValue::GinNull:  // valid values are 1,2,3.
      return KeyEntryValue::GinNull(fdp->ConsumeIntegralInRange<uint8_t>(1, 3));
    case FuzzKeyEntryValue::UInt32:
      return KeyEntryValue::UInt32(fdp->ConsumeIntegral<uint32_t>());
    case FuzzKeyEntryValue::UInt64:
      return KeyEntryValue::UInt64(fdp->ConsumeIntegral<uint64_t>());
    case FuzzKeyEntryValue::ColumnId:
      return KeyEntryValue::MakeColumnId(
          ColumnId(fdp->ConsumeIntegral<uint16_t>()));
    case FuzzKeyEntryValue::NullValue:
      return KeyEntryValue::NullValue(SortingType::kDescending);
  }
  FATAL_INVALID_ENUM_VALUE(FuzzKeyEntryValue, type_switch);
}

// consume_all = true - consume all the bytes of the input data till the end,
// otherwise there might be something left.
dockv::KeyEntryValues GetKeyEntryValueVector(FuzzedDataProvider *fdp, bool consume_all) {
  dockv::KeyEntryValues result;
  size_t len = consume_all ? std::numeric_limits<size_t>::max()
                           : fdp->ConsumeIntegralInRange<int8_t>(0, 10);

  for (size_t i = 0; (i < len) && fdp->remaining_bytes(); i++) {
    result.push_back(GetKeyEntryValue(fdp));
  }
  return result;
}

dockv::DocKey GetDocKey(FuzzedDataProvider *fdp, bool consume_all) {
  bool use_hash = fdp->ConsumeBool();
  return use_hash ? dockv::DocKey(fdp->ConsumeIntegral<uint16_t>(),
                           GetKeyEntryValueVector(fdp, false),
                           GetKeyEntryValueVector(fdp, consume_all))
                  : dockv::DocKey(GetKeyEntryValueVector(fdp, consume_all));
}

}  // namespace fuzz
}  // namespace docdb
}  // namespace yb

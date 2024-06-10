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

#include "yb/dockv/key_bytes.h"

#include <cstdint>
#include <string>
#include <vector>

#include "yb/util/logging.h"

#include "yb/common/column_id.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"

#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/value_type.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"
#include "yb/util/fast_varint.h"
#include "yb/util/monotime.h"
#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::dockv {

void AppendDocHybridTime(const DocHybridTime& doc_ht, KeyBytes* key) {
  key->AppendKeyEntryType(KeyEntryType::kHybridTime);
  doc_ht.AppendEncodedInDocDbFormat(key->mutable_data());
}

void AppendHash(uint16_t hash, KeyBytes* key) {
  key->AppendKeyEntryType(KeyEntryType::kUInt16Hash);
  key->AppendUInt16(hash);
}

void KeyBytes::AppendUInt64AsVarInt(uint64_t value) {
  unsigned char buf[kMaxVarIntBufferSize];
  AppendRawBytes(Slice(buf, FastEncodeUnsignedVarInt(value, buf)));
}

void KeyBytes::AppendColumnId(ColumnId column_id) {
  FastAppendSignedVarIntToBuffer(column_id.rep(), &data_);
}

void KeyBytes::AppendKeyEntryType(KeyEntryType value_type) {
  data_.push_back(static_cast<char>(value_type));
}

void KeyBytes::AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType value_type) {
  if (data_.empty() || data_.back() != KeyEntryTypeAsChar::kGroupEnd) {
    AppendKeyEntryType(value_type);
    AppendKeyEntryType(KeyEntryType::kGroupEnd);
  } else {
    data_.back() = static_cast<char>(value_type);
    data_.push_back(KeyEntryTypeAsChar::kGroupEnd);
  }
}

void KeyBytes::AppendHybridTime(const DocHybridTime& hybrid_time) {
  hybrid_time.AppendEncodedInDocDbFormat(&data_);
}

void KeyBytes::RemoveKeyEntryTypeSuffix(KeyEntryType value_type) {
  CHECK_GE(data_.size(), sizeof(char));
  CHECK_EQ(data_.back(), static_cast<char>(value_type));
  data_.pop_back();
}

std::string KeyBytes::ToString() const {
  return FormatSliceAsStr(data_.AsSlice());
}

void KeyBytes::AppendString(const std::string& raw_string) {
  ZeroEncodeAndAppendStrToKey(raw_string, &data_);
}

void KeyBytes::AppendDescendingString(const std::string &raw_string) {
  ComplementZeroEncodeAndAppendStrToKey(raw_string, &data_);
}

void KeyBytes::AppendUInt64(uint64_t x) {
  AppendUInt64ToKey(x, &data_);
}

void KeyBytes::AppendDescendingUInt64(uint64_t x) {
  AppendUInt64ToKey(~x, &data_);
}

void KeyBytes::AppendUInt32(uint32_t x) {
  AppendUInt32ToKey(x, &data_);
}

void KeyBytes::AppendDescendingUInt32(uint32_t x) {
  AppendUInt32ToKey(~x, &data_);
}

void KeyBytes::AppendUInt16(uint16_t x) {
  AppendUInt16ToKey(x, &data_);
}

void KeyBytes::AppendGroupEnd() {
  AppendKeyEntryType(KeyEntryType::kGroupEnd);
}

void KeyBytes::Truncate(size_t new_size) {
  DCHECK_LE(new_size, data_.size());
  data_.Truncate(new_size);
}

void KeyBytes::RemoveLastByte() {
  DCHECK(!data_.empty());
  data_.pop_back();
}

}  // namespace yb::dockv

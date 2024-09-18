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

#include <optional>

#include <boost/container/small_vector.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"
#include "yb/common/value.pb.h"

#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/schema_packing.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/algorithm_util.h"
#include "yb/util/kv_util.h"

namespace yb {

class WriteBuffer;

}

namespace yb::dockv {

using PgValueDatum = size_t;

class PgValue {
 public:
  PgValue() = default;
  explicit PgValue(PgValueDatum value) : value_(value) {}

  int8_t int8_value() const;
  int16_t int16_value() const;
  int32_t int32_value() const;
  uint32_t uint32_value() const;
  int64_t int64_value() const;
  uint64_t uint64_value() const;
  float float_value() const;
  double double_value() const;
  bool bool_value() const;
  Slice binary_value() const;
  Slice string_value() const;

  Slice decimal_value() const {
    return string_value();
  }

  QLValuePB ToQLValuePB(DataType data_type) const;

  void AppendTo(DataType data_type, WriteBuffer* out) const;
  void AppendTo(DataType data_type, ValueBuffer* out) const;

 private:
  template <class Buffer>
  void DoAppendTo(DataType data_type, Buffer* out) const;

  Slice Vardata() const;
  Slice VardataWithLen() const;

  PgValueDatum value_;
};

struct PgWireEncoderEntry;

using PgWireEncoder = void(*)(const PgTableRow&, WriteBuffer*, const PgWireEncoderEntry*);

struct PgWireEncoderEntry {
  PgWireEncoder encoder;
  size_t data;

  void Invoke(const PgTableRow& row, WriteBuffer* buffer) const {
    encoder(row, buffer, this);
  }
};

class PgTableRow {
 public:
  explicit PgTableRow(std::reference_wrapper<const ReaderProjection> projection);

  bool Exists() const {
    return true;
  }

  bool IsEmpty() const;
  std::string ToString() const;

  std::optional<PgValue> GetValueByIndex(size_t index) const;
  PgWireEncoderEntry GetEncoder(size_t index, bool last) const;

  std::optional<PgValue> GetValueByColumnId(ColumnId column_id) const {
    return GetValueByColumnId(column_id.rep());
  }

  std::optional<PgValue> GetValueByColumnId(ColumnIdRep column_id) const;

  void Reset();

  Status SetNullOrMissingResult(const Schema& schema);
  void SetNull(size_t column_idx);

  Status DecodeValue(size_t column_idx, PackedValueV1 value);
  Status DecodeValue(size_t column_idx, PackedValueV2 value);

  bool IsNull(size_t index) const {
    return is_null_[index];
  }

  PgValueDatum GetPrimitiveDatum(size_t index) const {
    return values_[index];
  }

  Slice GetVarlenSlice(size_t index) const {
    const auto data = pointer_cast<const char*>(buffer_.data()) + values_[index];
    const auto len = BigEndian::Load64(data);
    return Slice(data, len + 8);
  }

  Status SetValue(ColumnId column_id, const QLValuePB& value);

  Status SetValueByColumnIdx(size_t idx, const QLValuePB& value);

  const ReaderProjection& projection() const {
    return *projection_;
  }

  QLValuePB GetQLValuePB(ColumnIdRep column_id) const;

  PgValue TrimString(size_t idx, size_t skip_prefix, size_t new_len);

  void SetDatum(size_t column_idx, PgValueDatum datum) {
    is_null_[column_idx] = false;
    values_[column_idx] = datum;
  }

  Result<const char*> DecodeComparableString(
      size_t column_idx, const char* input, const char* end, bool append_zero,
      SortOrder sort_order);
  void SetBinary(size_t column_idx, Slice value, bool append_zero);

  static PackedColumnDecoderEntry GetPackedColumnDecoderV1(
      bool last, DataType data_type, ssize_t packed_index);

  static PackedColumnDecoderEntry GetPackedColumnDecoderV2(
      bool last, DataType data_type, ssize_t packed_index);

  static PackedColumnDecoderEntry GetPackedColumnSkipperV2(
      bool last, bool skip_projection_column, DataType data_type, ssize_t packed_index);

 private:
  PgValueDatum GetDatum(size_t idx) const;

  const ReaderProjection* projection_;
  boost::container::small_vector<bool, 0x10> is_null_;
  boost::container::small_vector<PgValueDatum, 0x10> values_;
  ValueBuffer buffer_;
};

template <bool kLast>
void CallNextEncoder(const PgTableRow& row, WriteBuffer* buffer, const PgWireEncoderEntry* chain) {
  if (kLast) {
    return;
  }
  (++chain)->Invoke(row, buffer);
}

}  // namespace yb::dockv

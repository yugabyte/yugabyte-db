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

#include "yb/dockv/pg_key_decoder.h"

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/util/algorithm_util.h"
#include "yb/util/decimal.h"
#include "yb/util/logging.h"

namespace yb::dockv {

namespace {

template <SortOrder kSortOrder, bool kAppendZero>
class StringDecoder {
 public:
  Result<const char*> Decode(
      const char* input, const char* end, PgTableRow* row, size_t index) const {
    return row->DecodeComparableString(index, input, end, kAppendZero, kSortOrder);
  }

  Result<const char*> Skip(const char* input, const char* end) const {
    return kSortOrder == SortOrder::kAscending
        ? SkipZeroEncodedStr(input, end)
        : SkipComplementZeroEncodedStr(input, end);
  }
};

template <SortOrder kSortOrder>
class DecimalDecoder {
 public:
  static constexpr size_t kMinSize = 0;

  Result<const char*> Decode(
      const char* input, const char* end, PgTableRow* row, size_t index) const {
    util::Decimal decimal;
    size_t num_decoded_bytes = 0;
    RETURN_NOT_OK(decimal.DecodeFromComparable(Slice(input, end), &num_decoded_bytes));

    if (kSortOrder != SortOrder::kAscending) {
      decimal.Negate();
    }

    row->SetBinary(index, decimal.EncodeToComparable(), true);
    return input + num_decoded_bytes;
  }

  Result<const char*> Skip(const char* input, const char* end) const {
    util::Decimal decimal;
    size_t num_decoded_bytes = 0;
    RETURN_NOT_OK(decimal.DecodeFromComparable(Slice(input, end), &num_decoded_bytes));
    return input + num_decoded_bytes;
  }
};

template <SortOrder kSortOrder, class Value>
auto ApplySortOrder(Value value) {
  return kSortOrder == SortOrder::kAscending ? value : ~value;
}

template <SortOrder kSortOrder>
auto DecodePrimitive(const char* input, int32_t*) {
  return bit_cast<uint32_t>(ApplySortOrder<kSortOrder>(util::DecodeInt32FromKey(input)));
}

template <SortOrder kSortOrder>
int64_t DecodePrimitive(const char* input, int64_t*) {
  return ApplySortOrder<kSortOrder>(util::DecodeInt64FromKey(input));
}

template <SortOrder kSortOrder>
uint32_t DecodePrimitive(const char* input, uint32_t*) {
  return ApplySortOrder<kSortOrder>(BigEndian::Load32(input));
}

template <SortOrder kSortOrder>
uint64_t DecodePrimitive(const char* input, uint64_t*) {
  return ApplySortOrder<kSortOrder>(BigEndian::Load64(input));
}

template <SortOrder kSortOrder>
auto DecodePrimitive(const char* input, float*) {
  return bit_cast<uint32_t>(util::DecodeFloatFromKey(input, kSortOrder != SortOrder::kAscending));
}

template <SortOrder kSortOrder>
auto DecodePrimitive(const char* input, double*) {
  return bit_cast<uint64_t>(util::DecodeDoubleFromKey(input, kSortOrder != SortOrder::kAscending));
}

template <class Value, SortOrder kSortOrder>
class PrimitiveDecoder {
 public:
  static constexpr size_t kMinSize = sizeof(Value);

  const char* Decode(const char* input, const char* end, PgTableRow* row, size_t index) const {
    row->SetDatum(index, DecodePrimitive<kSortOrder>(input, static_cast<Value*>(nullptr)));
    return input + sizeof(Value);
  }

  const char* Skip(const char* input, const char* end) const {
    return input + sizeof(Value);
  }
};

template <bool kMatchedId, bool kLastColumn>
UnsafeStatus CallNextDecoder(
    const char* input, const char* end, PgTableRow* row, size_t index, void*const* chain) {
  if (kLastColumn) {
    return UnsafeStatus();
  }
  if (kMatchedId) {
    ++index;
  }
  auto next_decoder = reinterpret_cast<PgKeyColumnDecoder>(*chain);
  return next_decoder(input, end, row, index, ++chain);
}

template <bool kMatchedId, bool kLastColumn, KeyEntryType kEntryType>
UnsafeStatus HandleDifferentEntryType(
    const char* input, const char* end, PgTableRow* row, size_t index, void*const* chain,
    KeyEntryType entry_type) {
  if (entry_type == KeyEntryType::kNullLow || entry_type == KeyEntryType::kNullHigh) {
    if (kMatchedId) {
      row->SetNull(index);
    }
    return CallNextDecoder<kMatchedId, kLastColumn>(input, end, row, index, chain);
  }
  return STATUS_FORMAT(
      Corruption, "Wrong key entry type $0 expected but $1 found", kEntryType, entry_type)
      .UnsafeRelease();
}

UnsafeStatus BadGroupEnd(char ch) {
  return STATUS_FORMAT(
      Corruption, "Group end expected, but $0 found", static_cast<dockv::KeyEntryType>(ch))
      .UnsafeRelease();
}

#define CONSUME_GROUP_END() \
  do { \
    if (kConsumeGroupEnd) { \
      if (*input != dockv::KeyEntryTypeAsChar::kGroupEnd) { \
        return BadGroupEnd(*input); \
      } \
      ++input; \
    } \
  } while(false)

bool IsOk(const char* input) {
  return true;
}

UnsafeStatus ExtractStatus(const char** input) {
  return UnsafeStatus();
}

const char* ExtractValue(const char* input) {
  return input;
}

bool IsOk(const Result<const char*>& result) {
  return result.ok();
}

UnsafeStatus ExtractStatus(Result<const char*>* input) {
  return std::move(input->status()).UnsafeRelease();
}

const char* ExtractValue(const Result<const char*>& input) {
  return *input;
}

#define PERFORM_DECODE() \
  do { \
    auto decode_result = kMatchedId \
        ? decoder.Decode(input, end, row, index) : decoder.Skip(input, end); \
    if (PREDICT_FALSE(!IsOk(decode_result))) { \
      return ExtractStatus(&decode_result); \
    } \
    input = ExtractValue(decode_result); \
  } while (false)

template <bool kConsumeGroupEnd, bool kMatchedId, bool kLastColumn, SortOrder kSortOrder,
          KeyEntryType kEntryType, class Decoder>
UnsafeStatus DecodeColumn(
    const char* input, const char* end, PgTableRow* row, size_t index, void*const* chain) {
  CONSUME_GROUP_END();
  auto entry_type = static_cast<KeyEntryType>(*input++);
  if (PREDICT_FALSE(entry_type != kEntryType)) {
    return HandleDifferentEntryType<kMatchedId, kLastColumn, kEntryType>(
        input, end, row, index, chain, entry_type);
  }
  Decoder decoder;
  PERFORM_DECODE();
  if (PREDICT_FALSE(input > end)) {
    return STATUS_FORMAT(
        Corruption, "Not enough bytes to decode $0", kEntryType).UnsafeRelease();
  }
  return CallNextDecoder<kMatchedId, kLastColumn>(input, end, row, index, chain);
}

template <bool kConsumeGroupEnd, bool kMatchedId, bool kLastColumn, SortOrder kSortOrder>
UnsafeStatus DecodeBoolColumn(
    const char* input, const char* end, PgTableRow* row, size_t index, void*const* chain) {
  CONSUME_GROUP_END();
  auto entry_type = static_cast<KeyEntryType>(*input++);
  constexpr auto kTrue = kSortOrder == SortOrder::kAscending
      ? KeyEntryType::kTrue : KeyEntryType::kTrueDescending;
  constexpr auto kFalse = kSortOrder == SortOrder::kAscending
      ? KeyEntryType::kFalse : KeyEntryType::kFalseDescending;
  if (entry_type == kTrue) {
    if (kMatchedId) {
      row->SetDatum(index, 1);
    }
  } else if (entry_type == kFalse) {
    if (kMatchedId) {
      row->SetDatum(index, 0);
    }
  } else if (entry_type == KeyEntryType::kNullLow || entry_type == KeyEntryType::kNullHigh) {
    if (kMatchedId) {
      row->SetNull(index);
    }
  } else {
    return STATUS_FORMAT(
        Corruption, "Wrong key entry type $0 for bool column", entry_type).UnsafeRelease();
  }
  return CallNextDecoder<kMatchedId, kLastColumn>(input, end, row, index, chain);
}

template <bool kConsumeGroupEnd, bool kMatchedId, bool kLastColumn, SortOrder kSortOrder,
          bool kAppendZero>
UnsafeStatus DecodeStringColumn(
    const char* input, const char* end, PgTableRow* row, size_t index, void*const* chain) {
  CONSUME_GROUP_END();
  auto entry_type = static_cast<KeyEntryType>(*input++);
  constexpr auto kRegularString = kSortOrder == SortOrder::kAscending
      ? KeyEntryType::kString : KeyEntryType::kStringDescending;
  constexpr auto kCollString = !kAppendZero
      ? kRegularString
      : kSortOrder == SortOrder::kAscending
          ? KeyEntryType::kCollString : KeyEntryType::kCollStringDescending;
  if (PREDICT_FALSE(entry_type != kRegularString && entry_type != kCollString)) {
    return HandleDifferentEntryType<kMatchedId, kLastColumn, kRegularString>(
        input, end, row, index, chain, entry_type);
  }
  StringDecoder<kSortOrder, kAppendZero> decoder;
  PERFORM_DECODE();
  return CallNextDecoder<kMatchedId, kLastColumn>(input, end, row, index, chain);
}

template <bool kConsumeGroupEnd, bool kMatchedId, bool kLastColumn, SortOrder kSortOrder>
struct DecodeColumnFactory {
  template <KeyEntryType kAscEntryType, KeyEntryType kDescEntryType, class Decoder>
  static PgKeyColumnDecoder Apply() {
    return DecodeColumn<
        kConsumeGroupEnd, kMatchedId, kLastColumn, kSortOrder,
        kSortOrder == SortOrder::kAscending ? kAscEntryType : kDescEntryType, Decoder>;
  }

  static PgKeyColumnDecoder ApplyBoolColumn() {
    return DecodeBoolColumn<kConsumeGroupEnd, kMatchedId, kLastColumn, kSortOrder>;
  }

  template <bool kAppendZero>
  static PgKeyColumnDecoder ApplyStringColumn() {
    return DecodeStringColumn<kConsumeGroupEnd, kMatchedId, kLastColumn, kSortOrder, kAppendZero>;
  }
};

template <bool kConsumeGroupEnd, bool kMatchedId, bool kLastColumn, SortOrder kSortOrder>
PgKeyColumnDecoder GetDecoder5(const ColumnSchema& column) {
  using Factory = DecodeColumnFactory<kConsumeGroupEnd, kMatchedId, kLastColumn, kSortOrder>;
  auto data_type = column.type()->main();
  switch (data_type) {
    case DataType::BINARY:
      return Factory::template ApplyStringColumn<false>();
    case DataType::STRING:
      return Factory::template ApplyStringColumn<true>();
    case DataType::DECIMAL:
      return Factory::template Apply<
          KeyEntryType::kDecimal, KeyEntryType::kDecimalDescending,
          DecimalDecoder<kSortOrder>>();
    case DataType::INT8: [[fallthrough]];
    case DataType::INT16: [[fallthrough]];
    case DataType::INT32:
      return Factory::template Apply<
          KeyEntryType::kInt32, KeyEntryType::kInt32Descending,
          PrimitiveDecoder<int32_t, kSortOrder>>();
    case DataType::INT64:
      return Factory::template Apply<
          KeyEntryType::kInt64, KeyEntryType::kInt64Descending,
          PrimitiveDecoder<int64_t, kSortOrder>>();
    case DataType::UINT32:
      return Factory::template Apply<
          KeyEntryType::kUInt32, KeyEntryType::kUInt32Descending,
          PrimitiveDecoder<uint32_t, kSortOrder>>();
    case DataType::UINT64:
      return Factory::template Apply<
          KeyEntryType::kUInt64, KeyEntryType::kUInt64Descending,
          PrimitiveDecoder<uint64_t, kSortOrder>>();
    case DataType::BOOL:
      return Factory::ApplyBoolColumn();
    case DataType::FLOAT:
      return Factory::template Apply<
          KeyEntryType::kFloat, KeyEntryType::kFloatDescending,
          PrimitiveDecoder<float, kSortOrder>>();
    case DataType::DOUBLE:
      return Factory::template Apply<
          KeyEntryType::kDouble, KeyEntryType::kDoubleDescending,
          PrimitiveDecoder<double, kSortOrder>>();
    default:
      break;
  }

  LOG(FATAL) << "Data type not supported: " << data_type;
  return nullptr;
}

template <bool kConsumeGroupEnd, bool kMatchedId, bool kLastColumn>
PgKeyColumnDecoder GetDecoder4(const ColumnSchema& column) {
  auto sorting_type = column.sorting_type();
  if (sorting_type == SortingType::kDescending ||
      sorting_type == SortingType::kDescendingNullsLast) {
    return GetDecoder5<kConsumeGroupEnd, kMatchedId, kLastColumn, SortOrder::kDescending>(column);
  } else {
    return GetDecoder5<kConsumeGroupEnd, kMatchedId, kLastColumn, SortOrder::kAscending>(column);
  }
}

template <bool kConsumeGroupEnd, bool kMatchedId>
PgKeyColumnDecoder GetDecoder3(const ColumnSchema& column, bool last_column) {
  if (last_column) {
    return GetDecoder4<kConsumeGroupEnd, kMatchedId, true>(column);
  } else {
    return GetDecoder4<kConsumeGroupEnd, kMatchedId, false>(column);
  }
}

template <bool kConsumeGroupEnd>
PgKeyColumnDecoder GetDecoder2(const ColumnSchema& column, bool matched_id, bool last_column) {
  if (matched_id) {
    return GetDecoder3<kConsumeGroupEnd, true>(column, last_column);
  } else {
    return GetDecoder3<kConsumeGroupEnd, false>(column, last_column);
  }
}

PgKeyColumnDecoder GetDecoder(
    const ColumnSchema& column, bool consume_group_end, bool matched_id, bool last_column) {
  if (consume_group_end) {
    return GetDecoder2<true>(column, matched_id, last_column);
  } else {
    return GetDecoder2<false>(column, matched_id, last_column);
  }
}

UnsafeStatus NopDecoder(const char*, const char*, PgTableRow*, size_t, void*const*) {
  return UnsafeStatus();
}

UnsafeStatus UpdateSlice(
  const char* input, const char* end, PgTableRow* row, size_t index, void*const* chain) {
  Slice* slice = static_cast<Slice*>(*chain);
  *slice = Slice(input, end);
  return UnsafeStatus();
}

} // namespace

PgKeyDecoder::PgKeyDecoder(const Schema& schema, const ReaderProjection& projection) {
  if (projection.num_key_columns == 0) {
    decoders_.push_back(NopDecoder);
    return;
  }
  size_t out_idx = 0;
  decoders_.reserve(schema.num_key_columns());
  for (size_t k = 0;; ++k) {
    auto matched_id = schema.column_id(k) == projection.columns[out_idx].id;
    auto last_column = matched_id && ++out_idx >= projection.num_key_columns;
    decoders_.push_back(GetDecoder(
        schema.columns()[k], k && k == schema.num_hash_key_columns(), matched_id, last_column));
    if (last_column) {
      break;
    }
  }
}

PgKeyDecoder::~PgKeyDecoder() = default;

Status PgKeyDecoder::Decode(Slice key, PgTableRow* out) const {
  auto* decoders = decoders_.data();
  return Status(
      (*decoders)(key.cdata(), key.cend(), out, 0, reinterpret_cast<void*const*>(decoders + 1)));
}

Status PgKeyDecoder::DecodeEntry(
    Slice* key, const ColumnSchema& column, PgTableRow* out, size_t index) {
  auto decoder = GetDecoder(column, false, true, false);
  void* chain[] = {
    reinterpret_cast<void*>(UpdateSlice),
    key,
  };
  return Status(decoder(key->cdata(), key->cend(), out, index, chain));
}

}  // namespace yb::dockv

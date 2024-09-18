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

#include "yb/dockv/schema_packing.h"

#include "yb/common/column_id.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"

#include "yb/dockv/dockv.pb.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/packed_value.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/reader_projection.h"
#include "yb/dockv/value_packing.h"
#include "yb/dockv/value_packing_v2.h"

#include "yb/gutil/casts.h"

#include "yb/util/coding_consts.h"
#include "yb/util/fast_varint.h"
#include "yb/util/flags/flag_tags.h"

DEFINE_test_flag(bool, dcheck_for_missing_schema_packing, true,
                 "Whether we use check failure for missing schema packing in debug builds");

namespace yb::dockv {

namespace {

bool IsVarlenColumn(TableType table_type, const ColumnSchema& column_schema) {
  // CQL columns could have individual TTL.
  return table_type == TableType::YQL_TABLE_TYPE ||
         column_schema.is_nullable() || column_schema.type_info()->var_length();
}

size_t EncodedColumnSize(const ColumnSchema& column_schema) {
  const auto& type_info = *column_schema.type_info();
  switch (type_info.type) {
    case DataType::BOOL:
      // Boolean values are encoded as value type only.
      return 1;
    case DataType::INT8: [[fallthrough]];
    case DataType::INT16:
      // Encoded as int32. I.e., 1 byte value type + 4 bytes for int32 itself.
      return 5;
    default:
      // Other fixed length columns are encoded as 1 byte value type + value body.
      return 1 + type_info.size;
  }
}

// Visitor used by PackedRowDecoderV2::DoGetValue.
struct DoGetValueVisitor {
  const uint8_t* data;

  Slice Binary() const {
    auto [len, start] = DecodeFieldLength(data);
    return Slice(start, len);
  }

  Slice Decimal() const {
    return Binary();
  }

  Slice String() const {
    return Binary();
  }

  template <class T>
  Slice Primitive() const {
    return Slice(data, sizeof(T));
  }
};

UnsafeStatus NopRouter(
    PackedColumnRouterData* data,
    const uint8_t* value, size_t projection_index,
    const PackedColumnDecoderEntry* chain) {
  return UnsafeStatus();
}

} // namespace

struct PackedColumnRouterData {
  void* context;
};

struct PackedRowScope final {
  uint64_t token = 0;
  const EncodedDocHybridTime* hybrid_time = nullptr;

  inline bool IsValid(uint64_t token_) const {
    return token == token_ && hybrid_time != nullptr;
  }

  inline void Reset() {
    ++token;
    hybrid_time = nullptr;
  }

  std::string ToString() const {
    return hybrid_time ? YB_STRUCT_TO_STRING(token, *hybrid_time) : "{ <invalid> }";
  }
};

struct PackedColumnEntry {
  inline bool HasUpdate(const PackedRowScope& row_scope) const {
    return row_scope.IsValid(row_token) && (update_time > *(row_scope.hybrid_time));
  }

  inline bool HasUpdate(uint64_t row_token_, const EncodedDocHybridTime& ht_time) const {
    return (row_token == row_token_) && (update_time > ht_time);
  }

  inline const PackedColumnDecoderEntry& GetDecoder(const PackedRowScope& row_scope) const {
    return HasUpdate(row_scope) ? skip_decoder : base_decoder;
  }

  inline void SetDecoders(PackedColumnDecoderEntry&& base, PackedColumnDecoderEntry&& skip) {
    base_decoder = std::move(base);
    skip_decoder = std::move(skip);
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(row_token, update_time);
  }

 private:
  uint64_t row_token = 0;
  EncodedDocHybridTime update_time;
  PackedColumnDecoderEntry base_decoder;
  PackedColumnDecoderEntry skip_decoder;

  friend class PackedRowColumnUpdateTracker;
};

namespace {

template <class Base>
struct PackedRowScopeDecorator : public Base {
  const PackedRowScope& row_scope;
};

using PackedColumnTrackingRouterData    = PackedRowScopeDecorator<PackedColumnRouterData>;
using PackedColumnTrackingDecoderDataV1 = PackedRowScopeDecorator<PackedColumnDecoderDataV1>;
using PackedColumnTrackingDecoderDataV2 = PackedRowScopeDecorator<PackedColumnDecoderDataV2>;


template <bool kTrackingChain>
auto CreateDecoderDataV1(
    PackedColumnRouterData* router_data, const auto& schema_packing, const uint8_t* value) {
  if constexpr (!kTrackingChain) {
    return PackedColumnDecoderDataV1 {
        .decoder = PackedRowDecoderV1(schema_packing, value),
        .context = router_data->context,
    };
  } else {
    return PackedColumnTrackingDecoderDataV1 {
        { { schema_packing, value }, router_data->context },
        static_cast<PackedColumnTrackingRouterData*>(router_data)->row_scope
    };
  }
}

template <bool kTrackingChain>
auto CreateDecoderDataV2(PackedColumnRouterData* router_data, const uint8_t* header) {
  if constexpr (!kTrackingChain) {
    return PackedColumnDecoderDataV2 {
        .header = header,
        .context = router_data->context
    };
  } else {
    return PackedColumnTrackingDecoderDataV2 {
        { header, router_data->context },
        static_cast<PackedColumnTrackingRouterData*>(router_data)->row_scope
    };
  }
}

UnsafeStatus PackedColumnTrackingDecoderV1(
    PackedColumnDecoderDataV1* data, size_t projection_index,
    const dockv::PackedColumnDecoderArgsUnion& decoder_args,
    const PackedColumnDecoderEntry* proxy_chain) {
  auto* row_data = static_cast<PackedColumnTrackingDecoderDataV1*>(data);
  VLOG_WITH_FUNC(4)
      << "Triggering proxy decoder"
      << ": projection_index = " << projection_index
      << ", column update = " << decoder_args.column->ToString()
      << ", packed row scope = " << row_data->row_scope.ToString();
  const auto& decoder = decoder_args.column->GetDecoder(row_data->row_scope);
  return decoder.decoder.v1(data, projection_index, decoder.decoder_args, proxy_chain);
}

template <bool kLast>
UnsafeStatus PackedColumnSkipDecoderV1(
    PackedColumnDecoderDataV1* data, size_t projection_index,
    const dockv::PackedColumnDecoderArgsUnion& decoder_args,
    const PackedColumnDecoderEntry* proxy_chain) {
  DCHECK(decoder_args.column == nullptr);
  VLOG_WITH_FUNC(4)
      << "Triggering packed column skip decoder"
      << ": projection_index = " << projection_index;
  return CallNextDecoderV1<kLast>(data, projection_index, proxy_chain);
}

inline PackedColumnDecoderV1 MakePackedColumnSkipDecoderV1(bool last) {
  return last ? &PackedColumnSkipDecoderV1<true> : &PackedColumnSkipDecoderV1<false>;
}

template <bool kCheckNull>
UnsafeStatus PackedColumnTrackingDecoderV2(
    PackedColumnDecoderDataV2* data, const uint8_t* body, size_t projection_index,
    const dockv::PackedColumnDecoderArgsUnion& decoder_args,
    const PackedColumnDecoderEntry* proxy_chain) {
  auto* row_data = static_cast<PackedColumnTrackingDecoderDataV2*>(data);
  VLOG_WITH_FUNC(4)
      << "Triggering proxy decoder"
      << ": projection_index = " << projection_index
      << ", column update = " << decoder_args.column->ToString()
      << ", packed row scope = " << row_data->row_scope.ToString();
  const auto& decoder = decoder_args.column->GetDecoder(row_data->row_scope);
  if constexpr (kCheckNull) {
    return decoder.decoder.v2.with_nulls(
        data, body, projection_index, decoder.decoder_args, proxy_chain);
  } else {
    return decoder.decoder.v2.no_nulls(
        data, body, projection_index, decoder.decoder_args, proxy_chain);
  }
}

inline PackedColumnDecodersV2 MakePackedColumnTrackingDecoderV2() {
  return PackedColumnDecodersV2 {
    .with_nulls = &PackedColumnTrackingDecoderV2<true>,
    .no_nulls   = &PackedColumnTrackingDecoderV2<false>
  };
}

template <bool kTrackingChain>
UnsafeStatus RoutePackedRowV1(
    PackedColumnRouterData* router_data,
    const uint8_t* value,
    size_t projection_index,
    const PackedColumnDecoderEntry* chain) {
  const auto& schema_packing = *(chain->decoder_args.router.v1.schema_packing);

  auto decoder_data = CreateDecoderDataV1<kTrackingChain>(router_data, schema_packing, value);
  ++chain;

  return chain->decoder.v1(&decoder_data, projection_index, chain->decoder_args, chain);
}

template <bool kTrackingChain>
UnsafeStatus RoutePackedRowV2(
    PackedColumnRouterData* router_data,
    const uint8_t* header, size_t projection_index,
    const PackedColumnDecoderEntry* chain) {
  if (*header & RowPackerV2::kHasNullsFlag) {
    ++header;
    auto body = header + chain->decoder_args.router.v2.value_offset;
    ++chain;

    auto decoder_data = CreateDecoderDataV2<kTrackingChain>(router_data, header);
    return chain->decoder.v2.with_nulls(
        &decoder_data, body, projection_index, chain->decoder_args, chain);
  }
  ++chain;
  auto decoder_data = CreateDecoderDataV2<kTrackingChain>(router_data, nullptr);
  return chain->decoder.v2.no_nulls(
      &decoder_data, header + 1, projection_index, chain->decoder_args, chain);
}

inline PackedColumnEntry* PrepareColumnEntryV1(
    PackedColumnEntry& column_entry, PackedColumnDecoderEntry decoder, bool last) {
  column_entry.SetDecoders(
      std::move(decoder),
      { .decoder = { .v1 = MakePackedColumnSkipDecoderV1(last) },
        .decoder_args = { .column = nullptr } }
  );
  return &column_entry;
}

inline PackedColumnEntry* PrepareColumnEntryV2(
    PackedColumnEntry& column_entry, PackedColumnDecoderEntry decoder,
    bool last, const SchemaPacking& schema_packing, int64_t packed_index) {
  column_entry.SetDecoders(
      std::move(decoder),
      PgTableRow::GetPackedColumnSkipperV2(
          last, /* skip_projection_column */ true,
          schema_packing.column_packing_data(packed_index).data_type, packed_index)
  );
  return &column_entry;
}

void FillDecodersV1(
    size_t index,
    size_t num_columns,
    const ReaderProjection& projection,
    const SchemaPacking& schema_packing,
    const PackedRowDecoderFactory& decoder_factory,
    PackedRowDecoder::DecoderVectorBase& decoders,
    PackedRowDecoder::TrackingDecoderVector& tracking_decoders,
    PackedRowColumnUpdateTracker* column_update_tracker) {
  // Fill router entry.
  decoders.push_back({
      .decoder = { .router = &RoutePackedRowV1</* kTrackingChain */ false> },
      .decoder_args = { .router = { .v1 = { .schema_packing = &schema_packing } } }
  });
  if (column_update_tracker) {
    tracking_decoders.push_back({
        .decoder = { .router = &RoutePackedRowV1</* kTrackingChain */ true> },
        .decoder_args = decoders.back().decoder_args
    });
  }

  // Fill decoders entries.
  VLOG_WITH_FUNC(4) <<
      "index = " << index << ", num cols = " << num_columns << ", "
      "decoders = " << decoders.size() << ", tracking_decoders = " << tracking_decoders.size();
  for (const auto last_index = num_columns - 1; index < num_columns; ++index) {
    bool last = index == last_index;
    const auto column_id = projection.columns[index].id;
    const auto packed_index = schema_packing.GetIndex(column_id);

    VLOG_WITH_FUNC(4) <<
        "processing: index = " << index << ", column_id = " << column_id << ", "
        "packed_index = " << packed_index << ", last = " << last;

    decoders.push_back(decoder_factory.GetColumnDecoderV1(index, packed_index, last));
    if (column_update_tracker) {
      tracking_decoders.push_back({
        .decoder = { .v1 = &PackedColumnTrackingDecoderV1 },
        .decoder_args = { .column = PrepareColumnEntryV1(
            column_update_tracker->GetEntry(column_id), decoders.back(), last) }
      });
    }
  }
}

void FillDecodersV2(
    size_t index,
    size_t num_columns,
    const ReaderProjection& projection,
    const SchemaPacking& schema_packing,
    const PackedRowDecoderFactory& decoder_factory,
    PackedRowDecoder::DecoderVectorBase& decoders,
    PackedRowDecoder::TrackingDecoderVector& tracking_decoders,
    PackedRowColumnUpdateTracker* column_update_tracker) {
  // Fill router entry.
  decoders.push_back({
      .decoder = { .router = &RoutePackedRowV2</* kTrackingChain */ false> },
      .decoder_args = { .router = { .v2 = { .value_offset = schema_packing.NullMaskSize() } } }
  });
  if (column_update_tracker) {
    tracking_decoders.push_back({
        .decoder = { .router = &RoutePackedRowV2</* kTrackingChain */ true> },
        .decoder_args = decoders.back().decoder_args
    });
  }

  // Fill decoders entries.
  VLOG_WITH_FUNC(4) <<
      "index = " << index << ", num cols = " << num_columns << ", "
      "decoders = " << decoders.size() << ", tracking_decoders = " << tracking_decoders.size();

  int64_t next_packed_index = 0;
  for (const auto last_index = num_columns - 1; index < num_columns; ++index) {
    bool last = index == last_index;
    const auto column_id = projection.columns[index].id;
    const auto packed_index = schema_packing.GetIndex(column_id);
    const auto is_skipped_column = packed_index == SchemaPacking::kSkippedColumnIdx;

    VLOG_WITH_FUNC(4) <<
        "processing: index = " << index << ", column_id = " << column_id << ", "
        "packed_index = " << packed_index << ", last = " << last;

    if (PREDICT_TRUE(!is_skipped_column)) {
      while (next_packed_index < packed_index) {
        auto entry = PgTableRow::GetPackedColumnSkipperV2(
            /* last */ false, /* skip_projection_column */ false,
            schema_packing.column_packing_data(next_packed_index).data_type, next_packed_index);
        ++next_packed_index;
        decoders.push_back(entry);
        if (column_update_tracker) {
          tracking_decoders.push_back(entry);
        }
      }
    }

    decoders.push_back(decoder_factory.GetColumnDecoderV2(index, packed_index, last));
    if (column_update_tracker) {
      tracking_decoders.push_back({
        .decoder = { .v2 = MakePackedColumnTrackingDecoderV2() },
        .decoder_args = {
            .column = PrepareColumnEntryV2(
                column_update_tracker->GetEntry(column_id), decoders.back(), last,
                schema_packing, packed_index) }
      });
    }

    if (PREDICT_TRUE(!is_skipped_column)) {
      ++next_packed_index;
    }
  }
}

} // namespace

ColumnPackingData ColumnPackingData::FromPB(const ColumnPackingPB& pb) {
  return ColumnPackingData {
    .id = ColumnId(pb.id()),
    .num_varlen_columns_before = pb.num_varlen_columns_before(),
    .offset_after_prev_varlen_column = pb.offset_after_prev_varlen_column(),
    .size = pb.size(),
    .nullable = pb.nullable(),
    .data_type = ToLW(pb.data_type()),
  };
}

void ColumnPackingData::ToPB(ColumnPackingPB* out) const {
  out->set_id(id.rep());
  out->set_num_varlen_columns_before(num_varlen_columns_before);
  out->set_offset_after_prev_varlen_column(offset_after_prev_varlen_column);
  out->set_size(size);
  out->set_nullable(nullable);
  out->set_data_type(yb::ToPB(data_type));
}

std::string ColumnPackingData::ToString() const {
  return YB_STRUCT_TO_STRING(
      id, num_varlen_columns_before, offset_after_prev_varlen_column, size, nullable, data_type);
}

Slice ColumnPackingData::FetchV1(const uint8_t* header, const uint8_t* body) const {
  size_t offset = num_varlen_columns_before
      ? VarLenColEndOffset(num_varlen_columns_before - 1, header) : 0;
  offset += offset_after_prev_varlen_column;
  size_t end = varlen()
      ? VarLenColEndOffset(num_varlen_columns_before, header)
      : offset + size;
  return Slice(body + offset, body + end);
}

SchemaPacking::SchemaPacking(TableType table_type, const Schema& schema) {
  varlen_columns_count_ = 0;
  size_t offset_after_prev_varlen_column = 0;
  columns_.reserve(schema.num_columns() - schema.num_key_columns());
  for (auto i = schema.num_key_columns(); i != schema.num_columns(); ++i) {
    const auto& column_schema = schema.column(i);
    auto column_id = schema.column_id(i);
    if (column_schema.is_collection() || column_schema.is_static()) {
      column_to_idx_.set(column_id.rep(), kSkippedColumnIdx);
      continue;
    }
    column_to_idx_.set(column_id.rep(), narrow_cast<int>(columns_.size()));
    bool varlen = IsVarlenColumn(table_type, column_schema);

    const auto& type_info = *column_schema.type_info();

    columns_.emplace_back(ColumnPackingData {
      .id = schema.column_id(i),
      .num_varlen_columns_before = varlen_columns_count_,
      .offset_after_prev_varlen_column = offset_after_prev_varlen_column,
      .size = varlen ? 0 : EncodedColumnSize(column_schema),
      .nullable = column_schema.is_nullable(),
      .data_type = type_info.type,
    });

    if (varlen) {
      ++varlen_columns_count_;
      offset_after_prev_varlen_column = 0;
    } else {
      offset_after_prev_varlen_column += EncodedColumnSize(column_schema);
    }
  }
}

SchemaPacking::SchemaPacking(const SchemaPackingPB& pb) : varlen_columns_count_(0) {
  columns_.reserve(pb.columns().size());
  for (const auto& entry : pb.columns()) {
    columns_.push_back(ColumnPackingData::FromPB(entry));
    column_to_idx_.set(columns_.back().id.rep(), narrow_cast<int>(columns_.size()) - 1);
    if (columns_.back().varlen()) {
      ++varlen_columns_count_;
    }
  }
  for (auto skipped_column_id : pb.skipped_column_ids()) {
    column_to_idx_.set(skipped_column_id, kSkippedColumnIdx);
  }
}

size_t VarLenColEndOffset(size_t idx, const uint8_t* data) {
  return LittleEndian::Load32(data + idx * sizeof(uint32_t));
}

size_t VarLenColEndOffset(size_t idx, Slice packed) {
  return VarLenColEndOffset(idx, packed.data());
}

bool SchemaPacking::HasDataType() const {
  return columns_.empty() || columns_[0].data_type != DataType::NULL_VALUE_TYPE;
}

bool SchemaPacking::SkippedColumn(ColumnId column_id) const {
  auto it = column_to_idx_.find(column_id);
  return it && *it == kSkippedColumnIdx;
}

void SchemaPacking::GetBounds(
    Slice packed, boost::container::small_vector_base<const uint8_t*>* bounds) const {
  bounds->clear();
  bounds->reserve(columns_.size() + 1);
  const auto prefix_len = this->prefix_len();
  size_t offset = prefix_len;
  bounds->push_back(packed.data() + offset);
  const auto* end_ptr = packed.data();
  for (const auto& column_data : columns_) {
    if (column_data.varlen()) {
      offset = prefix_len + LittleEndian::Load32(end_ptr);
      end_ptr += sizeof(uint32_t);
    } else {
      offset += column_data.size;
    }
    bounds->push_back(packed.data() + offset);
  }
}

Slice SchemaPacking::GetValue(size_t idx, Slice packed) const {
  return columns_[idx].FetchV1(packed.data(), packed.data() + prefix_len());
}

std::optional<Slice> SchemaPacking::GetValue(ColumnId column_id, Slice packed) const {
  auto index = column_to_idx_.get(column_id.rep());
  if (index == kSkippedColumnIdx) {
    return {};
  }
  return GetValue(index, packed);
}

int64_t SchemaPacking::GetIndex(ColumnId column_id) const {
  return column_to_idx_.get(column_id.rep());
}

std::string SchemaPacking::ToString() const {
  return YB_CLASS_TO_STRING(columns, varlen_columns_count);
}

void SchemaPacking::ToPB(SchemaPackingPB* out) const {
  for (const auto& column : columns_) {
    column.ToPB(out->add_columns());
  }
  column_to_idx_.ForEach([out](int key, int value) {
    if (value == kSkippedColumnIdx) {
      out->add_skipped_column_ids(key);
    }
  });
}

bool SchemaPacking::CouldPack(
    const google::protobuf::RepeatedPtrField<QLColumnValuePB>& values) const {
  if (make_unsigned(values.size()) != column_to_idx_.size()) {
    return false;
  }
  for (const auto& value : values) {
    if (!column_to_idx_.find(value.column_id())) {
      return false;
    }
  }
  return true;
}

class PackedRowColumnUpdateTracker::Impl final {
 public:
  PackedColumnEntry& GetEntry(ColumnId column_id) {
    return column_map_[column_id];
  }

  bool HasUpdates() const {
    return latest_update_ && latest_update_->HasUpdate(row_scope_);
  }

  bool HasUpdatesAfter(const EncodedDocHybridTime& ht_time) {
    return latest_update_ && latest_update_->HasUpdate(row_scope_.token, ht_time);
  }

  void Reset() {
    row_scope_.Reset();
    latest_update_ = nullptr;
  }

  const PackedRowScope& GetRowScope() const {
    return row_scope_;
  }

  void TrackRow(const EncodedDocHybridTime* row_time) {
    row_scope_.hybrid_time = row_time;
  }

  void TrackColumn(ColumnId column_id, const EncodedDocHybridTime& column_time) {
    auto& entry = column_map_[column_id];
    entry.update_time = column_time;
    entry.row_token = row_scope_.token;
    if (!latest_update_ || latest_update_->update_time < column_time) {
      latest_update_ = &entry;
    }
  }

 private:
  PackedRowScope row_scope_;
  const PackedColumnEntry* latest_update_;
  std::unordered_map<ColumnId, PackedColumnEntry> column_map_;
};

PackedRowColumnUpdateTracker::~PackedRowColumnUpdateTracker() = default;

PackedRowColumnUpdateTracker::PackedRowColumnUpdateTracker()
    : impl_(std::make_unique<Impl>()) {
}

PackedColumnEntry& PackedRowColumnUpdateTracker::GetEntry(ColumnId column_id) {
  return impl_->GetEntry(column_id);
}

bool PackedRowColumnUpdateTracker::HasUpdates() const {
  return impl_->HasUpdates();
}

bool PackedRowColumnUpdateTracker::HasUpdatesAfter(const EncodedDocHybridTime& ht_time) const {
  return impl_->HasUpdatesAfter(ht_time);
}

void PackedRowColumnUpdateTracker::Reset() {
  impl_->Reset();
}

const PackedRowScope& PackedRowColumnUpdateTracker::GetRowScope() const {
  return impl_->GetRowScope();
}

void PackedRowColumnUpdateTracker::TrackRow(const EncodedDocHybridTime* row_time) {
  impl_->TrackRow(row_time);
}

void PackedRowColumnUpdateTracker::TrackColumn(
    ColumnId column_id, const EncodedDocHybridTime& column_time) {
  impl_->TrackColumn(column_id, column_time);
}

PackedRowDecoder::PackedRowDecoder() = default;

void PackedRowDecoder::Init(
    PackedRowVersion version, const ReaderProjection& projection,
    const SchemaPacking& schema_packing, const PackedRowDecoderFactory& factory,
    const Schema& schema, PackedRowColumnUpdateTracker* column_tracker) {
  schema_packing_ = &schema_packing;
  num_key_columns_ = projection.num_key_columns;
  schema_ = &schema;

  decoders_.clear();
  tracking_decoders_.clear();

  auto index = projection.num_key_columns;
  auto num_columns = projection.columns.size();
  if (index == num_columns) {
    decoders_.push_back( {
      .decoder = { .router = &NopRouter },
      .decoder_args = { .packed_index = 0 }
    });
    return;
  }

  // The math could be not accuarate enough for Packed Row V2 as it is required to create a decoder
  // per each packed column, between the very first and the very last columns from the projection,
  // and one decoder per skipped column from the projection. But the complexity comes from the fact
  // that it is required to iterate over all the columns in the projection to understand which are
  // skipped columns and which packed columns are not mentioned in the projection, but should be
  // taken into accoount, and thus to identify what would be the real number of decoders. Hence
  // skipping that logic now, and accepting the fact some reallocations may happen. Alternatice
  // would be to reserve the size in accordance with the number of columns from schema packing.
  decoders_.reserve(num_columns - index + 1);
  if (column_tracker) {
    tracking_decoders_.reserve(decoders_.capacity());
  }

  switch (version) {
    case PackedRowVersion::kV1:
      return FillDecodersV1(
          index, num_columns, projection, schema_packing, factory,
          decoders_, tracking_decoders_, column_tracker);

    case PackedRowVersion::kV2:
      return FillDecodersV2(
          index, num_columns, projection, schema_packing, factory,
          decoders_, tracking_decoders_, column_tracker);
  }
  FATAL_INVALID_ENUM_VALUE(PackedRowVersion, version);
}

Status PackedRowDecoder::Apply(Slice value, void* context) {
  auto chain = decoders_.data();
  PackedColumnRouterData router_data {
    .context = context,
  };
  return Status(chain->decoder.router(&router_data, value.data(), num_key_columns_, chain));
}

Status PackedRowDecoder::Apply(Slice value, void* context, const PackedRowScope& row_scope) {
  PackedColumnTrackingRouterData router_data { { context }, row_scope };

  auto chain = tracking_decoders_.data();
  return Status(chain->decoder.router(&router_data, value.data(), num_key_columns_, chain));
}

SchemaPackingStorage::SchemaPackingStorage(TableType table_type) : table_type_(table_type) {}

SchemaPackingStorage::SchemaPackingStorage(
    const SchemaPackingStorage& rhs, SchemaVersion min_schema_version)
    : table_type_(rhs.table_type_) {
  for (const auto& [version, packing] : rhs.version_to_schema_packing_) {
    if (version < min_schema_version) {
      continue;
    }
    version_to_schema_packing_.emplace(version, packing);
  }
}

Result<const SchemaPacking&> SchemaPackingStorage::GetPacking(SchemaVersion schema_version) const {
  auto it = version_to_schema_packing_.find(schema_version);
  auto get_first = [](const auto& pair) { return pair.first; };
  if (it == version_to_schema_packing_.end()) {
    auto status = STATUS_FORMAT(
        NotFound, "Schema packing not found: $0, available_versions: $1",
        schema_version, CollectionToString(version_to_schema_packing_, get_first));
#ifndef NDEBUG
    if (FLAGS_TEST_dcheck_for_missing_schema_packing) {
      CHECK_OK(status);
    }
#endif
    return status;
  }
  return it->second;
}

Result<const SchemaPacking&> SchemaPackingStorage::GetPacking(Slice* packed_row) const {
  auto version = VERIFY_RESULT(FastDecodeUnsignedVarInt(packed_row));
  return GetPacking(narrow_cast<SchemaVersion>(version));
}

void SchemaPackingStorage::AddSchema(SchemaVersion version, const Schema& schema) {
  AddSchema(table_type_, version, schema, &version_to_schema_packing_);
}

void SchemaPackingStorage::AddSchema(
    TableType table_type, SchemaVersion version, const Schema& schema,
    std::unordered_map<SchemaVersion, SchemaPacking>* out) {
  auto inserted = out->emplace(std::piecewise_construct,
                               std::forward_as_tuple(version),
                               std::forward_as_tuple(table_type, schema)).second;
  LOG_IF(DFATAL, !inserted) << "Duplicate schema version: " << version << ", " << AsString(*out);
}

Status SchemaPackingStorage::LoadFromPB(
    const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas) {
  version_to_schema_packing_.clear();
  return InsertSchemas(schemas, false, OverwriteSchemaPacking::kFalse);
}

Result<SchemaVersion> SchemaPackingStorage::GetSchemaPackingVersion(
    TableType table_type,
    const Schema& schema) const {
  SchemaPacking packing(table_type, schema);
  SchemaVersion max_compatible_schema_version = 0;
  bool found = false;
  // Find the highest possible schema version for which there
  // is a match as that will be the latest schema version.
  for (const auto& [version, schema_packing] : version_to_schema_packing_)  {
    if (packing == schema_packing) {
      found = true;
      if (version > max_compatible_schema_version) {
        max_compatible_schema_version = version;
      }
    }
  }

  SCHECK(found, NotFound, "Schema packing not found: ");
  return max_compatible_schema_version;
}

Status SchemaPackingStorage::MergeWithRestored(
    SchemaVersion schema_version, const SchemaPB& schema,
    const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas,
    OverwriteSchemaPacking overwrite) {
  auto new_packings =
      VERIFY_RESULT(GetMergedSchemaPackings(schema_version, schema, schemas, overwrite));
  version_to_schema_packing_ = std::move(new_packings);
  return Status::OK();
}

Result<std::unordered_map<SchemaVersion, SchemaPacking>>
SchemaPackingStorage::GetMergedSchemaPackings(
    SchemaVersion schema_version, const SchemaPB& schema,
    const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas,
    OverwriteSchemaPacking overwrite) const {
  auto new_packings = version_to_schema_packing_;
  RETURN_NOT_OK(InsertSchemas(schemas, true, overwrite, &new_packings));
  if (overwrite) {
    new_packings.erase(schema_version);
  }
  if (!new_packings.contains(schema_version)) {
    Schema temp_schema;
    RETURN_NOT_OK(SchemaFromPB(schema, &temp_schema));
    AddSchema(table_type_, schema_version, temp_schema, &new_packings);
  }
  if (VLOG_IS_ON(2)) {
    VLOG(2) << "Schema Packing History after merging with snapshot";
    for (const auto& [version, packing] : new_packings) {
      VLOG(2) << version << " : " << packing.ToString();
    }
  }
  return new_packings;
}

Status SchemaPackingStorage::InsertSchemas(
    const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas,
    bool could_present, OverwriteSchemaPacking overwrite) {
  return InsertSchemas(schemas, could_present, overwrite, &version_to_schema_packing_);
}

Status SchemaPackingStorage::InsertSchemas(
    const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas, bool could_present,
    OverwriteSchemaPacking overwrite, std::unordered_map<SchemaVersion, SchemaPacking>* out) {
  for (const auto& entry : schemas) {
    if (overwrite) {
      out->erase(entry.schema_version());
    }
    auto inserted = out->emplace(std::piecewise_construct,
                                 std::forward_as_tuple(entry.schema_version()),
                                 std::forward_as_tuple(entry)).second;
    RSTATUS_DCHECK(
        could_present || inserted, Corruption,
        Format("Duplicate schema version: $0", entry.schema_version()));
  }
  return Status::OK();
}

void SchemaPackingStorage::ToPB(
    SchemaVersion skip_schema_version,
    google::protobuf::RepeatedPtrField<SchemaPackingPB>* out) const {
  for (const auto& version_and_packing : version_to_schema_packing_) {
    if (version_and_packing.first == skip_schema_version) {
      continue;
    }
    auto* packing_out = out->Add();
    packing_out->set_schema_version(version_and_packing.first);
    version_and_packing.second.ToPB(packing_out);
  }
}

bool SchemaPackingStorage::HasVersionBelow(SchemaVersion version) const {
  for (const auto& p : version_to_schema_packing_) {
    if (p.first < version) {
      return true;
    }
  }
  return false;
}

std::string SchemaPackingStorage::VersionsToString() const {
  std::vector<SchemaVersion> versions;
  versions.reserve(version_to_schema_packing_.size());
  for (const auto& [version, schema] : version_to_schema_packing_) {
    versions.push_back(version);
  }
  std::sort(versions.begin(), versions.end());
  return AsString(versions);
}

std::string SchemaPackingStorage::ToString() const {
  return YB_CLASS_TO_STRING(table_type, version_to_schema_packing);
}

std::optional<SchemaVersion> SchemaPackingStorage::SingleSchemaVersion() const {
  if (version_to_schema_packing_.size() != 1) {
    return std::nullopt;
  }
  return version_to_schema_packing_.begin()->first;
}

PackedRowDecoderV1::PackedRowDecoderV1(
    std::reference_wrapper<const SchemaPacking> packing, const uint8_t* data)
    : PackedRowDecoderBase(packing), header_ptr_(data), data_(data + packing_->prefix_len()) {
}

PackedValueV1 PackedRowDecoderV1::FetchValue(size_t idx) {
  const auto& column_data = packing_->column_packing_data(idx);
  size_t offset = column_data.num_varlen_columns_before
      ? VarLenColEndOffset(column_data.num_varlen_columns_before - 1, header_ptr_) : 0;
  offset += column_data.offset_after_prev_varlen_column;
  size_t end = column_data.varlen()
      ? VarLenColEndOffset(column_data.num_varlen_columns_before, header_ptr_)
      : offset + column_data.size;
  return PackedValueV1(Slice(data_ + offset, data_ + end));
}

PackedValueV1 PackedRowDecoderV1::FetchValue(ColumnId column_id) {
  auto index = packing_->GetIndex(column_id);
  return index != SchemaPacking::kSkippedColumnIdx ? FetchValue(index) : PackedValueV1::Null();
}

int64_t PackedRowDecoderV1::GetPackedIndex(ColumnId column_id) {
  return packing_->GetIndex(column_id);
}

const uint8_t* PackedRowDecoderV1::GetEnd(ColumnId column_id) {
  auto index = packing_->GetIndex(column_id);
  if (index == SchemaPacking::kSkippedColumnIdx) {
    return nullptr;
  }
  return FetchValue(index)->end();
}

PackedRowDecoderV2::PackedRowDecoderV2(
    std::reference_wrapper<const SchemaPacking> packing, const uint8_t* data)
    : PackedRowDecoderBase(packing), header_ptr_(data) {
  if (*header_ptr_ & RowPackerV2::kHasNullsFlag) {
    ++header_ptr_;
    data_ = header_ptr_ + packing_->NullMaskSize();
  } else {
    data_ = header_ptr_ + 1;
    header_ptr_ = nullptr;
  }
}

bool PackedRowDecoderV2::IsNull(const uint8_t* header, size_t idx) {
  return header[idx / 8] & (1 << (idx & 7));
}

bool PackedRowDecoderV2::IsNull(size_t idx) const {
  if (!header_ptr_) {
    return false;
  }
  return IsNull(header_ptr_, idx);
}

Slice PackedRowDecoderV2::DoGetValue(size_t idx) {
  return VisitDataType(
      packing_->column_packing_data(idx).data_type, DoGetValueVisitor { .data = data_ });
}

PackedValueV2 PackedRowDecoderV2::FetchValue(size_t idx) {
  CHECK_LE(next_idx_, idx);
  for (auto next_idx = next_idx_; next_idx < idx; ++next_idx) {
    if (IsNull(next_idx)) {
      continue;
    }

    data_ = DoGetValue(next_idx).end();
  }
  if (IsNull(idx)) {
    next_idx_ = idx + 1;
    return PackedValueV2::Null();
  }
  auto result = DoGetValue(idx);
  next_idx_ = idx + 1;
  data_ = result.end();
  return PackedValueV2(result);
}

PackedValueV2 PackedRowDecoderV2::FetchValue(ColumnId column_id) {
  auto index = packing_->GetIndex(column_id);
  return index != SchemaPacking::kSkippedColumnIdx ? FetchValue(index) : PackedValueV2::Null();
}

int64_t PackedRowDecoderV2::GetPackedIndex(ColumnId column_id) {
  return packing_->GetIndex(column_id);
}

const uint8_t* PackedRowDecoderV2::GetEnd(ColumnId column_id) {
  FetchValue(column_id);
  return data_;
}

} // namespace yb::dockv

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
    const uint8_t* value, void* context, size_t projection_index,
    const PackedColumnDecoderEntry* chain) {
  return UnsafeStatus();
}

template <PackedRowVersion kVersion, class Factory>
void FillDecoders(
    size_t index, size_t num_columns, const ReaderProjection& projection,
    const SchemaPacking& schema_packing,
    boost::container::small_vector_base<PackedColumnDecoderEntry>* decoders,
    const Factory& factory) {
  --num_columns;
  int64_t next_packed_index = 0;
  for (;;) {
    auto packed_index = schema_packing.GetIndex(projection.columns[index].id);
    bool last = index == num_columns;
    if (PREDICT_FALSE(packed_index == SchemaPacking::kSkippedColumnIdx)) {
      decoders->push_back(factory(index, packed_index, last));
    } else {
      if (kVersion == PackedRowVersion::kV2) {
        while (next_packed_index < packed_index) {
          auto entry = PgTableRow::GetPackedColumnSkipperV2(
              schema_packing.column_packing_data(next_packed_index).data_type,
              next_packed_index);
          ++next_packed_index;
          decoders->push_back(entry);
        }
      }
      decoders->push_back(factory(index, packed_index, last));
      ++next_packed_index;
    }
    ++index;
    if (last) {
      break;
    }
  }
}

UnsafeStatus RoutePackedRowV1(
    const uint8_t* value, void* context, size_t projection_index,
    const PackedColumnDecoderEntry* chain) {
  auto& schema_packing = *bit_cast<const SchemaPacking*>(chain->data);
  PackedColumnDecoderDataV1 data {
    .decoder = PackedRowDecoderV1(schema_packing, value),
    .context = context,
  };
  ++chain;
  return chain->decoder.v1(&data, projection_index, chain);
}

UnsafeStatus RoutePackedRowV2(
    const uint8_t* header, void* context, size_t projection_index,
    const PackedColumnDecoderEntry* chain) {
  if (*header & RowPackerV2::kHasNullsFlag) {
    ++header;
    auto body = header + chain->data;
    ++chain;
    return chain->decoder.v2.with_nulls(
        header, body, context, projection_index, chain);
  }
  ++chain;
  return chain->decoder.v2.no_nulls(
      nullptr, header + 1, context, projection_index, chain);
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

PackedRowDecoder::PackedRowDecoder() = default;

void PackedRowDecoder::Init(
    PackedRowVersion version, const ReaderProjection& projection,
    const SchemaPacking& schema_packing, PackedRowDecoderFactory* factory,
    const Schema& schema) {
  schema_packing_ = &schema_packing;
  num_key_columns_ = projection.num_key_columns;
  schema_ = &schema;

  decoders_.clear();
  auto index = projection.num_key_columns, num_columns = projection.columns.size();
  if (index == num_columns) {
    decoders_.push_back(PackedColumnDecoderEntry {
      .decoder = PackedColumnDecoderUnion {
        .router = &NopRouter,
      },
      .data = 0,
    });
    return;
  }

  decoders_.reserve(num_columns - index + 1);

  switch (version) {
    case PackedRowVersion::kV1:
      decoders_.push_back(PackedColumnDecoderEntry {
        .decoder = PackedColumnDecoderUnion {
          .router = &RoutePackedRowV1,
        },
        .data = bit_cast<size_t>(&schema_packing),
      });
      FillDecoders<PackedRowVersion::kV1>(
          index, num_columns, projection, schema_packing, &decoders_,
          [factory](size_t projection_index, ssize_t packed_index, bool last) {
        return factory->GetColumnDecoderV1(projection_index, packed_index, last);
      });
      return;
    case PackedRowVersion::kV2:
      decoders_.push_back(PackedColumnDecoderEntry {
        .decoder = PackedColumnDecoderUnion {
          .router = &RoutePackedRowV2,
        },
        .data = schema_packing.NullMaskSize(),
      });
      FillDecoders<PackedRowVersion::kV2>(
          index, num_columns, projection, schema_packing, &decoders_,
          [factory](size_t projection_index, ssize_t packed_index, bool last) {
        return factory->GetColumnDecoderV2(projection_index, packed_index, last);
      });
      return;
  }
  FATAL_INVALID_ENUM_VALUE(PackedRowVersion, version);
}

Status PackedRowDecoder::Apply(Slice value, void* context) {
  auto chain = decoders_.data();
  return Status(chain->decoder.router(value.data(), context, num_key_columns_, chain));
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

PackedRowDecoderBase& DecoderBase(PackedRowDecoderVariant* decoder) {
  return *std::visit([](auto& decoder) {
    PackedRowDecoderBase* result = &decoder;
    return result;
  }, *decoder);
}

} // namespace yb::dockv

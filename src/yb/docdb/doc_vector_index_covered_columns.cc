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

#include "yb/docdb/doc_vector_index_covered_columns.h"

#include <algorithm>

#include "yb/common/common.pb.h"

#include "yb/dockv/packed_value.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/util/fast_varint.h"
#include "yb/util/status_format.h"

namespace yb::docdb {

DocVectorIndexCoveredColumns::DocVectorIndexCoveredColumns(Schema schema)
    : schema_(std::move(schema)),
      packing_(TableType::PGSQL_TABLE_TYPE, schema_),
      column_ids_(schema_.column_ids()) {
}

Result<DocVectorIndexCoveredColumnsPtr> DocVectorIndexCoveredColumns::Make(
    const Schema& indexed_table_schema, const PgVectorIdxOptionsPB& options) {
  if (options.covered_column_ids().empty()) {
    return nullptr;
  }
  std::vector<ColumnId> ids;
  ids.reserve(options.covered_column_ids().size());
  for (auto id : options.covered_column_ids()) {
    ids.emplace_back(id);
  }
  // The packing keeps the schema order, so sorting makes it independent of the INCLUDE order.
  std::ranges::sort(ids);
  SCHECK(
      std::ranges::adjacent_find(ids) == ids.end(), InvalidArgument,
      Format("Duplicate covered columns: $0", options.covered_column_ids()));

  std::vector<ColumnSchema> columns;
  columns.reserve(ids.size());
  for (auto id : ids) {
    const auto& column = VERIFY_RESULT_REF(indexed_table_schema.column_by_id(id));
    SCHECK(
        !indexed_table_schema.is_key_column(id), InvalidArgument,
        Format("Key column $0 is covered by the ybctid", column.name()));
    SCHECK(
        !column.is_vector(), NotSupported,
        Format("Vector column $0 cannot be covered", column.name()));
    columns.push_back(column);
  }
  return std::make_shared<DocVectorIndexCoveredColumns>(Schema(columns, ids));
}

bool DocVectorIndexCoveredColumns::Contains(ColumnId column_id) const {
  return std::ranges::binary_search(column_ids_, column_id);
}

dockv::RowPackerV2 DocVectorIndexCoveredColumns::MakePacker() const {
  return dockv::RowPackerV2(
      /* version= */ 0, packing_, std::numeric_limits<ssize_t>::max(),
      dockv::ValueControlFields());
}

Result<Slice> DocVectorIndexCoveredColumns::Complete(dockv::RowPackerV2& packer) {
  auto packed = VERIFY_RESULT(packer.Complete());
  // Strip the packed row type and the schema version, both fixed.
  RSTATUS_DCHECK(
      packed.TryConsumeByte(dockv::ValueEntryTypeAsChar::kPackedRowV2), Corruption,
      Format("Wrong packed covered columns: $0", packed.ToDebugHexString()));
  RETURN_NOT_OK(FastDecodeUnsignedVarInt(&packed));
  return packed;
}

Status DocVectorIndexCoveredColumns::Decode(Slice covered, dockv::PgTableRow& row) const {
  RSTATUS_DCHECK(!covered.empty(), Corruption, "Empty covered columns");
  dockv::PackedRowDecoderV2 decoder(packing_, covered.data());
  const auto& projection = row.projection();
  // The decoder reads the columns in packing order.
  for (size_t i = 0; i != packing_.columns(); ++i) {
    auto column_id = packing_.column_packing_data(i).id;
    auto idx = projection.ColumnIdxById(column_id);
    if (idx == dockv::ReaderProjection::kNotFoundIndex) {
      continue;
    }
    RETURN_NOT_OK(row.DecodeValue(idx, decoder.FetchValue(i)));
  }
  return Status::OK();
}

} // namespace yb::docdb

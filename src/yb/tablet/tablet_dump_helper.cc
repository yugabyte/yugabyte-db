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

#include "yb/tablet/tablet_dump_helper.h"

#include "yb/client/client.h"
#include "yb/common/colocated_util.h"
#include "yb/docdb/docdb_util.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"
#include "yb/qlexpr/ql_expr.h"

namespace yb::tablet {

namespace {
void XorHash(const QLValuePB& value, uint64_t& xor_hash) {
  auto size = value.ByteSizeLong();
  faststring buffer;
  buffer.resize(size);
  value.SerializeToArray(buffer.data(), narrow_cast<int>(size));

  auto data = reinterpret_cast<const uint8_t*>(buffer.data());
  const auto block_size = sizeof(uint64_t);
  const size_t n_blocks = size / block_size;
  for (size_t i = 0; i < n_blocks; i++) {
    // Copy 8 bytes at a time.
    // Need to copy into uint64_t to avoid alignment issues.
    uint64_t value;
    memcpy(&value, data + i * block_size, block_size);
    xor_hash ^= value;
  }
  if (size > n_blocks * block_size) {
    // Copy the remaining bytes.
    uint64_t value = 0;
    memcpy(&value, data + n_blocks * block_size, size - n_blocks * block_size);
    xor_hash ^= value;
  }
}

Status ProcessPgTableRow(
    const dockv::PgTableRow& row, const Schema& schema,
    const std::unordered_map<uint32_t, std::string>& enum_oid_label_map,
    const std::unordered_map<uint32_t, std::vector<master::PgAttributePB>>& composite_atts_map,
    std::optional<std::ostringstream>& string_output, uint64_t& xor_hash) {
  for (size_t i = 0; i != row.projection().size(); ++i) {
    const auto& col_schema = schema.column(i);
    if (i != 0 && string_output) {
      *string_output << ", ";
    }
    auto value = row.GetValueByIndex(i);
    if (value) {
      auto ql_value_pb = value->ToQLValuePB(col_schema.type_info()->type);
      XorHash(ql_value_pb, xor_hash);

      if (string_output) {
        *string_output << VERIFY_RESULT(
            docdb::QLBinaryWrapperToString(
                ql_value_pb, col_schema.pg_type_oid(), enum_oid_label_map, composite_atts_map));
      }
    } else if (string_output) {
      *string_output << "<NULL>";
    }
  }
  return Status::OK();
}

void ProcessQLTableRow(
    const qlexpr::QLTableRow& row, const Schema& schema,
    std::optional<std::ostringstream>& string_output, uint64_t& xor_hash) {
  for (size_t col_idx = 0; col_idx < schema.num_columns(); col_idx++) {
    if (col_idx > 0 && string_output) {
      *string_output << ", ";
    }
    const auto* value = row.GetColumn(schema.column_id(col_idx));
    if (value && value->value_case() != QLValuePB::VALUE_NOT_SET) {
      XorHash(*value, xor_hash);
      if (string_output) {
        *string_output << QLValue(*value).ToValueString();
      }
    } else if (string_output) {
      *string_output << "<NULL>";
    }
  }
}

Status AppendToFile(WritableFile* file, const std::string& s) {
  if (!file) {
    return Status::OK();
  }
  return file->Append(s);
}

Status AppendToFile(WritableFile* file, std::optional<std::ostringstream>& string_output) {
  if (!file || !string_output) {
    return Status::OK();
  }
  auto str = string_output->str();
  // Reset the string and clear the error if any.
  string_output->str("");
  string_output->clear();
  return file->Append(str);
}

}  // namespace

Status DumpTabletData(
    Tablet& tablet, std::shared_future<client::YBClient*> client_future, WritableFile* file,
    uint64_t read_ht, CoarseTimePoint deadline, uint64_t& xor_hash, uint64_t& row_count) {
  xor_hash = 0;
  row_count = 0;

  auto tablet_metadata = tablet.metadata();
  // Get all tables of the tablet. For non-colocated tables, this will return a single table.
  auto table_ids = tablet_metadata->GetAllColocatedTables();

  HybridTime read_hybrid_time;
  if (read_ht) {
    RETURN_NOT_OK(read_hybrid_time.FromUint64(read_ht));
  } else {
    read_hybrid_time = VERIFY_RESULT(tablet.SafeTime(RequireLease::kFalse));
  }
  RETURN_NOT_OK(AppendToFile(file, Format("Read HT: $0\n", read_hybrid_time)));

  std::optional<std::ostringstream> string_output;
  std::unordered_map<uint32_t, std::string> enum_oid_label_map;
  std::unordered_map<uint32_t, std::vector<master::PgAttributePB>> composite_atts_map;
  if (file) {
    string_output = std::make_optional<std::ostringstream>();
    if (tablet.table_type() == TableType::PGSQL_TABLE_TYPE) {
      const auto ns_name = tablet_metadata->namespace_name();
      const auto client = client_future.get();
      enum_oid_label_map = VERIFY_RESULT(client->GetPgEnumOidLabelMap(ns_name));
      composite_atts_map = VERIFY_RESULT(client->GetPgCompositeAttsMap(ns_name));
    }
  }

  for (const auto& table_id : table_ids) {
    if (IsColocationParentTableId(table_id)) {
      continue;
    }
    auto table_info = VERIFY_RESULT(tablet_metadata->GetTableInfo(table_id));

    TableInfoPB table_info_pb;
    table_info->ToPB(&table_info_pb);
    RETURN_NOT_OK(AppendToFile(file, Format("\nTable Info:\n$0", table_info_pb.DebugString())));
    RETURN_NOT_OK(AppendToFile(file, "\nRows:\n"));

    const auto& schema = table_info->schema();
    dockv::ReaderProjection projection(schema);
    auto iter = VERIFY_RESULT(tablet.NewRowIterator(
        projection, ReadHybridTime::SingleTime(read_hybrid_time), table_id, deadline));
    if (tablet.table_type() == TableType::PGSQL_TABLE_TYPE) {
      dockv::PgTableRow table_row(projection);
      while (VERIFY_RESULT(iter->PgFetchNext(&table_row))) {
        RETURN_NOT_OK(ProcessPgTableRow(
            table_row, schema, enum_oid_label_map, composite_atts_map, string_output, xor_hash));
        RETURN_NOT_OK(AppendToFile(file, string_output));
        RETURN_NOT_OK(AppendToFile(file, "\n"));
        row_count++;
      }
    } else {
      qlexpr::QLTableRow table_row;
      while (VERIFY_RESULT(iter->FetchNext(&table_row))) {
        ProcessQLTableRow(table_row, schema, string_output, xor_hash);
        RETURN_NOT_OK(AppendToFile(file, string_output));
        RETURN_NOT_OK(AppendToFile(file, "\n"));
        row_count++;
      }
    }
  }

  return Status::OK();
}

}  // namespace yb::tablet

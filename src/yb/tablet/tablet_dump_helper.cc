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
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_util.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"
#include "yb/qlexpr/ql_expr.h"
#include "yb/util/status_format.h"

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
    uint64_t read_ht, CoarseTimePoint deadline, uint64_t& xor_hash, uint64_t& row_count,
    const TableId& target_table_id, Slice start_partition_key, Slice end_partition_key) {
  xor_hash = 0;
  row_count = 0;

  auto tablet_metadata = tablet.metadata();
  // Get all tables of the tablet. For non-colocated tables, this will return a single table.
  auto table_ids = tablet_metadata->GetAllColocatedTables();
  // When a single table is requested, track whether we actually saw it so we can reject a
  // table_id that does not belong to this tablet rather than silently returning an empty hash.
  bool target_table_found = false;

  const bool has_key_range = !start_partition_key.empty() || !end_partition_key.empty();
  SCHECK(
      !has_key_range || !target_table_id.empty() || table_ids.size() == 1, InvalidArgument,
      "get_table_hash with a key range requires a single target table; pass a concrete table id "
      "(not a colocation parent id)");

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
    // Restrict to the requested table when one was specified (used to hash a single table of a
    // colocated tablet). An empty target_table_id hashes every table in the tablet.
    if (!target_table_id.empty() && table_id != target_table_id) {
      continue;
    }
    auto table_info = VERIFY_RESULT(tablet_metadata->GetTableInfo(table_id));
    // Vector indexes are colocated with the base table and contain the same data, so are currently
    // skipped.
    if (table_info->IsVectorIndex()) {
      continue;
    }
    target_table_found = true;

    TableInfoPB table_info_pb;
    table_info->ToPB(&table_info_pb);
    RETURN_NOT_OK(AppendToFile(file, Format("\nTable Info:\n$0", table_info_pb.DebugString())));
    RETURN_NOT_OK(AppendToFile(file, "\nRows:\n"));

    const auto& schema = table_info->schema();
    dockv::ReaderProjection projection(schema);
    // When a start bound is given we reposition the iterator with SeekToDocKeyPrefix below, so skip
    // the initial seek NewRowIterator would otherwise perform: two seeks without an intervening
    // fetch trip a DCHECK in IntentAwareIterator (need_fetch_ already set). With no start bound we
    // keep the default initial seek, which positions the iterator at the table's natural start.
    const bool seek_to_start = !start_partition_key.empty();
    auto iter = VERIFY_RESULT(tablet.NewRowIterator(
        projection, ReadHybridTime::SingleTime(read_hybrid_time), table_id, deadline,
        docdb::SkipSeek(seek_to_start)));

    // Build the encoded key bounds for this table as [table prefix][encoded partition key]. The
    // table prefix (cotable_id / colocation_id bytes; empty for a non-colocated table) places the
    // bound in this table's slice of the tablet, and the encoded partition key narrows within it
    // (kUInt16Hash + hash for a hash key, the encoded range otherwise). An empty user bound leaves
    // that side unbounded: the iterator is already scoped to the table, so it naturally starts and
    // stops at the table boundary. The row key is a prefix-compatible extension of these bounds, so
    // a row whose key is >= encoded_end is at/beyond the exclusive upper bound and ends the scan.
    const Slice table_key_prefix = table_info->doc_read_context->table_key_prefix();
    // Builds the full encoded bound [table prefix][encoded partition key] for this table.
    auto encode_table_bound = [&](Slice partition_key) -> Result<dockv::KeyBytes> {
      // For a hash-partitioned table the partition key is a 2-byte hash prefix (kUInt16Hash). The
      // CLI only validates that the hex is well-formed, not its length, so reject a non-2-byte
      // bound here rather than encoding a nonsensical key. (Only non-empty bounds reach this
      // lambda; an empty bound is left at the table's natural boundary.)
      SCHECK(
          !table_info->partition_schema.IsHashPartitioning() || partition_key.size() == 2,
          InvalidArgument,
          Format("hash-partitioned table $0 requires a 2-byte partition key bound; got $1 bytes",
                 table_id, partition_key.size()));
      dockv::KeyBytes encoded;
      encoded.AppendRawBytes(table_key_prefix);
      encoded.AppendRawBytes(VERIFY_RESULT(
          table_info->partition_schema.GetEncodedPartitionKey(partition_key.ToBuffer())));
      return encoded;
    };
    dockv::KeyBytes encoded_end;
    if (seek_to_start) {
      auto encoded_start = VERIFY_RESULT(encode_table_bound(start_partition_key));
      iter->SeekToDocKeyPrefix(encoded_start.AsSlice());
    }
    if (!end_partition_key.empty()) {
      encoded_end = VERIFY_RESULT(encode_table_bound(end_partition_key));
    }
    auto past_upper_bound = [&iter, &end_partition_key, &encoded_end]() {
      return !end_partition_key.empty() && iter->GetRowKey().compare(encoded_end.AsSlice()) >= 0;
    };
    if (tablet.table_type() == TableType::PGSQL_TABLE_TYPE) {
      dockv::PgTableRow table_row(projection);
      while (VERIFY_RESULT(iter->PgFetchNext(&table_row))) {
        if (past_upper_bound()) {
          break;
        }
        RETURN_NOT_OK(ProcessPgTableRow(
            table_row, schema, enum_oid_label_map, composite_atts_map, string_output, xor_hash));
        RETURN_NOT_OK(AppendToFile(file, string_output));
        RETURN_NOT_OK(AppendToFile(file, "\n"));
        row_count++;
      }
    } else {
      qlexpr::QLTableRow table_row;
      while (VERIFY_RESULT(iter->FetchNext(&table_row))) {
        if (past_upper_bound()) {
          break;
        }
        ProcessQLTableRow(table_row, schema, string_output, xor_hash);
        RETURN_NOT_OK(AppendToFile(file, string_output));
        RETURN_NOT_OK(AppendToFile(file, "\n"));
        row_count++;
      }
    }
  }

  SCHECK(
      target_table_id.empty() || target_table_found, InvalidArgument,
      Format("Requested table $0 was not found in this tablet", target_table_id));

  return Status::OK();
}

}  // namespace yb::tablet

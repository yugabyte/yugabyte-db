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

#include "yb/docdb/docdb_debug.h"

#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/kv_debug.h"

#include "yb/rocksdb/db.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/fast_varint.h"
#include "yb/util/result.h"

using std::ostream;

using namespace std::placeholders;

namespace yb {
namespace docdb {

namespace {

void AppendLineToStream(const std::string& s, ostream* out) {
  *out << s << std::endl;
}

std::pair<Result<std::string>, Result<std::string>> DumpEntryToString(
    Slice key, Slice value, SchemaPackingProvider* schema_packing_provider /*null ok*/,
    StorageDbType db_type) {
  const auto key_str = DocDBKeyToDebugStr(key, db_type);
  if (!key_str.ok()) {
    return {key_str.status(), value.ToDebugHexString()};
  }
  const KeyType key_type = GetKeyType(key, db_type);
  return {key_str, DocDBValueToDebugStr(key_type, key, value, schema_packing_provider)};
}

template <class DumpStringFunc>
void ProcessDumpEntry(
    Slice key, Slice value, SchemaPackingProvider* schema_packing_provider /*null ok*/,
    StorageDbType db_type, IncludeBinary include_binary, DumpStringFunc func) {
  auto [key_res, value_res] = DumpEntryToString(key, value, schema_packing_provider, db_type);
  func(Format("$0 -> $1", key_res, value_res));
  if (include_binary) {
    func(Format("$0 -> $1\n", FormatSliceAsStr(key), FormatSliceAsStr(value)));
  }
}

template <class DumpStringFunc>
void DocDBDebugDump(
    rocksdb::DB* rocksdb, SchemaPackingProvider* schema_packing_provider /*null ok*/,
    StorageDbType db_type, IncludeBinary include_binary, DumpStringFunc dump_func) {
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  auto iter = std::unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  iter->SeekToFirst();

  if (db_type == StorageDbType::kIntents) {
    while (iter->Valid()) {
      ProcessDumpEntry(
          iter->key(), iter->value(), schema_packing_provider, db_type, include_binary, dump_func);
      iter->Next();
    }
  } else if (iter->Valid()) {
    rocksdb::ScanCallback scan_callback = [&](const Slice& key, const Slice& value) -> bool {
      ProcessDumpEntry(key, value, schema_packing_provider, db_type, include_binary, dump_func);
      return true;
    };
    iter->ScanForward(Slice(), /*key_filter_callback=*/ nullptr, &scan_callback);
  }
  const auto s = iter->status();
  if (!s.ok()) {
    dump_func(s.ToString());
  }
}

} // namespace

std::string EntryToString(
    const Slice& key, const Slice& value,
    SchemaPackingProvider* schema_packing_provider /*null ok*/, StorageDbType db_type) {
  auto [key_res, value_res] = DumpEntryToString(key, value, schema_packing_provider, db_type);
  std::string value_str;
  auto value_copy = value;
  if (value_res.ok()) {
    value_str = *value_res;
  } else if (value_res.status().IsNotFound() &&
             IsPackedRow(dockv::DecodeValueEntryType(value_copy))) {
    value_copy.consume_byte();
    auto version = FastDecodeUnsignedVarInt(&value_copy);
    if (!version.ok()) {
      value_str = version.status().ToString();
    } else {
      value_str = Format("PACKED_ROW[$0]($1)", *version, value_copy.ToDebugHexString());
    }
  } else {
    value_str = value_res.status().ToString();
  }
  return Format("$0 -> $1", key_res, value_str);
}

std::string EntryToString(
    const rocksdb::Iterator& iterator,
    SchemaPackingProvider* schema_packing_provider /*null ok*/, StorageDbType db_type) {
  return EntryToString(iterator.key(), iterator.value(), schema_packing_provider, db_type);
}

void DocDBDebugDump(
    rocksdb::DB* rocksdb, ostream& out,
    SchemaPackingProvider* schema_packing_provider /*null ok*/, StorageDbType db_type,
    IncludeBinary include_binary) {
  DocDBDebugDump(
      rocksdb, schema_packing_provider, db_type, include_binary,
      std::bind(&AppendLineToStream, _1, &out));
}

std::string DocDBDebugDumpToStr(
    DocDB docdb, SchemaPackingProvider* schema_packing_provider /*null ok*/,
    IncludeBinary include_binary) {
  std::stringstream ss;
  DocDBDebugDump(
      docdb.regular, ss, schema_packing_provider, StorageDbType::kRegular, include_binary);
  if (docdb.intents) {
    DocDBDebugDump(
        docdb.intents, ss, schema_packing_provider, StorageDbType::kIntents, include_binary);
  }
  return ss.str();
}

std::string DocDBDebugDumpToStr(
    rocksdb::DB* rocksdb, SchemaPackingProvider* schema_packing_provider /*null ok*/,
    StorageDbType db_type, IncludeBinary include_binary) {
  std::stringstream ss;
  DocDBDebugDump(rocksdb, ss, schema_packing_provider, db_type, include_binary);
  return ss.str();
}

std::string DocDBDebugDumpToStr(const DocOperationApplyData& data) {
  return DocDBDebugDumpToStr(data.doc_write_batch->doc_db(), data.schema_packing_provider);
}

void AppendToContainer(const std::string& s, std::unordered_set<std::string>* out) {
  out->insert(s);
}

void AppendToContainer(const std::string& s, std::vector<std::string>* out) {
  out->push_back(s);
}

template <class T>
void DocDBDebugDumpToContainer(
    rocksdb::DB* rocksdb, SchemaPackingProvider* schema_packing_provider /*null ok*/, T* out,
    StorageDbType db_type) {
  void (*f)(const std::string&, T*) = AppendToContainer;
  DocDBDebugDump(
      rocksdb, schema_packing_provider, db_type, IncludeBinary::kFalse, std::bind(f, _1, out));
}

void DocDBDebugDumpToContainer(
    DocDB docdb, SchemaPackingProvider* schema_packing_provider /*null ok*/,
    std::unordered_set<std::string>* out) {
  DocDBDebugDumpToContainer(docdb.regular, schema_packing_provider, out, StorageDbType::kRegular);
  if (docdb.intents) {
    DocDBDebugDumpToContainer(docdb.intents, schema_packing_provider, out, StorageDbType::kIntents);
  }
}

void DumpRocksDBToLog(
    rocksdb::DB* rocksdb, SchemaPackingProvider* schema_packing_provider /*null ok*/,
    StorageDbType db_type, const std::string& log_prefix) {
  std::vector<std::string> lines;
  DocDBDebugDumpToContainer(rocksdb, schema_packing_provider, &lines, db_type);
  LOG(INFO) << log_prefix << AsString(db_type) << " DB dump:";
  for (const auto& line : lines) {
    LOG(INFO) << log_prefix << "  " << line;
  }
}

}  // namespace docdb
}  // namespace yb

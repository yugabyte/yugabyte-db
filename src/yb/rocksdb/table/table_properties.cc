//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/rocksdb/table_properties.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/util/string_util.h"

namespace rocksdb {

const uint32_t TablePropertiesCollectorFactory::Context::kUnknownColumnFamily =
    port::kMaxInt32;

namespace {
  void AppendProperty(
      std::string* props,
      const std::string& key,
      const std::string& value,
      const std::string& prop_delim,
      const std::string& kv_delim) {
    props->append(key);
    props->append(kv_delim);
    props->append(value);
    props->append(prop_delim);
  }

  template <class TValue>
  void AppendProperty(
      std::string* props,
      const std::string& key,
      const TValue& value,
      const std::string& prop_delim,
      const std::string& kv_delim) {
    AppendProperty(
        props, key, ToString(value), prop_delim, kv_delim
    );
  }
} // namespace

std::string TableProperties::ToString(
    const std::string& prop_delim,
    const std::string& kv_delim) const {
  std::string result;
  result.reserve(1024);

  // Basic Info
  AppendProperty(&result, "# data blocks", num_data_blocks, prop_delim,
                 kv_delim);
  AppendProperty(&result, "# data index blocks", num_data_index_blocks, prop_delim,
      kv_delim);
  AppendProperty(&result, "# filter blocks", num_filter_blocks, prop_delim,
      kv_delim);
  AppendProperty(&result, "# entries", num_entries, prop_delim, kv_delim);

  AppendProperty(&result, "raw key size", raw_key_size, prop_delim, kv_delim);
  AppendProperty(&result, "raw average key size",
                 num_entries != 0 ? 1.0 * raw_key_size / num_entries : 0.0,
                 prop_delim, kv_delim);
  AppendProperty(&result, "raw value size", raw_value_size, prop_delim,
                 kv_delim);
  AppendProperty(&result, "raw average value size",
                 num_entries != 0 ? 1.0 * raw_value_size / num_entries : 0.0,
                 prop_delim, kv_delim);

  AppendProperty(&result, "data blocks total size", data_size, prop_delim, kv_delim);
  AppendProperty(&result, "data index size", data_index_size, prop_delim, kv_delim);
  AppendProperty(&result, "filter blocks total size", filter_size, prop_delim, kv_delim);
  AppendProperty(&result, "filter index block size", filter_index_size, prop_delim, kv_delim);
  AppendProperty(
      &result, "(estimated) table size", data_size + data_index_size + filter_size, prop_delim,
      kv_delim);

  AppendProperty(
      &result, "filter policy name",
      filter_policy_name.empty() ? std::string("N/A") : filter_policy_name,
      prop_delim, kv_delim);

  return result;
}

void TableProperties::Add(const TableProperties& tp) {
  data_size += tp.data_size;
  data_index_size += tp.data_index_size;
  filter_size += tp.filter_size;
  filter_index_size += tp.filter_index_size;
  raw_key_size += tp.raw_key_size;
  raw_value_size += tp.raw_value_size;
  num_data_blocks += tp.num_data_blocks;
  num_filter_blocks += tp.num_filter_blocks;
  num_data_index_blocks += tp.num_data_index_blocks;
  num_entries += tp.num_entries;
}

const std::string TablePropertiesNames::kDataSize  =
    "rocksdb.data.size";
const std::string TablePropertiesNames::kDataIndexSize =
    "rocksdb.data.index.size";
const std::string TablePropertiesNames::kFilterSize =
    "rocksdb.filter.size";
const std::string TablePropertiesNames::kFilterIndexSize =
    "rocksdb.filter.index.size";
const std::string TablePropertiesNames::kRawKeySize =
    "rocksdb.raw.key.size";
const std::string TablePropertiesNames::kRawValueSize =
    "rocksdb.raw.value.size";
const std::string TablePropertiesNames::kNumDataBlocks =
    "rocksdb.num.data.blocks";
const std::string TablePropertiesNames::kNumEntries =
    "rocksdb.num.entries";
const std::string TablePropertiesNames::kNumFilterBlocks =
    "rocksdb.num.filter.blocks";
const std::string TablePropertiesNames::kNumDataIndexBlocks =
    "rocksdb.num.data.index.blocks";
const std::string TablePropertiesNames::kFilterPolicy =
    "rocksdb.filter.policy";
const std::string TablePropertiesNames::kFormatVersion =
    "rocksdb.format.version";
const std::string TablePropertiesNames::kFixedKeyLen =
    "rocksdb.fixed.key.length";

extern const std::string kPropertiesBlock = "rocksdb.properties";
// Old property block name for backward compatibility
extern const std::string kPropertiesBlockOldName = "rocksdb.stats";

// Seek to the properties block.
// Return true if it successfully seeks to the properties block.
Status SeekToPropertiesBlock(InternalIterator* meta_iter, bool* is_found) {
  *is_found = true;
  meta_iter->Seek(kPropertiesBlock);
  if (meta_iter->status().ok() &&
      (!meta_iter->Valid() || meta_iter->key() != kPropertiesBlock)) {
    meta_iter->Seek(kPropertiesBlockOldName);
    if (meta_iter->status().ok() &&
        (!meta_iter->Valid() || meta_iter->key() != kPropertiesBlockOldName)) {
      *is_found = false;
    }
  }
  return meta_iter->status();
}

}  // namespace rocksdb

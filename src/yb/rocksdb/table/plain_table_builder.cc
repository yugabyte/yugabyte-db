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


#include "yb/rocksdb/table/plain_table_builder.h"

#include <assert.h>

#include <limits>
#include <map>

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/block_builder.h"
#include "yb/rocksdb/table/bloom_block.h"
#include "yb/rocksdb/table/format.h"
#include "yb/rocksdb/table/meta_blocks.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/file_reader_writer.h"

#include "yb/util/status_log.h"

namespace rocksdb {

namespace {

// a utility that helps writing block content to the file
//   @offset will advance if @block_contents was successfully written.
//   @block_handle the block handle this particular block.
Status WriteBlock(const Slice& block_contents, WritableFileWriter* file,
                  uint64_t* offset, BlockHandle* block_handle) {
  block_handle->set_offset(*offset);
  block_handle->set_size(block_contents.size());
  Status s = file->Append(block_contents);

  if (s.ok()) {
    *offset += block_contents.size();
  }
  return s;
}

}  // namespace

// kPlainTableMagicNumber was picked by running
//    echo rocksdb.table.plain | sha1sum
// and taking the leading 64 bits.
extern const uint64_t kPlainTableMagicNumber = 0x8242229663bf9564ull;
extern const uint64_t kLegacyPlainTableMagicNumber = 0x4f3418eb7a8f13b8ull;

PlainTableBuilder::PlainTableBuilder(
    const ImmutableCFOptions& ioptions,
    const IntTblPropCollectorFactories& int_tbl_prop_collector_factories,
    uint32_t column_family_id,
    WritableFileWriter* file,
    uint32_t user_key_len,
    EncodingType encoding_type,
    size_t index_sparseness,
    uint32_t bloom_bits_per_key,
    uint32_t num_probes, size_t
    huge_page_tlb_size,
    double hash_table_ratio,
    bool store_index_in_file)
    : ioptions_(ioptions),
      bloom_block_(num_probes),
      file_(file),
      bloom_bits_per_key_(bloom_bits_per_key),
      huge_page_tlb_size_(huge_page_tlb_size),
      encoder_(encoding_type, user_key_len, ioptions.prefix_extractor,
               index_sparseness),
      store_index_in_file_(store_index_in_file),
      prefix_extractor_(ioptions.prefix_extractor) {
  // Build index block and save it in the file if hash_table_ratio > 0
  if (store_index_in_file_) {
    assert(hash_table_ratio > 0 || IsTotalOrderMode());
    index_builder_.reset(
        new PlainTableIndexBuilder(&arena_, ioptions, index_sparseness,
                                   hash_table_ratio, huge_page_tlb_size_));
    assert(bloom_bits_per_key_ > 0);
    properties_.user_collected_properties
        [PlainTablePropertyNames::kBloomVersion] = "1";  // For future use
  }

  properties_.fixed_key_len = user_key_len;

  // for plain table, we put all the data in a big chuck.
  properties_.num_data_blocks = 1;
  // Fill it later if store_index_in_file_ == true
  properties_.data_index_size = 0;
  properties_.num_data_index_blocks = 0;
  properties_.filter_index_size = 0;
  properties_.num_filter_blocks = 0;
  properties_.filter_size = 0;
  // To support roll-back to previous version, now still use version 0 for
  // plain encoding.
  properties_.format_version = (encoding_type == kPlain) ? 0 : 1;

  if (ioptions_.prefix_extractor) {
    properties_.user_collected_properties
        [PlainTablePropertyNames::kPrefixExtractorName] =
        ioptions_.prefix_extractor->Name();
  }

  std::string val;
  PutFixed32(&val, static_cast<uint32_t>(encoder_.GetEncodingType()));
  properties_.user_collected_properties
      [PlainTablePropertyNames::kEncodingType] = val;

  for (auto& collector_factories : int_tbl_prop_collector_factories) {
    table_properties_collectors_.emplace_back(
        collector_factories->CreateIntTblPropCollector(column_family_id));
  }
}

PlainTableBuilder::~PlainTableBuilder() {
}

void PlainTableBuilder::Add(const Slice& key, const Slice& value) {
  // temp buffer for metadata bytes between key and value.
  char meta_bytes_buf[6];
  size_t meta_bytes_buf_size = 0;
  last_key_.assign(key.cdata(), key.size());

  ParsedInternalKey internal_key;
  ParseInternalKey(key, &internal_key);

  // Store key hash
  if (store_index_in_file_) {
    if (ioptions_.prefix_extractor == nullptr) {
      keys_or_prefixes_hashes_.push_back(GetSliceHash(internal_key.user_key));
    } else {
      Slice prefix =
          ioptions_.prefix_extractor->Transform(internal_key.user_key);
      keys_or_prefixes_hashes_.push_back(GetSliceHash(prefix));
    }
  }

  // Write value
  assert(offset_ <= std::numeric_limits<uint32_t>::max());
  auto prev_offset = static_cast<uint32_t>(offset_);
  // Write out the key
  CHECK_OK(encoder_.AppendKey(key, file_, &offset_, meta_bytes_buf, &meta_bytes_buf_size));
  if (SaveIndexInFile()) {
    index_builder_->AddKeyPrefix(GetPrefix(internal_key), prev_offset);
  }

  // Write value length
  uint32_t value_size = static_cast<uint32_t>(value.size());
  char* end_ptr =
      EncodeVarint32(meta_bytes_buf + meta_bytes_buf_size, value_size);
  assert(end_ptr <= meta_bytes_buf + sizeof(meta_bytes_buf));
  meta_bytes_buf_size = end_ptr - meta_bytes_buf;
  CHECK_OK(file_->Append(Slice(meta_bytes_buf, meta_bytes_buf_size)));

  // Write value
  CHECK_OK(file_->Append(value));
  offset_ += value_size + meta_bytes_buf_size;

  properties_.num_entries++;
  properties_.raw_key_size += key.size();
  properties_.raw_value_size += value.size();

  // notify property collectors
  NotifyCollectTableCollectorsOnAdd(
      key, value, offset_, table_properties_collectors_, ioptions_.info_log);
}

Status PlainTableBuilder::status() const { return status_; }

Status PlainTableBuilder::Finish() {
  assert(!closed_);
  closed_ = true;

  properties_.data_size = offset_;

  //  Write the following blocks
  //  1. [meta block: bloom] - optional
  //  2. [meta block: index] - optional
  //  3. [meta block: properties]
  //  4. [metaindex block]
  //  5. [footer]

  MetaIndexBuilder meta_index_builer;

  if (store_index_in_file_ && (properties_.num_entries > 0)) {
    assert(properties_.num_entries <= std::numeric_limits<uint32_t>::max());
    bloom_block_.SetTotalBits(
        &arena_,
        static_cast<uint32_t>(properties_.num_entries) * bloom_bits_per_key_,
        ioptions_.bloom_locality, huge_page_tlb_size_, ioptions_.info_log);

    PutVarint32(&properties_.user_collected_properties
                     [PlainTablePropertyNames::kNumBloomBlocks],
                bloom_block_.GetNumBlocks());

    bloom_block_.AddKeysHashes(keys_or_prefixes_hashes_);
    BlockHandle bloom_block_handle;
    auto finish_result = bloom_block_.Finish();

    properties_.filter_size = finish_result.size();
    properties_.num_filter_blocks = 1;
    auto s = WriteBlock(finish_result, file_, &offset_, &bloom_block_handle);

    if (!s.ok()) {
      return s;
    }

    BlockHandle index_block_handle;
    finish_result = index_builder_->Finish();

    properties_.data_index_size = finish_result.size();
    properties_.num_data_index_blocks = 1;
    s = WriteBlock(finish_result, file_, &offset_, &index_block_handle);

    if (!s.ok()) {
      return s;
    }

    meta_index_builer.Add(BloomBlockBuilder::kBloomBlock, bloom_block_handle);
    meta_index_builer.Add(PlainTableIndexBuilder::kPlainTableIndexBlock,
                          index_block_handle);
  }

  // Calculate bloom block size and index block size
  PropertyBlockBuilder property_block_builder;
  // -- Add basic properties
  property_block_builder.AddTableProperty(properties_);

  property_block_builder.Add(properties_.user_collected_properties);

  // -- Add user collected properties
  NotifyCollectTableCollectorsOnFinish(table_properties_collectors_,
                                       ioptions_.info_log,
                                       &property_block_builder);

  // -- Write property block
  BlockHandle property_block_handle;
  auto s = WriteBlock(
      property_block_builder.Finish(),
      file_,
      &offset_,
      &property_block_handle
  );
  if (!s.ok()) {
    return s;
  }
  meta_index_builer.Add(kPropertiesBlock, property_block_handle);

  // -- write metaindex block
  BlockHandle metaindex_block_handle;
  s = WriteBlock(
      meta_index_builer.Finish(),
      file_,
      &offset_,
      &metaindex_block_handle
  );
  if (!s.ok()) {
    return s;
  }

  // Write Footer
  // no need to write out new footer if we're using default checksum
  Footer footer(kLegacyPlainTableMagicNumber, 0);
  footer.set_metaindex_handle(metaindex_block_handle);
  footer.set_index_handle(BlockHandle::NullBlockHandle());
  std::string footer_encoding;
  footer.AppendEncodedTo(&footer_encoding);
  s = file_->Append(footer_encoding);
  if (s.ok()) {
    offset_ += footer_encoding.size();
  }

  return s;
}

void PlainTableBuilder::Abandon() {
  closed_ = true;
}

uint64_t PlainTableBuilder::NumEntries() const {
  return properties_.num_entries;
}

uint64_t PlainTableBuilder::TotalFileSize() const {
  return offset_;
}

uint64_t PlainTableBuilder::BaseFileSize() const {
  return TotalFileSize();
}

const std::string& PlainTableBuilder::LastKey() const {
  return last_key_;
}

}  // namespace rocksdb

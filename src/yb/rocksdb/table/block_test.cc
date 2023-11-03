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

#include <stdio.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/table/block.h"
#include "yb/rocksdb/table/block_builder.h"
#include "yb/rocksdb/table/block_builder_internal.h"
#include "yb/rocksdb/table/block_hash_index.h"
#include "yb/rocksdb/table/block_internal.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/util/testutil.h"

#include "yb/util/env.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"

DECLARE_int32(v);

namespace rocksdb {

std::string GenerateKey(int primary_key, int secondary_key, int padding_size,
                        Random *rnd) {
  char buf[50];
  char *p = &buf[0];
  snprintf(buf, sizeof(buf), "%6d%4d", primary_key, secondary_key);
  std::string k(p);
  if (padding_size) {
    k += RandomString(rnd, padding_size);
  }

  return k;
}

// Generate random key value pairs.
// The generated key will be sorted. You can tune the parameters to generated
// different kinds of test key/value pairs for different scenario.
void GenerateRandomKVs(std::vector<std::string> *keys,
                       std::vector<std::string> *values, const int from,
                       const int len, const int step = 1,
                       const int padding_size = 0,
                       const int keys_share_prefix = 1) {
  Random rnd(302);

  // generate different prefix
  for (int i = from; i < from + len; i += step) {
    // generating keys that shares the prefix
    for (int j = 0; j < keys_share_prefix; ++j) {
      keys->emplace_back(GenerateKey(i, j, padding_size, &rnd));

      // 100 bytes values
      values->emplace_back(RandomString(&rnd, 100));
    }
  }
}

class BlockTest : public RocksDBTest {};

// block test
TEST_F(BlockTest, SimpleTest) {
  for (auto key_value_encoding_format : KeyValueEncodingFormatList()) {
    Random rnd(301);
    Options options = Options();
    std::unique_ptr<InternalKeyComparator> ic;
    ic.reset(new test::PlainInternalKeyComparator(options.comparator));

    std::vector<std::string> keys;
    std::vector<std::string> values;
    BlockBuilder builder(16, key_value_encoding_format);
    int num_records = 100000;

    GenerateRandomKVs(&keys, &values, 0, num_records);
    // add a bunch of records to a block
    for (int i = 0; i < num_records; i++) {
      builder.Add(keys[i], values[i]);
    }

    // read serialized contents of the block
    Slice rawblock = builder.Finish();

    // create block reader
    BlockContents contents;
    contents.data = rawblock;
    contents.cachable = false;
    Block reader(std::move(contents));

    // read contents of block sequentially
    int count = 0;
    InternalIterator *iter = reader.NewIterator(options.comparator, key_value_encoding_format);
    for (iter->SeekToFirst(); iter->Valid(); count++, iter->Next()) {

      // read kv from block
      Slice k = iter->key();
      Slice v = iter->value();

      // compare with lookaside array
      ASSERT_EQ(k.ToString().compare(keys[count]), 0);
      ASSERT_EQ(v.ToString().compare(values[count]), 0);
    }
    delete iter;

    // read block contents randomly
    iter = reader.NewIterator(options.comparator, key_value_encoding_format);
    for (int i = 0; i < num_records; i++) {

      // find a random key in the lookaside array
      int index = rnd.Uniform(num_records);
      Slice k(keys[index]);

      // search in block for this key
      iter->Seek(k);
      ASSERT_TRUE(iter->Valid());
      Slice v = iter->value();
      ASSERT_EQ(v.ToString().compare(values[index]), 0);
    }
    delete iter;
  }
}

// return the block contents
BlockContents GetBlockContents(std::unique_ptr<BlockBuilder> *builder,
                               const std::vector<std::string> &keys,
                               const std::vector<std::string> &values,
                               const KeyValueEncodingFormat key_value_encoding_format,
                               const int prefix_group_size = 1) {
  builder->reset(new BlockBuilder(1 /* restart interval */, key_value_encoding_format));

  // Add only half of the keys
  for (size_t i = 0; i < keys.size(); ++i) {
    (*builder)->Add(keys[i], values[i]);
  }
  Slice rawblock = (*builder)->Finish();

  BlockContents contents;
  contents.data = rawblock;
  contents.cachable = false;

  return contents;
}

void CheckBlockContents(
    BlockContents contents, const KeyValueEncodingFormat key_value_encoding_format,
    const std::vector<std::string>& keys, const std::vector<std::string>& values) {
  Block reader(std::move(contents));
  std::unique_ptr<InternalIterator> iter(
      reader.NewIterator(BytewiseComparator(), key_value_encoding_format));

  // Scan through the block and compare with the data loaded.
  {
    size_t i = 0;
    for (iter->SeekToFirst(); iter->Valid(); i++, iter->Next()) {
      const auto k = iter->key();
      const auto v = iter->value();
      ASSERT_EQ(k.ToString(), keys[i])
          << "i: " << i << "\nexpected: " << Slice(keys[i]).ToDebugHexString()
          << "\n  actual: " << k.ToDebugHexString();
      ASSERT_EQ(v.ToString(), values[i])
          << "i: " << i << "\nkey: " << Slice(keys[i]).ToDebugHexString()
          << "\nexpected value: " << Slice(values[i]).ToDebugHexString()
          << "\n  actual value: " << v.ToDebugHexString();
    }
    ASSERT_EQ(i, keys.size());
  }

  // Seek to and near existing keys.
  std::string higher_key;
  for (size_t i = 0; i < keys.size(); i++) {
    const auto& key = keys[i];

    // Duplicate keys should not appear in a real workload, but RocksDB does not prohibit duplicate
    // keys, although RocksDB's Seek behavior in that case is non-deterministic.
    const bool is_duplicated_key =
        (i > 0 && key == keys[i - 1]) || (i + 1 < keys.size() && key == keys[i + 1]);

    iter->Seek(key);
    ASSERT_OK(iter->status());
    ASSERT_TRUE(iter->Valid());

    const auto k = iter->key();
    ASSERT_EQ(k.ToString(), key)
        << "i: " << i << " is_duplicated_key: " << is_duplicated_key
        << "\nexpected key: " << Slice(key).ToDebugHexString()
        << "\n  actual key: " << k.ToDebugHexString();

    if (is_duplicated_key) {
      // For duplicated keys we might seek to a different key-value for the equal key.
      continue;
    }

    const auto v = iter->value();
    ASSERT_EQ(v.ToString(), values[i])
        << "i: " << i
        << "\nkey: " << Slice(key).ToDebugHexString()
        << "\nexpected value: " << Slice(values[i]).ToDebugHexString()
        << "\n  actual value: " << v.ToDebugHexString();

    if (!key.empty()) {
      // Seek to slightly lower key.
      const auto lower_key = Slice(key.data(), key.size() - 1);
      auto j = i;
      while (j > 0 && lower_key.compare(keys[j - 1]) <= 0) {
        --j;
      }
      iter->Seek(lower_key);
      ASSERT_OK(iter->status());
      ASSERT_TRUE(iter->Valid());

      ASSERT_EQ(iter->key().ToString(), keys[j])
          << "j: " << j
          << "\nexpected key: " << Slice(keys[j]).ToDebugHexString()
          << "\n  actual key: " << iter->key().ToDebugHexString();
      ASSERT_EQ(iter->value().ToString(), values[j])
          << "j: " << j
          << "\nkey: " << iter->key().ToDebugHexString()
          << "\nexpected value: " << Slice(values[j]).ToDebugHexString()
          << "\n  actual value: " << iter->value().ToDebugHexString();
    }

    // Seek to slightly higher key.
    higher_key = key;
    higher_key.push_back(0);
    auto j = i + 1;
    while (j < keys.size() && keys[j].compare(higher_key) < 0) {
      ++j;
    }
    iter->Seek(higher_key);
    ASSERT_OK(iter->status());
    if (j == keys.size()) {
      ASSERT_FALSE(ASSERT_RESULT(iter->CheckedValid()));
      continue;
    }
    ASSERT_TRUE(iter->Valid());

    ASSERT_EQ(iter->key().ToString(), keys[j])
        << "j: " << j
        << "\nexpected key: " << Slice(keys[j]).ToDebugHexString()
        << "\n  actual key: " << iter->key().ToDebugHexString();
    ASSERT_EQ(iter->value().ToString(), values[j])
        << "j: " << j
        << "\nkey: " << Slice(keys[j]).ToDebugHexString()
        << "\nexpected value: " << Slice(values[j]).ToDebugHexString()
        << "\n  actual value: " << iter->value().ToDebugHexString();
  }
}

void CheckBlockContents(BlockContents contents,
                        const KeyValueEncodingFormat key_value_encoding_format,
                        const int max_key,
                        const std::vector<std::string> &keys,
                        const std::vector<std::string> &values) {
  ASSERT_EQ(keys.size(), values.size());

  CheckBlockContents(
      BlockContents(contents.data, contents.cachable, contents.compression_type),
      key_value_encoding_format, keys, values);

  const size_t prefix_size = 6;
  // create block reader
  BlockContents contents_ref(contents.data, contents.cachable,
                             contents.compression_type);
  Block reader1(std::move(contents));
  Block reader2(std::move(contents_ref));

  std::unique_ptr<const SliceTransform> prefix_extractor(
      NewFixedPrefixTransform(prefix_size));

  {
    auto iter1 = reader1.NewIterator(nullptr, key_value_encoding_format);
    auto iter2 = reader1.NewIterator(nullptr, key_value_encoding_format);
    reader1.SetBlockHashIndex(CreateBlockHashIndexOnTheFly(
        iter1, iter2, static_cast<uint32_t>(keys.size()), BytewiseComparator(),
        prefix_extractor.get()));

    delete iter1;
    delete iter2;
  }

  std::unique_ptr<InternalIterator> hash_iter(
      reader1.NewIterator(BytewiseComparator(), key_value_encoding_format, nullptr, false));

  std::unique_ptr<InternalIterator> regular_iter(
      reader2.NewIterator(BytewiseComparator(), key_value_encoding_format));

  // Seek existent keys
  for (size_t i = 0; i < keys.size(); i++) {
    hash_iter->Seek(keys[i]);
    ASSERT_OK(hash_iter->status());
    ASSERT_TRUE(hash_iter->Valid());

    Slice v = hash_iter->value();
    ASSERT_EQ(v.ToString().compare(values[i]), 0);
  }

  // Seek non-existent keys.
  // For hash index, if no key with a given prefix is not found, iterator will
  // simply be set as invalid; whereas the binary search based iterator will
  // return the one that is closest.
  for (int i = 1; i < max_key - 1; i += 2) {
    auto key = GenerateKey(i, 0, 0, nullptr);
    hash_iter->Seek(key);
    ASSERT_TRUE(!hash_iter->Valid());

    regular_iter->Seek(key);
    ASSERT_TRUE(regular_iter->Valid());
  }
}

// In this test case, no two key share same prefix.
TEST_F(BlockTest, SimpleIndexHash) {
  const int kMaxKey = 100000;

  for (auto key_value_encoding_format : KeyValueEncodingFormatList()) {
    std::vector<std::string> keys;
    std::vector<std::string> values;
    GenerateRandomKVs(&keys, &values, 0 /* first key id */,
                      kMaxKey /* last key id */, 2 /* step */,
                      8 /* padding size (8 bytes randomly generated suffix) */);

    std::unique_ptr<BlockBuilder> builder;
    auto contents = GetBlockContents(&builder, keys, values, key_value_encoding_format);

    CheckBlockContents(
        std::move(contents), key_value_encoding_format, kMaxKey, keys, values);
  }
}

TEST_F(BlockTest, IndexHashWithSharedPrefix) {
  const int kMaxKey = 100000;
  // for each prefix, there will be 5 keys starts with it.
  const int kPrefixGroup = 5;

  for (auto key_value_encoding_format : KeyValueEncodingFormatList()) {
    std::vector<std::string> keys;
    std::vector<std::string> values;
    // Generate keys with same prefix.
    GenerateRandomKVs(
        &keys, &values, 0,  // first key id
        kMaxKey,            // last key id
        2,                  // step
        10,                 // padding size,
        kPrefixGroup);

    std::unique_ptr<BlockBuilder> builder;
    auto contents =
        GetBlockContents(&builder, keys, values, key_value_encoding_format, kPrefixGroup);

    CheckBlockContents(
        std::move(contents), key_value_encoding_format, kMaxKey, keys, values);
  }
}

namespace {

std::string GetPaddedNum(int i) {
  return StringPrintf("%010d", i);
}

yb::Result<std::string> GetMiddleKey(
    const KeyValueEncodingFormat key_value_encoding_format, const int num_keys,
    const int block_restart_interval, const MiddlePointPolicy middle_policy) {
  BlockBuilder builder(block_restart_interval, key_value_encoding_format);

  for (int i = 1; i <= num_keys; ++i) {
    const auto padded_num = GetPaddedNum(i);
    builder.Add("k" + padded_num, "v" + padded_num);
  }

  BlockContents contents;
  contents.data = builder.Finish();
  contents.cachable = false;
  Block reader(std::move(contents));

  return VERIFY_RESULT(
      reader.GetMiddleKey(key_value_encoding_format, BytewiseComparator(), middle_policy));
}

void CheckMiddleKey(
    const KeyValueEncodingFormat key_value_encoding_format, const int num_keys,
    const int block_restart_interval, const int expected_middle_key,
    const MiddlePointPolicy middle_policy) {
  const auto middle_key = ASSERT_RESULT(
      GetMiddleKey(key_value_encoding_format, num_keys, block_restart_interval, middle_policy));
  ASSERT_EQ(middle_key, "k" + GetPaddedNum(expected_middle_key)) << "For num_keys = " << num_keys;
}

} // namespace

TEST_F(BlockTest, GetMiddleKey) {
  // Checking of explicit values
  for (const auto key_value_encoding_format : KeyValueEncodingFormatList()) {
    auto block_restart_interval = 1;

    for (const auto middle_policy : MiddlePointPolicyList()) {
      for (const auto num_keys : { 0, 1 }) {
      const auto empty_block_middle_key = GetMiddleKey(
          key_value_encoding_format, /* num_keys = */ 0, block_restart_interval, middle_policy);
      ASSERT_NOK(empty_block_middle_key) << empty_block_middle_key;
      ASSERT_TRUE(empty_block_middle_key.status().IsIncomplete()) << empty_block_middle_key;
      }
    }

    /* block_restart_interval == 1 */
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 2, block_restart_interval,
        /* expected_middle_key = */ 1, MiddlePointPolicy::kMiddleLow);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 3, block_restart_interval,
        /* expected_middle_key = */ 2, MiddlePointPolicy::kMiddleLow);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 15, block_restart_interval,
        /* expected_middle_key = */ 8, MiddlePointPolicy::kMiddleLow);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 16, block_restart_interval,
        /* expected_middle_key = */ 8, MiddlePointPolicy::kMiddleLow);

    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 2, block_restart_interval,
        /* expected_middle_key = */ 2, MiddlePointPolicy::kMiddleHigh);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 3, block_restart_interval,
        /* expected_middle_key = */ 2, MiddlePointPolicy::kMiddleHigh);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 15, block_restart_interval,
        /* expected_middle_key = */ 8, MiddlePointPolicy::kMiddleHigh);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 16, block_restart_interval,
        /* expected_middle_key = */ 9, MiddlePointPolicy::kMiddleHigh);

    block_restart_interval = 16;
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 2, block_restart_interval,
        /* expected_middle_key = */ 1, MiddlePointPolicy::kMiddleLow);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 3, block_restart_interval,
        /* expected_middle_key = */ 2, MiddlePointPolicy::kMiddleLow);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 15, block_restart_interval,
        /* expected_middle_key = */ 8, MiddlePointPolicy::kMiddleLow);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 16, block_restart_interval,
        /* expected_middle_key = */ 8, MiddlePointPolicy::kMiddleLow);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 17, block_restart_interval,
        /* expected_middle_key = */ 1, MiddlePointPolicy::kMiddleLow);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 18, block_restart_interval,
        /* expected_middle_key = */ 1, MiddlePointPolicy::kMiddleLow);

    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 2, block_restart_interval,
        /* expected_middle_key = */ 2, MiddlePointPolicy::kMiddleHigh);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 3, block_restart_interval,
        /* expected_middle_key = */ 2, MiddlePointPolicy::kMiddleHigh);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 15, block_restart_interval,
        /* expected_middle_key = */ 8, MiddlePointPolicy::kMiddleHigh);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 16, block_restart_interval,
        /* expected_middle_key = */ 9, MiddlePointPolicy::kMiddleHigh);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 17, block_restart_interval,
        /* expected_middle_key = */ 17, MiddlePointPolicy::kMiddleHigh);
    CheckMiddleKey(
        key_value_encoding_format, /* num_keys = */ 18, block_restart_interval,
        /* expected_middle_key = */ 17, MiddlePointPolicy::kMiddleHigh);
  }

  // Checking of ranges
  for (const auto middle_policy : MiddlePointPolicyList()) {
    for (auto block_restart_interval = 1; block_restart_interval < 17; ++block_restart_interval) {
      const auto get_num_restarts = [&](const int num_keys) {
        return num_keys <= 0 ? 0 : (num_keys + block_restart_interval - 1) / block_restart_interval;
      };
      const auto get_expected_middle_key = [&](const int num_keys) {
        const auto num_restarts = get_num_restarts(num_keys);
        if (num_restarts > 1) {
          const auto restart_idx = (num_restarts - yb::to_underlying(middle_policy)) / 2;
          return 1 + (restart_idx * block_restart_interval);
        } else {
          const auto key_idx = (num_keys - yb::to_underlying(middle_policy)) / 2;
          return 1 + key_idx;
        }
      };

      for (const auto key_value_encoding_format : KeyValueEncodingFormatList()) {
        for (auto num_keys = 0; num_keys < 501; ++num_keys) {
          if (num_keys > 1) {
            CheckMiddleKey(key_value_encoding_format, num_keys, block_restart_interval,
                          get_expected_middle_key(num_keys), middle_policy);
          } else {
            const auto empty_block_middle_key = GetMiddleKey(
                key_value_encoding_format, /* num_keys = */ 0,
                block_restart_interval, middle_policy);
            ASSERT_NOK(empty_block_middle_key) << empty_block_middle_key;
            ASSERT_TRUE(empty_block_middle_key.status().IsIncomplete()) << empty_block_middle_key;
          }
        }
      }
    }
  }
}

TEST_F(BlockTest, EncodeThreeSharedPartsSizes) {
  constexpr auto kNumIters = 100000;

  constexpr auto kBigMaxCompSize = std::numeric_limits<uint32_t>::max() / 16;
  constexpr auto kSmallMaxCompSize = 16;

  auto gen_comp_size = []() {
    return yb::RandomActWithProbability(0.5) ? 0
           : yb::RandomActWithProbability(0.5)
               ? yb::RandomUniformInt<uint32_t>(0, kBigMaxCompSize)
               : yb::RandomUniformInt<uint32_t>(0, kSmallMaxCompSize);
  };
  std::string buffer;

  for (auto i = 0; i < kNumIters; ++i) {
    YB_LOG_EVERY_N_SECS(INFO, 5) << "Iterations completed: " << i;
    buffer.clear();
    // Simulate there is something in buffer before encoded key.
    buffer.append('X', yb::RandomUniformInt<size_t>(0, 8));
    const auto encoded_sizes_start_offset = buffer.size();

    size_t shared_prefix_size = gen_comp_size();
    size_t last_internal_component_reuse_size;
    bool is_last_internal_component_inc;
    switch (yb::RandomUniformInt<int>(0, 2)) {
      case 0:
        last_internal_component_reuse_size = 0;
        is_last_internal_component_inc = false;
        break;
      case 1:
        last_internal_component_reuse_size = kLastInternalComponentSize;
        is_last_internal_component_inc = false;
        break;
      case 2:
        last_internal_component_reuse_size = kLastInternalComponentSize;
        is_last_internal_component_inc = true;
        break;
      default:
        FAIL();
    }
    ComponentSizes rest_sizes {
        .prev_key_non_shared_1_size = gen_comp_size(),
        .non_shared_1_size = gen_comp_size(),
        .shared_middle_size = gen_comp_size(),
        .prev_key_non_shared_2_size = gen_comp_size(),
        .non_shared_2_size = gen_comp_size(),
    };
    if (rest_sizes.shared_middle_size == 0) {
      rest_sizes.non_shared_2_size = 0;
      rest_sizes.prev_key_non_shared_2_size = 0;
    }
    const auto prev_key_size =
        shared_prefix_size + rest_sizes.prev_key_non_shared_1_size + rest_sizes.shared_middle_size +
        rest_sizes.prev_key_non_shared_2_size + last_internal_component_reuse_size;
    const auto key_size = shared_prefix_size + rest_sizes.non_shared_1_size +
                          rest_sizes.shared_middle_size + rest_sizes.non_shared_2_size +
                          last_internal_component_reuse_size;
    const auto value_size = gen_comp_size();

    EncodeThreeSharedPartsSizes(
        shared_prefix_size, last_internal_component_reuse_size, is_last_internal_component_inc,
        rest_sizes, prev_key_size, key_size, value_size, &buffer);

    const auto encoded_sizes_end_offset = buffer.size();
    const auto payload_size =
        rest_sizes.non_shared_1_size + rest_sizes.non_shared_2_size + value_size;

    uint32_t decoded_shared_prefix_size, decoded_non_shared_1_size, decoded_non_shared_2_size,
        decoded_shared_last_component_size, decoded_value_size;
    bool decoded_is_something_shared;
    int64_t decoded_non_shared_1_size_delta, decoded_non_shared_2_size_delta;
    uint64_t decoded_shared_last_component_increase;

    auto* result = DecodeEntryThreeSharedParts(
        buffer.data() + encoded_sizes_start_offset, buffer.data() + buffer.size() + payload_size,
        buffer.data(), &decoded_shared_prefix_size, &decoded_non_shared_1_size,
        &decoded_non_shared_1_size_delta, &decoded_is_something_shared, &decoded_non_shared_2_size,
        &decoded_non_shared_2_size_delta, &decoded_shared_last_component_size,
        &decoded_shared_last_component_increase, &decoded_value_size);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(decoded_shared_last_component_size, last_internal_component_reuse_size);
    EXPECT_EQ(decoded_shared_last_component_increase, 0x100 * is_last_internal_component_inc);
    EXPECT_EQ(
        decoded_is_something_shared,
        shared_prefix_size + rest_sizes.shared_middle_size + last_internal_component_reuse_size >
            0);
    EXPECT_EQ(decoded_non_shared_1_size, rest_sizes.non_shared_1_size);

    if (decoded_is_something_shared) {
      EXPECT_EQ(decoded_non_shared_2_size, rest_sizes.non_shared_2_size);
      EXPECT_EQ(
          rest_sizes.prev_key_non_shared_1_size + decoded_non_shared_1_size_delta,
          rest_sizes.non_shared_1_size)
          << " prev_key_non_shared_1_size: " << rest_sizes.prev_key_non_shared_1_size
          << " decoded_non_shared_1_size_delta: " << decoded_non_shared_1_size_delta
          << " non_shared_1_size: " << rest_sizes.non_shared_1_size;
      EXPECT_EQ(
          rest_sizes.prev_key_non_shared_2_size + decoded_non_shared_2_size_delta,
          rest_sizes.non_shared_2_size)
          << " prev_key_non_shared_2_size: " << rest_sizes.prev_key_non_shared_2_size
          << " decoded_non_shared_2_size_delta: " << decoded_non_shared_2_size_delta
          << " non_shared_2_size: " << rest_sizes.non_shared_2_size;
      EXPECT_EQ(decoded_shared_prefix_size, shared_prefix_size);
    }

    EXPECT_EQ(decoded_value_size, value_size);
    EXPECT_EQ(result, buffer.data() + encoded_sizes_end_offset);

    if (testing::Test::HasFailure()) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_v) = 4;
      EncodeThreeSharedPartsSizes(
          shared_prefix_size, last_internal_component_reuse_size, is_last_internal_component_inc,
          rest_sizes, prev_key_size, key_size, value_size, &buffer);
      result = DecodeEntryThreeSharedParts(
          buffer.data() + encoded_sizes_start_offset, buffer.data() + buffer.capacity(),
          buffer.data(), &decoded_shared_prefix_size, &decoded_non_shared_1_size,
          &decoded_non_shared_1_size_delta, &decoded_is_something_shared,
          &decoded_non_shared_2_size, &decoded_non_shared_2_size_delta,
          &decoded_shared_last_component_size, &decoded_shared_last_component_increase,
          &decoded_value_size);
      FAIL();
    }
  }
}

TEST_F(BlockTest, EncodeThreeSharedParts) {
  constexpr auto kNumIters = 20;
  constexpr auto kKeysPerBlock = 1000;
  constexpr auto kMaxKeySize = 1_KB;
  constexpr auto kMaxValueSize = 1_KB;
  constexpr auto kBlockRestartInterval = 16;
  constexpr auto kKeyValueEncodingFormat =
      KeyValueEncodingFormat::kKeyDeltaEncodingThreeSharedParts;

  auto gen_comp_size = [](size_t* size_left) {
    auto result =
        yb::RandomActWithProbability(0.5) ? 0 : yb::RandomUniformInt<size_t>(0, *size_left);
    CHECK_GE(*size_left, result);
    *size_left -= result;
    return result;
  };
  auto append_random_string = [](std::string* buf, size_t size) {
    while (size > 0) {
      *buf += yb::RandomUniformInt<uint8_t>();
      size--;
    }
  };

  std::vector<std::string> keys;
  for (auto iter = 0; iter < kNumIters; ++iter) {
    const bool use_delta_encoding = iter % 2 == 0;
    keys.clear();
    {
      const std::string empty;
      std::string key;
      for (auto i = 0; i < kKeysPerBlock; ++i) {
        const auto& prev_key = keys.empty() ? empty : keys.back();

        // Generate key based on prev_key in the following format:
        // <shared_prefix><non_shared_1><shared_middle><non_shared_2><shared_last>
        // Each component might be empty.
        // shared_prefix, shared_middle, shared_last are taken from the beginning, middle and
        // end of prev_key and have random size (+ random position for the middle).
        //
        // Since we generate non_shared_1 and non_shared_2 randomly we can still have
        // beginning/end/whole of those shared with prev key, but that doesn't matter, because
        // we will still have enough non-shared components for the test.
        auto prev_key_size_left = prev_key.size();
        const auto shared_prefix_size = gen_comp_size(&prev_key_size_left);
        const auto shared_middle_size = gen_comp_size(&prev_key_size_left);
        const auto shared_last_size = gen_comp_size(&prev_key_size_left);
        const auto shared_size = shared_prefix_size + shared_middle_size + shared_last_size;

        auto key_size_left = yb::RandomUniformInt<size_t>(0, kMaxKeySize - shared_size);
        const auto non_shared_1_size = gen_comp_size(&key_size_left);
        const auto non_shared_2_size = key_size_left;
        DVLOG(4) << "shared_prefix_size: " << shared_prefix_size
                 << " shared_middle_size: " << shared_middle_size
                 << " shared_last_size: " << shared_last_size
                 << " non_shared_1_size: " << non_shared_1_size
                 << " non_shared_2_size: " << non_shared_2_size;

        const auto prev_key_non_shared_1_size = gen_comp_size(&prev_key_size_left);
        const auto prev_key_non_shared_2_size = prev_key_size_left;

        key = prev_key.substr(0, shared_prefix_size);
        append_random_string(&key, non_shared_1_size);
        key.append(
            prev_key.substr(shared_prefix_size + prev_key_non_shared_1_size, shared_middle_size));
        append_random_string(&key, non_shared_2_size);
        key.append(prev_key.substr(
            shared_prefix_size + prev_key_non_shared_1_size + shared_middle_size +
                prev_key_non_shared_2_size,
            shared_last_size));

        keys.emplace_back(std::move(key));
      }
    }

    std::sort(keys.begin(), keys.end());

    BlockBuilder builder(kBlockRestartInterval, kKeyValueEncodingFormat, use_delta_encoding);

    std::vector<std::string> values;
    {
      std::string value;
      for (const auto& key : keys) {
        value.clear();
        append_random_string(&value, yb::RandomUniformInt<size_t>(0, kMaxValueSize));
        builder.Add(key, value);
        values.emplace_back(std::move(value));
      }
    }

    auto rawblock = builder.Finish();

    BlockContents contents;
    contents.data = rawblock;
    contents.cachable = false;

    CheckBlockContents(std::move(contents), kKeyValueEncodingFormat, keys, values);
  }
}

void TestBlockScanPerf(
    const KeyValueEncodingFormat key_value_encoding_format,
    const bool use_delta_encoding,
    const size_t block_size = 32 * 1024 /*32_KB*/,
    int key_size = 32,
    int value_size = 0) {
  constexpr auto kRestarts = 16;
  constexpr auto kIterations = 10;

  BlockBuilder builder(kRestarts, key_value_encoding_format, use_delta_encoding);

  Random rnd(302);
  std::string value;
  if (value_size > 0) rocksdb::RandomString(&rnd, value_size);
  Slice value_slize(value);
  int key_count = 0;
  do {
    /*10 digits value is generated for primary and secondary key by GenerateKey*/
    auto key = GenerateKey(key_count, key_count + 1000, key_size - 10, &rnd);
    builder.Add(key, value_slize);
    key_count++;
  } while (builder.CurrentSizeEstimate() < block_size);

  auto rawblock = builder.Finish();
  LOG(INFO) << "KeyValueEncodingFormat: "
            << KeyValueEncodingFormatToString(key_value_encoding_format)
            << ", UseDeltaEncoding: " << use_delta_encoding
            << ", BlockSize: " << rawblock.size()
            << ", KeysCount: " << key_count;

  BlockContents contents(rawblock, false, kNoCompression);
  Block reader(std::move(contents));

  uint64_t total_time = 0;
  uint64_t min_time = std::numeric_limits<uint64_t>::max();
  uint64_t max_time = 0;
  for (size_t i = 0; i < kIterations; i++) {
    auto start = yb::Env::Default()->NowNanos();
    std::unique_ptr<InternalIterator> iter(
        reader.NewIterator(BytewiseComparator(), key_value_encoding_format));

    // Scan through the block and validate the number of keys.
    {
      size_t keys = 0;
      for (iter->SeekToFirst(); iter->Valid(); keys++, iter->Next()) {
        /*const auto k =*/ iter->key();
        /*const auto v =*/ iter->value();
      }
      ASSERT_EQ(key_count, keys);
    }
    auto t = (yb::Env::Default()->NowNanos() - start);
    min_time = std::min(t, min_time);
    max_time = std::max(t, max_time);
    total_time += t;
  }

  LOG(INFO) << "Next performance (ns): Avg: " << (total_time / kIterations)
            << ", Min: " << min_time
            << ", Max: " << max_time
            << ", TotalTime: " << total_time;

  total_time = max_time = 0;
  min_time = std::numeric_limits<uint64_t>::max();
  for (size_t i = 0; i < kIterations; i++) {
    auto start = yb::Env::Default()->NowNanos();
    std::unique_ptr<InternalIterator> iter(
        reader.NewIterator(BytewiseComparator(), key_value_encoding_format));

    // Scan through the block and validate the number of keys.
    {
      size_t keys = 0;
      for (iter->SeekToLast(); iter->Valid(); keys++, iter->Prev()) {
        /*const auto k =*/ iter->key();
        /*const auto v =*/ iter->value();
      }
      ASSERT_EQ(key_count, keys);
    }
    auto t = (yb::Env::Default()->NowNanos() - start);
    min_time = std::min(t, min_time);
    max_time = std::max(t, max_time);
    total_time += t;
  }

  LOG(INFO) << "Prev performance (ns): Avg: " << (total_time / kIterations)
            << ", Min: " << min_time
            << ", Max: " << max_time
            << ", TotalTime: " << total_time;
}

TEST_F(BlockTest, IterPerfDisabledDeltaEncoding) {
  constexpr auto kKeyValueEncodingFormat =
      KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix;
  constexpr auto kUseDeltaEncoding = false;

  TestBlockScanPerf(KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix, false);
  TestBlockScanPerf(KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix, true);
  TestBlockScanPerf(KeyValueEncodingFormat::kKeyDeltaEncodingThreeSharedParts, true);
}

}  // namespace rocksdb

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}

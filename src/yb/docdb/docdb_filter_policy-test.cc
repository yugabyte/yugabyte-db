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

#include "yb/dockv/doc_key.h"
#include "yb/docdb/docdb_filter_policy.h"

#include "yb/util/test_util.h"

using rocksdb::FilterBitsBuilder;
using rocksdb::FilterBitsReader;

namespace yb::docdb {

class DocDBFilterPolicyTest : public YBTest {
};

std::string EncodeSubDocKey(const std::string& hash_key,
    const std::string& range_key, const std::string& sub_key, uint64_t time) {
  dockv::DocKey dk(0, dockv::MakeKeyEntryValues(hash_key), dockv::MakeKeyEntryValues(range_key));
  return dockv::SubDocKey(
      dk, dockv::KeyEntryValue(sub_key), HybridTime::FromMicros(time)).Encode().ToStringBuffer();
}

std::string EncodeSimpleSubDocKey(const std::string& hash_key) {
  return EncodeSubDocKey(hash_key, "range_key", "sub_key", 12345L);
}

std::string EncodeSimpleSubDocKeyWithDifferentNonHashPart(const std::string& hash_key) {
  return EncodeSubDocKey(hash_key, "another_range_key", "another_sub_key", 55555L);
}

TEST_F(DocDBFilterPolicyTest, TestKeyMatching) {
  DocDbAwareV2FilterPolicy policy(rocksdb::FilterPolicy::kDefaultFixedSizeFilterBits, nullptr);
  std::string keys[] = { "foo", "bar", "test" };
  std::string absent_key = "fake";

  std::unique_ptr<FilterBitsBuilder> builder(policy.GetFilterBitsBuilder());
  ASSERT_NE(builder, nullptr);
  // Policy supports GetFilterBitsBuilder/Reader interface (see description in filter_policy.h) -
  // lets test it.
  for (const auto& key : keys) {
    builder->AddKey(policy.GetKeyTransformer()->Transform(EncodeSimpleSubDocKey(key)));
  }
  std::unique_ptr<const char[]> buf;
  rocksdb::Slice filter = builder->Finish(&buf);

  std::unique_ptr<FilterBitsReader> reader(policy.GetFilterBitsReader(filter));

  auto may_match = [&](const std::string& sub_doc_key_str) {
    return reader->MayMatch(policy.GetKeyTransformer()->Transform(sub_doc_key_str));
  };

  for (const auto &key : keys) {
    ASSERT_TRUE(may_match(EncodeSimpleSubDocKey(key))) << "Key: " << key;
    ASSERT_TRUE(may_match(EncodeSimpleSubDocKeyWithDifferentNonHashPart(key))) << "Key: " << key;
  }
  ASSERT_FALSE(may_match(EncodeSimpleSubDocKey(absent_key))) << "Key: " << absent_key;
}

}  // namespace yb::docdb

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

#include "yb/docdb/docdb.messages.h"

#include "yb/docdb/docdb-test.h"

namespace yb {
namespace docdb {

INSTANTIATE_TEST_CASE_P(DocDBTests,
                        DocDBTestWrapper,
                        testing::Values(TestDocDb::kQlReader, TestDocDb::kRedisReader));

TEST_P(DocDBTestWrapper, ExpiredValueCompactionTest) {
  const auto doc_key = MakeDocKey("k1");
  const MonoDelta one_ms = 1ms;
  const MonoDelta two_ms = 2ms;
  const HybridTime t0 = 1000_usec_ht;
  HybridTime t1 = t0.AddDelta(two_ms);
  HybridTime t2 = t1.AddDelta(two_ms);
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s1")),
      ValueControlFields {.ttl = one_ms}, ValueRef(QLValue::Primitive("v11")), t0));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s1")),
      QLValue::Primitive("v14"), t2));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s2")),
      ValueControlFields {.ttl = 3ms}, ValueRef(QLValue::Primitive("v21")), t0));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s2")),
      QLValue::Primitive("v24"), t2));

  // Note: HT{ physical: 1000 } + 4ms = HT{ physical: 5000 }
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 5000 }]) -> "v14"
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 1000 }]) -> "v11"; ttl: 0.001s
      SubDocKey(DocKey([], ["k1"]), ["s2"; HT{ physical: 5000 }]) -> "v24"
      SubDocKey(DocKey([], ["k1"]), ["s2"; HT{ physical: 1000 }]) -> "v21"; ttl: 0.003s
      )#");
  FullyCompactHistoryBefore(t1);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 5000 }]) -> "v14"
SubDocKey(DocKey([], ["k1"]), ["s2"; HT{ physical: 5000 }]) -> "v24"
SubDocKey(DocKey([], ["k1"]), ["s2"; HT{ physical: 1000 }]) -> "v21"; ttl: 0.003s
      )#");
}

ValueControlFields TtlWithMergeFlags(MonoDelta ttl) {
  return ValueControlFields {
    .merge_flags = ValueControlFields::kTtlFlag,
    .ttl = ttl,
  };
}

// Compaction testing with TTL merge records for generic Redis collections.
// Observe that because only collection-level merge records are supported,
// all tests begin with initializing a vanilla collection and adding TTL over it.
TEST_P(DocDBTestWrapper, RedisCollectionTTLCompactionTest) {
  const MonoDelta one_ms = 1ms;
  string key_string = "k0";
  string val_string = "v0";
  int n_times = 35;
  vector<HybridTime> t(n_times);
  t[0] = 1000_usec_ht;
  for (int i = 1; i < n_times; ++i) {
    t[i] = t[i-1].AddDelta(one_ms);
  }

  std::set<std::pair<string, string>> docdb_dump;
  auto time_iter = t.begin();

  // Stack 1
  InitializeCollection(key_string, &val_string, &time_iter, &docdb_dump);
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      ValueRef(ValueEntryType::kTombstone), *time_iter));
  ++time_iter;
  InitializeCollection(key_string, &val_string, &time_iter, &docdb_dump);
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(21ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(9ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  time_iter = t.begin();
  ++key_string[1];

  // Stack 2
  InitializeCollection(key_string, &val_string, &time_iter, &docdb_dump);
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(18ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(15ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      ValueRef(ValueEntryType::kTombstone), *time_iter));
  time_iter = t.begin();
  ++key_string[1];

  // Stack 3
  InitializeCollection(key_string, &val_string, &time_iter, &docdb_dump);
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(15ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(18ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  InitializeCollection(key_string, &val_string, &time_iter, &docdb_dump);
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(21ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(9ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(12ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  time_iter = t.begin();
  ++key_string[1];

  // Stack 4
  InitializeCollection(key_string, &val_string, &time_iter, &docdb_dump);
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(15ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(6ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(18ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  InitializeCollection(key_string, &val_string, &time_iter, &docdb_dump);
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(18ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(9ms), ValueRef(ValueEntryType::kObject),
      *time_iter));
  ++time_iter;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(ValueControlFields::kMaxTtl), ValueRef(ValueEntryType::kObject),
      *time_iter));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
  SubDocKey(DocKey([], ["k0"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 6000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 3000 }]) -> DEL
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "v0"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 4000 w: 2 }]) -> "v7"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 2000 }]) -> "v3"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 1000 w: 2 }]) -> "v1"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 4000 w: 3 }]) -> "v8"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "v4"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 1000 w: 3 }]) -> "v2"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "v5"
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 3000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k1"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "v<"
SubDocKey(DocKey([], ["k1"]), ["sk1"; HT{ physical: 2000 }]) -> "v?"
SubDocKey(DocKey([], ["k1"]), ["sk1"; HT{ physical: 1000 w: 2 }]) -> "v="
SubDocKey(DocKey([], ["k1"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "v@"
SubDocKey(DocKey([], ["k1"]), ["sk2"; HT{ physical: 1000 w: 3 }]) -> "v>"
SubDocKey(DocKey([], ["k1"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vA"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 3000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vB"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 5000 w: 2 }]) -> "vI"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 2000 }]) -> "vE"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 1000 w: 2 }]) -> "vC"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 5000 w: 3 }]) -> "vJ"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vF"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 1000 w: 3 }]) -> "vD"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vG"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 5000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.006s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 3000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vN"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 6000 w: 2 }]) -> "vU"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 2000 }]) -> "vQ"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 1000 w: 2 }]) -> "vO"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 6000 w: 3 }]) -> "vV"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vR"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 1000 w: 3 }]) -> "vP"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vS"
      )#");
  FullyCompactHistoryBefore(t[0]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 6000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 3000 }]) -> DEL
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "v0"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 4000 w: 2 }]) -> "v7"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 2000 }]) -> "v3"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 1000 w: 2 }]) -> "v1"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 4000 w: 3 }]) -> "v8"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "v4"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 1000 w: 3 }]) -> "v2"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "v5"
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 3000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k1"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "v<"
SubDocKey(DocKey([], ["k1"]), ["sk1"; HT{ physical: 2000 }]) -> "v?"
SubDocKey(DocKey([], ["k1"]), ["sk1"; HT{ physical: 1000 w: 2 }]) -> "v="
SubDocKey(DocKey([], ["k1"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "v@"
SubDocKey(DocKey([], ["k1"]), ["sk2"; HT{ physical: 1000 w: 3 }]) -> "v>"
SubDocKey(DocKey([], ["k1"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vA"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 3000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vB"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 5000 w: 2 }]) -> "vI"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 2000 }]) -> "vE"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 1000 w: 2 }]) -> "vC"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 5000 w: 3 }]) -> "vJ"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vF"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 1000 w: 3 }]) -> "vD"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vG"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 5000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.006s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 3000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vN"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 6000 w: 2 }]) -> "vU"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 2000 }]) -> "vQ"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 1000 w: 2 }]) -> "vO"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 6000 w: 3 }]) -> "vV"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vR"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 1000 w: 3 }]) -> "vP"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vS"
      )#");
  FullyCompactHistoryBefore(t[1]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 6000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 3000 }]) -> DEL
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "v0"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 4000 w: 2 }]) -> "v7"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 2000 }]) -> "v3"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 4000 w: 3 }]) -> "v8"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "v4"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "v5"
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 3000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k1"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "v<"
SubDocKey(DocKey([], ["k1"]), ["sk1"; HT{ physical: 2000 }]) -> "v?"
SubDocKey(DocKey([], ["k1"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "v@"
SubDocKey(DocKey([], ["k1"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vA"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 3000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vB"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 5000 w: 2 }]) -> "vI"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 2000 }]) -> "vE"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 5000 w: 3 }]) -> "vJ"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vF"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vG"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 5000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.006s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 3000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vN"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 6000 w: 2 }]) -> "vU"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 2000 }]) -> "vQ"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 6000 w: 3 }]) -> "vV"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vR"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vS"
      )#");
  FullyCompactHistoryBefore(t[2]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 6000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 4000 w: 2 }]) -> "v7"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 4000 w: 3 }]) -> "v8"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.015s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 1000 }]) -> {}; ttl: 0.020s
SubDocKey(DocKey([], ["k1"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "v<"
SubDocKey(DocKey([], ["k1"]), ["sk1"; HT{ physical: 2000 }]) -> "v?"
SubDocKey(DocKey([], ["k1"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "v@"
SubDocKey(DocKey([], ["k1"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vA"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 1000 }]) -> {}; ttl: 0.017s
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vB"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 5000 w: 2 }]) -> "vI"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 2000 }]) -> "vE"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 5000 w: 3 }]) -> "vJ"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vF"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vG"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 5000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 4000 }]) -> {}; merge flags: 1; ttl: 0.006s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 1000 }]) -> {}; ttl: 0.017s
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vN"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 6000 w: 2 }]) -> "vU"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 2000 }]) -> "vQ"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 6000 w: 3 }]) -> "vV"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vR"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vS"
      )#");
  FullyCompactHistoryBefore(t[3]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 6000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 4000 w: 2 }]) -> "v7"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 4000 w: 3 }]) -> "v8"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 1000 }]) -> {}; ttl: 0.018s
SubDocKey(DocKey([], ["k1"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "v<"
SubDocKey(DocKey([], ["k1"]), ["sk1"; HT{ physical: 2000 }]) -> "v?"
SubDocKey(DocKey([], ["k1"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "v@"
SubDocKey(DocKey([], ["k1"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vA"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 1000 }]) -> {}; ttl: 0.021s
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vB"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 5000 w: 2 }]) -> "vI"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 2000 }]) -> "vE"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 5000 w: 3 }]) -> "vJ"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vF"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vG"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 5000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 1000 }]) -> {}; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vN"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 6000 w: 2 }]) -> "vU"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 2000 }]) -> "vQ"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 6000 w: 3 }]) -> "vV"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vR"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vS"
      )#");
  FullyCompactHistoryBefore(t[4]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 6000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 5000 w: 2 }]) -> "vI"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 5000 w: 3 }]) -> "vJ"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 1000 }]) -> {}; ttl: 0.022s
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 1000 w: 1 }]) -> "vN"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 6000 w: 2 }]) -> "vU"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 2000 }]) -> "vQ"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 6000 w: 3 }]) -> "vV"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 2000 w: 1 }]) -> "vR"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 2000 w: 2 }]) -> "vS"
      )#");
  FullyCompactHistoryBefore(t[5]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}; ttl: 0.023s
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 7000 }]) -> {}; merge flags: 1; ttl: 0.021s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 6000 w: 2 }]) -> "vU"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 6000 w: 3 }]) -> "vV"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
      )#");
  FullyCompactHistoryBefore(t[6]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}; ttl: 0.012s
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}; ttl: 0.023s
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 8000 }]) -> {}; merge flags: 1; ttl: 0.018s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
      )#");
  FullyCompactHistoryBefore(t[7]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}; ttl: 0.012s
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}; ttl: 0.012s
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 9000 }]) -> {}; merge flags: 1; ttl: 0.009s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}; ttl: 0.020s
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
      )#");
  FullyCompactHistoryBefore(t[8]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}; ttl: 0.012s
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}; ttl: 0.016s
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 10000 }]) -> {}; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}; ttl: 0.012s
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
      )#");
  FullyCompactHistoryBefore(t[9]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 4000 }]) -> {}; ttl: 0.012s
SubDocKey(DocKey([], ["k0"]), ["sk0"; HT{ physical: 4000 w: 1 }]) -> "v6"
SubDocKey(DocKey([], ["k0"]), ["sk1"; HT{ physical: 5000 }]) -> "v9"
SubDocKey(DocKey([], ["k0"]), ["sk2"; HT{ physical: 5000 w: 1 }]) -> "v:"
SubDocKey(DocKey([], ["k0"]), ["sk3"; HT{ physical: 5000 w: 2 }]) -> "v;"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}; ttl: 0.016s
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
      )#");
  FullyCompactHistoryBefore(t[16]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 5000 }]) -> {}; ttl: 0.016s
SubDocKey(DocKey([], ["k2"]), ["sk0"; HT{ physical: 5000 w: 1 }]) -> "vH"
SubDocKey(DocKey([], ["k2"]), ["sk1"; HT{ physical: 6000 }]) -> "vK"
SubDocKey(DocKey([], ["k2"]), ["sk2"; HT{ physical: 6000 w: 1 }]) -> "vL"
SubDocKey(DocKey([], ["k2"]), ["sk3"; HT{ physical: 6000 w: 2 }]) -> "vM"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
      )#");
  FullyCompactHistoryBefore(t[21]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
      )#");
  FullyCompactHistoryBefore(t[34]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 6000 }]) -> {}
SubDocKey(DocKey([], ["k3"]), ["sk0"; HT{ physical: 6000 w: 1 }]) -> "vT"
SubDocKey(DocKey([], ["k3"]), ["sk1"; HT{ physical: 7000 }]) -> "vW"
SubDocKey(DocKey([], ["k3"]), ["sk2"; HT{ physical: 7000 w: 1 }]) -> "vX"
SubDocKey(DocKey([], ["k3"]), ["sk3"; HT{ physical: 7000 w: 2 }]) -> "vY"
      )#");
}

// Basic compaction testing for TTL in Redis.
TEST_P(DocDBTestWrapper, RedisTTLCompactionTest) {
  const MonoDelta one_ms = 1ms;
  string key_string = "k0";
  string val_string = "v0";
  int n_times = 20;
  vector<HybridTime> t(n_times);
  t[0] = 1000_usec_ht;
  for (int i = 1; i < n_times; ++i) {
    t[i] = t[i-1].AddDelta(one_ms);
  }
  // Compact at t10
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k0
                         Ttl(4ms), ValueRef(QLValue::Primitive(val_string)), t[2]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         Ttl(3ms), ValueRef(QLValue::Primitive(val_string)), t[0]));
  val_string[1]++;
  key_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k1
                         Ttl(8ms), ValueRef(QLValue::Primitive(val_string)), t[3]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         Ttl(1ms), ValueRef(QLValue::Primitive(val_string)), t[5]));
  val_string[1]++;
  key_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k2
                         Ttl(3ms), ValueRef(QLValue::Primitive(val_string)), t[5]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         Ttl(5ms), ValueRef(QLValue::Primitive(val_string)), t[7]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         ValueRef(QLValue::Primitive(val_string)), t[11]));
  key_string[1]++;
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k3
                         Ttl(4ms), ValueRef(QLValue::Primitive(val_string)), t[1]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         ValueRef(QLValue::Primitive(val_string)), t[4]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         Ttl(1ms), ValueRef(QLValue::Primitive(val_string)), t[13]));
  val_string[1]++;
  key_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k4
                         ValueRef(ValueEntryType::kTombstone), t[12]));
  key_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k5
                         Ttl(9ms), ValueRef(QLValue::Primitive(val_string)), t[8]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         ValueRef(ValueEntryType::kTombstone), t[9]));
  key_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k6
                         Ttl(9ms), ValueRef(QLValue::Primitive(val_string)), t[8]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         ValueRef(ValueEntryType::kTombstone), t[6]));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 3000 }]) -> "v0"; ttl: 0.004s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 1000 }]) -> "v1"; ttl: 0.003s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 6000 }]) -> "v3"; ttl: 0.001s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 4000 }]) -> "v2"; ttl: 0.008s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 12000 }]) -> "v6"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> "v5"; ttl: 0.005s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 6000 }]) -> "v4"; ttl: 0.003s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 14000 }]) -> "v9"; ttl: 0.001s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 5000 }]) -> "v8"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 2000 }]) -> "v7"; ttl: 0.004s
SubDocKey(DocKey([], ["k4"]), [HT{ physical: 13000 }]) -> DEL
SubDocKey(DocKey([], ["k5"]), [HT{ physical: 10000 }]) -> DEL
SubDocKey(DocKey([], ["k5"]), [HT{ physical: 9000 }]) -> "v:"; ttl: 0.009s
SubDocKey(DocKey([], ["k6"]), [HT{ physical: 9000 }]) -> "v;"; ttl: 0.009s
SubDocKey(DocKey([], ["k6"]), [HT{ physical: 7000 }]) -> DEL
      )#");
  FullyCompactHistoryBefore(t[10]);

  // Major compaction
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 12000 }]) -> "v6"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> "v5"; ttl: 0.005s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 14000 }]) -> "v9"; ttl: 0.001s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 5000 }]) -> "v8"
SubDocKey(DocKey([], ["k4"]), [HT{ physical: 13000 }]) -> DEL
SubDocKey(DocKey([], ["k6"]), [HT{ physical: 9000 }]) -> "v;"; ttl: 0.009s
      )#");

  FullyCompactHistoryBefore(t[14]);

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 12000 }]) -> "v6"
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 14000 }]) -> "v9"; ttl: 0.001s
SubDocKey(DocKey([], ["k6"]), [HT{ physical: 9000 }]) -> "v;"; ttl: 0.009s
      )#");

  FullyCompactHistoryBefore(t[19]);

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 12000 }]) -> "v6"
      )#");

  key_string = "k0";
  val_string = "v0";
  // Checking TTL rows now
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(), // k0
      TtlWithMergeFlags(6ms), ValueRef(QLValue::Primitive("")),
      t[5]));
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(4ms), ValueRef(QLValue::Primitive("")),
      t[2]));
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         Ttl(3ms), ValueRef(QLValue::Primitive(val_string)), t[0]));
  val_string[1]++;
  key_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k1
                         Ttl(8ms), ValueRef(QLValue::Primitive(val_string)), t[3]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(3ms), ValueRef(QLValue::Primitive("")),
      t[5]));
  key_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k2
                         Ttl(3ms), ValueRef(QLValue::Primitive(val_string)), t[5]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(5ms), ValueRef(QLValue::Primitive("")),
      t[7]));
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(ValueControlFields::kMaxTtl), ValueRef(QLValue::Primitive("")),
      t[12]));
  key_string[1]++;
  ASSERT_OK(SetPrimitive( // k3
  MakeDocKey(key_string).Encode(),
                         Ttl(4ms), ValueRef(QLValue::Primitive(val_string)), t[1]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(ValueControlFields::kMaxTtl), ValueRef(QLValue::Primitive("")),
      t[4]));
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         Ttl(1ms), ValueRef(QLValue::Primitive(val_string)), t[13]));
  val_string[1]++;
  key_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k4
                         ValueRef(ValueEntryType::kTombstone), t[12]));
  key_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(), // k5
                         Ttl(9ms), ValueRef(QLValue::Primitive(val_string)), t[8]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         ValueRef(ValueEntryType::kTombstone), t[9]));
  key_string[1]++;
  ASSERT_OK(SetPrimitive( // k6
      MakeDocKey(key_string).Encode(),
      TtlWithMergeFlags(4ms), ValueRef(QLValue::Primitive("")),
      t[10]));

  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         Ttl(9ms), ValueRef(QLValue::Primitive(val_string)), t[8]));
  val_string[1]++;
  ASSERT_OK(SetPrimitive(MakeDocKey(key_string).Encode(),
                         ValueRef(ValueEntryType::kTombstone), t[6]));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 6000 }]) -> ""; merge flags: 1; ttl: 0.006s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 3000 }]) -> ""; merge flags: 1; ttl: 0.004s
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 1000 }]) -> "v0"; ttl: 0.003s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 6000 }]) -> ""; merge flags: 1; ttl: 0.003s
SubDocKey(DocKey([], ["k1"]), [HT{ physical: 4000 }]) -> "v1"; ttl: 0.008s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 13000 }]) -> ""; merge flags: 1
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 12000 }]) -> "v6"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 8000 }]) -> ""; merge flags: 1; ttl: 0.005s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 6000 }]) -> "v2"; ttl: 0.003s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 14000 }]) -> "v4"; ttl: 0.001s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 5000 }]) -> ""; merge flags: 1
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 2000 }]) -> "v3"; ttl: 0.004s
SubDocKey(DocKey([], ["k4"]), [HT{ physical: 13000 }]) -> DEL
SubDocKey(DocKey([], ["k5"]), [HT{ physical: 10000 }]) -> DEL
SubDocKey(DocKey([], ["k5"]), [HT{ physical: 9000 }]) -> "v5"; ttl: 0.009s
SubDocKey(DocKey([], ["k6"]), [HT{ physical: 11000 }]) -> ""; merge flags: 1; ttl: 0.004s
SubDocKey(DocKey([], ["k6"]), [HT{ physical: 9000 }]) -> "v6"; ttl: 0.009s
SubDocKey(DocKey([], ["k6"]), [HT{ physical: 7000 }]) -> DEL
      )#");
  FullyCompactHistoryBefore(t[9]);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k0"]), [HT{ physical: 1000 }]) -> "v0"; ttl: 0.011s
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 13000 }]) -> ""; merge flags: 1
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 12000 }]) -> "v6"
SubDocKey(DocKey([], ["k2"]), [HT{ physical: 6000 }]) -> "v2"; ttl: 0.007s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 14000 }]) -> "v4"; ttl: 0.001s
SubDocKey(DocKey([], ["k3"]), [HT{ physical: 2000 }]) -> "v3"
SubDocKey(DocKey([], ["k4"]), [HT{ physical: 13000 }]) -> DEL
SubDocKey(DocKey([], ["k6"]), [HT{ physical: 11000 }]) -> ""; merge flags: 1; ttl: 0.004s
SubDocKey(DocKey([], ["k6"]), [HT{ physical: 9000 }]) -> "v6"; ttl: 0.009s
      )#");
}

TEST_P(DocDBTestWrapper, TTLCompactionTest) {
  // This test does not have schema, so cannot use with packed row.
  DisableYcqlPackedRow();

  const auto doc_key = MakeDocKey("k1");
  const MonoDelta one_ms = 1ms;
  const HybridTime t0 = 1000_usec_ht;
  HybridTime t1 = t0.AddDelta(one_ms);
  HybridTime t2 = t1.AddDelta(one_ms);
  HybridTime t3 = t2.AddDelta(one_ms);
  HybridTime t4 = t3.AddDelta(one_ms);
  KeyBytes encoded_doc_key(doc_key.Encode());
  // First row.
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::kLivenessColumn),
                         Ttl(1ms), ValueRef(ValueEntryType::kNullLow), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(0))),
                         Ttl(2ms), ValueRef(QLValue::Primitive("v1")), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(1))),
                         Ttl(3ms), ValueRef(QLValue::Primitive("v2")), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(2))),
                         QLValue::Primitive("v3"), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(3))),
                         QLValue::Primitive("v4"), t0));
  // Second row.
  const auto doc_key_row2 = MakeDocKey("k2");
  KeyBytes encoded_doc_key_row2(doc_key_row2.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key_row2, KeyEntryValue::kLivenessColumn),
                         Ttl(3ms), ValueRef(ValueEntryType::kNullLow), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key_row2, KeyEntryValue::MakeColumnId(ColumnId(0))),
                         Ttl(2ms), ValueRef(QLValue::Primitive("v1")), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key_row2, KeyEntryValue::MakeColumnId(ColumnId(1))),
                         Ttl(1ms), ValueRef(QLValue::Primitive("v2")), t0));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 0.001s
SubDocKey(DocKey([], ["k1"]), [ColumnId(0); HT{ physical: 1000 }]) -> "v1"; ttl: 0.002s
SubDocKey(DocKey([], ["k1"]), [ColumnId(1); HT{ physical: 1000 }]) -> "v2"; ttl: 0.003s
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT{ physical: 1000 }]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT{ physical: 1000 }]) -> "v4"
SubDocKey(DocKey([], ["k2"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 0.003s
SubDocKey(DocKey([], ["k2"]), [ColumnId(0); HT{ physical: 1000 }]) -> "v1"; ttl: 0.002s
SubDocKey(DocKey([], ["k2"]), [ColumnId(1); HT{ physical: 1000 }]) -> "v2"; ttl: 0.001s
      )#");

  FullyCompactHistoryBefore(t2);

  // Liveness column is gone for row1, v2 gone for row2.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(0); HT{ physical: 1000 }]) -> "v1"; ttl: 0.002s
SubDocKey(DocKey([], ["k1"]), [ColumnId(1); HT{ physical: 1000 }]) -> "v2"; ttl: 0.003s
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT{ physical: 1000 }]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT{ physical: 1000 }]) -> "v4"
SubDocKey(DocKey([], ["k2"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 0.003s
SubDocKey(DocKey([], ["k2"]), [ColumnId(0); HT{ physical: 1000 }]) -> "v1"; ttl: 0.002s
      )#");

  FullyCompactHistoryBefore(t3);

  // v1 is gone.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(1); HT{ physical: 1000 }]) -> "v2"; ttl: 0.003s
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT{ physical: 1000 }]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT{ physical: 1000 }]) -> "v4"
SubDocKey(DocKey([], ["k2"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 0.003s
      )#");

  FullyCompactHistoryBefore(t4);
  // v2 is gone for row 1, liveness column gone for row 2.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT{ physical: 1000 }]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT{ physical: 1000 }]) -> "v4"
      )#");

  // Delete values.
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(2))),
      ValueRef(ValueEntryType::kTombstone), t1));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(3))),
      ValueRef(ValueEntryType::kTombstone), t1));

  // Values are now marked with tombstones.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT{ physical: 2000 }]) -> DEL
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT{ physical: 1000 }]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT{ physical: 2000 }]) -> DEL
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT{ physical: 1000 }]) -> "v4"
      )#");

  FullyCompactHistoryBefore(t0);
  // Nothing is removed.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT{ physical: 2000 }]) -> DEL
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT{ physical: 1000 }]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT{ physical: 2000 }]) -> DEL
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT{ physical: 1000 }]) -> "v4"
      )#");

  FullyCompactHistoryBefore(t1);
  // Next compactions removes everything.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
      )#");
}

TEST_P(DocDBTestWrapper, TableTTLCompactionTest) {
  const auto doc_key = MakeDocKey("k1");
  const HybridTime t1 = 1000_usec_ht;
  const HybridTime t2 = 2000_usec_ht;
  const HybridTime t3 = 3000_usec_ht;
  const HybridTime t4 = 4000_usec_ht;
  const HybridTime t5 = 5000_usec_ht;
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s1")),
                         Ttl(1ms), ValueRef(QLValue::Primitive("v1")), t1));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s2")),
                         QLValue::Primitive("v2"), t1));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s3")),
                         Ttl(0ms), ValueRef(QLValue::Primitive("v3")), t2));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s4")),
                         Ttl(3ms), ValueRef(QLValue::Primitive("v4")), t1));
  // Note: HT{ physical: 1000 } + 1ms = HT{ physical: 4097000 }
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 1000 }]) -> "v1"; ttl: 0.001s
      SubDocKey(DocKey([], ["k1"]), ["s2"; HT{ physical: 1000 }]) -> "v2"
      SubDocKey(DocKey([], ["k1"]), ["s3"; HT{ physical: 2000 }]) -> "v3"; ttl: 0.000s
      SubDocKey(DocKey([], ["k1"]), ["s4"; HT{ physical: 1000 }]) -> "v4"; ttl: 0.003s
      )#");
  SetTableTTL(2);
  FullyCompactHistoryBefore(t3);

  // v1 compacted due to column level ttl.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), ["s2"; HT{ physical: 1000 }]) -> "v2"
SubDocKey(DocKey([], ["k1"]), ["s3"; HT{ physical: 2000 }]) -> "v3"; ttl: 0.000s
SubDocKey(DocKey([], ["k1"]), ["s4"; HT{ physical: 1000 }]) -> "v4"; ttl: 0.003s
      )#");

  FullyCompactHistoryBefore(t4);
  // v2 compacted due to table level ttl.
  // init marker compacted due to table level ttl.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), ["s3"; HT{ physical: 2000 }]) -> "v3"; ttl: 0.000s
SubDocKey(DocKey([], ["k1"]), ["s4"; HT{ physical: 1000 }]) -> "v4"; ttl: 0.003s
      )#");

  FullyCompactHistoryBefore(t5);
  // v4 compacted due to column level ttl.
  // v3 stays forever due to ttl being set to 0.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k1"]), ["s3"; HT{ physical: 2000 }]) -> "v3"; ttl: 0.000s
      )#");
}

Status InsertToWriteBatchWithTTL(DocWriteBatch* dwb, const MonoDelta ttl) {
  const auto doc_key = MakeDocKey("k1");
  KeyBytes encoded_doc_key(doc_key.Encode());
  QLValuePB subdoc;
  AddMapValue("sk1", "v1", &subdoc);

  return dwb->InsertSubDocument(
      DocPath(encoded_doc_key, KeyEntryValue("s1"), KeyEntryValue("s2")), ValueRef(subdoc),
      ReadOperationData(), rocksdb::kDefaultQueryId, ttl);
}

TEST_P(DocDBTestWrapper, TestUpdateDocWriteBatchTTL) {
  auto dwb = MakeDocWriteBatch();
  ThreadSafeArena arena;
  LWKeyValueWriteBatchPB kv_pb(&arena);
  dwb.TEST_CopyToWriteBatchPB(&kv_pb);
  ASSERT_FALSE(kv_pb.has_ttl());

  // Write a subdoc with kMaxTtl, which should not show up in the the kv ttl.
  ASSERT_OK(InsertToWriteBatchWithTTL(&dwb, ValueControlFields::kMaxTtl));
  dwb.TEST_CopyToWriteBatchPB(&kv_pb);
  ASSERT_FALSE(kv_pb.has_ttl());

  // Write a subdoc with 10s TTL, which should show up in the the kv ttl.
  ASSERT_OK(InsertToWriteBatchWithTTL(&dwb, 10s));
  dwb.TEST_CopyToWriteBatchPB(&kv_pb);
  ASSERT_EQ(kv_pb.ttl(), 10 * MonoTime::kNanosecondsPerSecond);

  // Write a subdoc with 5s TTL, which should make the kv ttl unchanged.
  ASSERT_OK(InsertToWriteBatchWithTTL(&dwb, 5s));
  dwb.TEST_CopyToWriteBatchPB(&kv_pb);
  ASSERT_EQ(kv_pb.ttl(), 10 * MonoTime::kNanosecondsPerSecond);

  // Write a subdoc with 15s TTL, which should show up in the the kv ttl.
  ASSERT_OK(InsertToWriteBatchWithTTL(&dwb, 15s));
  dwb.TEST_CopyToWriteBatchPB(&kv_pb);
  ASSERT_EQ(kv_pb.ttl(), 15 * MonoTime::kNanosecondsPerSecond);

  // Write a subdoc with kMaxTTL, which should make the kv ttl unchanged.
  ASSERT_OK(InsertToWriteBatchWithTTL(&dwb, ValueControlFields::kMaxTtl));
  dwb.TEST_CopyToWriteBatchPB(&kv_pb);
  ASSERT_EQ(kv_pb.ttl(), 15 * MonoTime::kNanosecondsPerSecond);
}

TEST_P(DocDBTestWrapper, TestCompactionForCollectionsWithTTL) {
  auto collection_key = MakeDocKey("collection");
  SetUpCollectionWithTTL(collection_key, UseIntermediateFlushes::kFalse);

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(ExpectedDebugDumpForCollectionWithTTL(
      collection_key, InitMarkerExpired::kFalse));

  FullyCompactHistoryBefore(HybridTime::FromMicros(1050 + 10 * 1000000));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(ExpectedDebugDumpForCollectionWithTTL(
      collection_key, InitMarkerExpired::kTrue));

  const auto subdoc_key = SubDocKey(collection_key).EncodeWithoutHt();
  SubDocument doc_from_rocksdb;
  bool subdoc_found_in_rocksdb = false;
  GetSubDoc(
      subdoc_key, &doc_from_rocksdb, &subdoc_found_in_rocksdb, kNonTransactionalOperationContext,
      ReadHybridTime::FromMicros(1200));
  ASSERT_TRUE(subdoc_found_in_rocksdb);

  for (int i = 0; i < kNumSubKeysForCollectionsWithTTL * 2; i++) {
    SubDocument subdoc;
    string key = "k" + std::to_string(i);
    string value = "vv" + std::to_string(i);
    ASSERT_EQ(value, doc_from_rocksdb.GetChild(KeyEntryValue(key))->GetString());
  }
}

TEST_P(DocDBTestWrapper, MinorCompactionsForCollectionsWithTTL) {
  ASSERT_OK(DisableCompactions());
  auto collection_key = MakeDocKey("c");
  SetUpCollectionWithTTL(collection_key, UseIntermediateFlushes::kTrue);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      ExpectedDebugDumpForCollectionWithTTL(collection_key, InitMarkerExpired::kFalse));
  MinorCompaction(
      HybridTime::FromMicros(1100 + 20 * 1000000 + 1), /* num_files_to_compact */ 2,
      /* start_index */ 1);

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["c"]), [HT{ physical: 1000 }]) -> {}; ttl: 10.000s               // file 1
SubDocKey(DocKey([], ["c"]), ["k0"; HT{ physical: 1100 }]) -> DEL                      // file 8
SubDocKey(DocKey([], ["c"]), ["k0"; HT{ physical: 1000 w: 1 }]) -> "v0"; ttl: 10.000s  // file 1
SubDocKey(DocKey([], ["c"]), ["k1"; HT{ physical: 1100 }]) -> "vv1"; ttl: 21.000s      // file 8
SubDocKey(DocKey([], ["c"]), ["k1"; HT{ physical: 1000 w: 2 }]) -> "v1"; ttl: 10.000s  // file 1
SubDocKey(DocKey([], ["c"]), ["k2"; HT{ physical: 1100 }]) -> "vv2"; ttl: 22.000s      // file 4
SubDocKey(DocKey([], ["c"]), ["k2"; HT{ physical: 1000 w: 3 }]) -> "v2"; ttl: 10.000s  // file 1
SubDocKey(DocKey([], ["c"]), ["k3"; HT{ physical: 1100 }]) -> "vv3"; ttl: 23.000s      // file 5
SubDocKey(DocKey([], ["c"]), ["k4"; HT{ physical: 1100 }]) -> "vv4"; ttl: 24.000s      // file 6
SubDocKey(DocKey([], ["c"]), ["k5"; HT{ physical: 1100 }]) -> "vv5"; ttl: 25.000s      // file 7
  )#");

  // Compact files 4, 5, 6, 7, 8. This should result in creation of a number of delete markers
  // from expired entries. Some expired entries from the first file will stay.
  MinorCompaction(
      HybridTime::FromMicros(1100 + 24 * 1000000 + 1), /* num_files_to_compact */ 5,
      /* start_index */ 1);

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["c"]), [HT{ physical: 1000 }]) -> {}; ttl: 10.000s               // file 1
SubDocKey(DocKey([], ["c"]), ["k0"; HT{ physical: 1100 }]) -> DEL                      // file 9
SubDocKey(DocKey([], ["c"]), ["k0"; HT{ physical: 1000 w: 1 }]) -> "v0"; ttl: 10.000s  // file 1
SubDocKey(DocKey([], ["c"]), ["k1"; HT{ physical: 1100 }]) -> DEL                      // file 9
SubDocKey(DocKey([], ["c"]), ["k1"; HT{ physical: 1000 w: 2 }]) -> "v1"; ttl: 10.000s  // file 1
SubDocKey(DocKey([], ["c"]), ["k2"; HT{ physical: 1100 }]) -> DEL                      // file 9
SubDocKey(DocKey([], ["c"]), ["k2"; HT{ physical: 1000 w: 3 }]) -> "v2"; ttl: 10.000s  // file 1
SubDocKey(DocKey([], ["c"]), ["k3"; HT{ physical: 1100 }]) -> DEL                      // file 9
SubDocKey(DocKey([], ["c"]), ["k4"; HT{ physical: 1100 }]) -> DEL                      // file 9
SubDocKey(DocKey([], ["c"]), ["k5"; HT{ physical: 1100 }]) -> "vv5"; ttl: 25.000s      // file 9
  )#");
}

} // namespace docdb
} // namespace yb

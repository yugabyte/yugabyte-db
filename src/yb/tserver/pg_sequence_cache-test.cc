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

#include "yb/tserver/pg_sequence_cache.h"

#include "yb/util/test_util.h"

namespace yb::tserver {

class PgSequenceCacheTest : public YBTest {
 protected:
  static constexpr PgOid kDbOid1 = 10001;
  static constexpr PgOid kDbOid2 = 10002;
  static constexpr PgOid kSeqOid1 = 20001;

  static MonoTime GetDeadline() { return MonoTime::Now() + MonoDelta::FromSeconds(5); }

  static Status FillCacheEntry(
      PgSequenceCache* cache, PgOid db_oid, PgOid sequence_oid, int64_t first, int64_t last) {
    auto entry = VERIFY_RESULT(
        cache->GetWhenAvailable(PgObjectId(db_oid, sequence_oid), GetDeadline()));
    entry->SetRange(first, last, cache->DatabaseEpoch(db_oid));
    entry->NotifyWaiter();
    return Status::OK();
  }

  static Result<std::optional<int64_t>> GetCachedValue(
      PgSequenceCache* cache, PgOid db_oid, PgOid sequence_oid) {
    auto entry = VERIFY_RESULT(
        cache->GetWhenAvailable(PgObjectId(db_oid, sequence_oid), GetDeadline()));
    auto value = entry->GetValueIfCached(/*inc_by=*/0, cache->DatabaseEpoch(db_oid));
    entry->NotifyWaiter();
    return value;
  }
};

TEST_F(PgSequenceCacheTest, BumpDatabaseEpoch) {
  PgSequenceCache cache;
  ASSERT_OK(FillCacheEntry(&cache, kDbOid1, kSeqOid1, /*first=*/10, /*last=*/20));
  ASSERT_OK(FillCacheEntry(&cache, kDbOid2, kSeqOid1, /*first=*/50, /*last=*/60));

  cache.BumpDatabaseEpoch(kDbOid1);

  auto db1_sequence = ASSERT_RESULT(GetCachedValue(&cache, kDbOid1, kSeqOid1));
  ASSERT_FALSE(db1_sequence.has_value());

  auto db2_sequence = ASSERT_RESULT(GetCachedValue(&cache, kDbOid2, kSeqOid1));
  ASSERT_TRUE(db2_sequence.has_value());
  ASSERT_EQ(*db2_sequence, 50);

  // A range stored under the new epoch is served.
  ASSERT_OK(FillCacheEntry(&cache, kDbOid1, kSeqOid1, /*first=*/100, /*last=*/110));
  auto refilled = ASSERT_RESULT(GetCachedValue(&cache, kDbOid1, kSeqOid1));
  ASSERT_TRUE(refilled.has_value());
  ASSERT_EQ(*refilled, 100);
}

TEST_F(PgSequenceCacheTest, SetRangeUnderPreviousEpoch) {
  PgSequenceCache cache;
  auto entry = ASSERT_RESULT(
      cache.GetWhenAvailable(PgObjectId(kDbOid1, kSeqOid1), GetDeadline()));
  const auto epoch_at_fetch = cache.DatabaseEpoch(kDbOid1);
  cache.BumpDatabaseEpoch(kDbOid1);

  // FetchSequenceTuple reads the current epoch at the start and skips SetRange when a bump follows
  // range allocation. A range stored under the epoch read at the start must still be invisible.
  entry->SetRange(/*first=*/10, /*last=*/20, epoch_at_fetch);
  entry->NotifyWaiter();

  auto value = ASSERT_RESULT(GetCachedValue(&cache, kDbOid1, kSeqOid1));
  ASSERT_FALSE(value.has_value());
}

TEST_F(PgSequenceCacheTest, BumpBeforeEntryCreation) {
  PgSequenceCache cache;
  cache.BumpDatabaseEpoch(kDbOid1);

  ASSERT_OK(FillCacheEntry(&cache, kDbOid1, kSeqOid1, /*first=*/5, /*last=*/8));
  auto value = ASSERT_RESULT(GetCachedValue(&cache, kDbOid1, kSeqOid1));
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(*value, 5);

  cache.BumpDatabaseEpoch(kDbOid1);
  value = ASSERT_RESULT(GetCachedValue(&cache, kDbOid1, kSeqOid1));
  ASSERT_FALSE(value.has_value());
}

}  // namespace yb::tserver

// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.

#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_protocol.messages.h"

#include "yb/util/memory/arena.h"
#include "yb/util/test_util.h"

#include "yb/yql/pggate/pg_sys_table_prefetcher.h"

namespace yb::pggate {

class PgSysTablePrefetcherTest : public YBTest {
 protected:
  PrefetcherOptions::CachingInfo CachingInfo(
      PrefetchingCacheMode mode, const ReadHybridTime& version_read_time) {
    return {
        .version_info = {
            .version = 1,
            .version_read_time = version_read_time,
            .is_db_catalog_version_mode = true,
        },
        .db_oid = kTemplate1Oid,
        .mode = mode,
    };
  }

  MissedPrefetchedDataAlternativeReadTime MissingTableReadTime(
      std::optional<PrefetcherOptions::CachingInfo> caching_info) {
    PgSysTablePrefetcher prefetcher({
        .caching_info = std::move(caching_info),
        .fetch_row_limit = 1024,
        .fetch_size_limit = 0,
    });
    ThreadSafeArena arena;
    LWPgsqlReadRequestPB request(&arena);
    request.dup_table_id(PgObjectId(kTemplate1Oid, 42).GetYbTableId());
    return std::get<MissedPrefetchedDataAlternativeReadTime>(
        prefetcher.GetData(request, false /* index_check_required */));
  }
};

TEST_F(PgSysTablePrefetcherTest, AuthCacheMissKeepsSnapshot) {
  for (const auto& version_read_time : {
           ReadHybridTime(), ReadHybridTime::SingleTime(HybridTime::FromMicros(1000))}) {
    SCOPED_TRACE(version_read_time.ToString());
    ASSERT_FALSE(MissingTableReadTime(
        CachingInfo(PrefetchingCacheMode::TRUST_CACHE_AUTH, version_read_time)));
  }
}

TEST_F(PgSysTablePrefetcherTest, CatalogCacheMissUsesVersionReadTime) {
  const auto version_read_time = ReadHybridTime::SingleTime(HybridTime::FromMicros(1000));
  const auto alternative = MissingTableReadTime(
      CachingInfo(PrefetchingCacheMode::TRUST_CACHE, version_read_time));
  ASSERT_TRUE(alternative);
  ASSERT_EQ(*alternative, version_read_time);
}

TEST_F(PgSysTablePrefetcherTest, RenewedCacheMissKeepsSnapshot) {
  const auto version_read_time = ReadHybridTime::SingleTime(HybridTime::FromMicros(1000));
  for (const auto mode : {PrefetchingCacheMode::RENEW_CACHE_SOFT,
                          PrefetchingCacheMode::RENEW_CACHE_HARD}) {
    SCOPED_TRACE(ToString(mode));
    ASSERT_FALSE(MissingTableReadTime(CachingInfo(mode, version_read_time)));
  }
}

TEST_F(PgSysTablePrefetcherTest, UncachedMissKeepsSnapshot) {
  ASSERT_FALSE(MissingTableReadTime(std::nullopt));
}

}  // namespace yb::pggate

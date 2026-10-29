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

#include "yb/master/pg_catalog_versions_cache.h"

#include "yb/common/pg_catversions.h"

#include "yb/util/test_util.h"

namespace yb::master {

namespace {

constexpr uint32_t kDb1 = 16384;
constexpr uint32_t kDb2 = 16385;

DbOidToCatalogVersionMap MakeVersions(
    std::initializer_list<std::pair<uint32_t, uint64_t>> entries) {
  DbOidToCatalogVersionMap result;
  for (const auto& [db_oid, version] : entries) {
    result.emplace(db_oid, PgCatalogVersion{version, version});
  }
  return result;
}

uint64_t Fingerprint(const DbOidToCatalogVersionMap& versions) {
  return FingerprintCatalogVersions<DbOidToCatalogVersionMap>(versions);
}

HybridTime Ht(uint64_t micros) { return HybridTime::FromMicros(micros); }

// Installs a snapshot the way a healthy caller would: capture the generation, then install.
bool Install(
    PgCatalogVersionsCache* cache, HybridTime read_ht, const DbOidToCatalogVersionMap& versions) {
  return cache->Install(
      cache->Generation(), read_ht, versions, Fingerprint(versions), false /* update_messages */,
      std::nullopt /* messages */);
}

// PgCatalogVersion has no operator==, so compare field by field rather than comparing maps.
void ExpectCachedVersions(
    const PgCatalogVersionsCache& cache, const DbOidToCatalogVersionMap& expected) {
  DbOidToCatalogVersionMap got;
  EXPECT_TRUE(cache.GetAll(&got, nullptr));
  EXPECT_EQ(got.size(), expected.size());
  for (const auto& [db_oid, want] : expected) {
    auto it = got.find(db_oid);
    if (it == got.end()) {
      ADD_FAILURE() << "missing db_oid " << db_oid;
      continue;
    }
    EXPECT_EQ(it->second.current_version, want.current_version);
    EXPECT_EQ(it->second.last_breaking_version, want.last_breaking_version);
  }
}

uint64_t CurrentVersion(const PgCatalogVersionsCache& cache, uint32_t db_oid) {
  uint64_t version = 0;
  EXPECT_TRUE(cache.GetForDb(db_oid, &version, nullptr));
  return version;
}

}  // namespace

class PgCatalogVersionsCacheTest : public YBTest {};

TEST_F(PgCatalogVersionsCacheTest, EmptyUntilFirstInstall) {
  PgCatalogVersionsCache cache;
  DbOidToCatalogVersionMap versions;
  uint64_t fingerprint = 0;
  // Before the first Install() only. Returning false here, rather than succeeding with an empty
  // map, is what makes GetYsqlAllDBCatalogVersions fall through to reading pg_yb_catalog_version.
  // After a Reset() the answer differs -- see ResetLeavesAnEmptyHit.
  ASSERT_FALSE(cache.GetAll(&versions, &fingerprint));
  ASSERT_FALSE(cache.GetForDb(kDb1, nullptr, nullptr));
  // Messages are the deliberate exception: an empty map is the right answer, because they are only
  // an optimization and a PG backend falls back to a full catalog refresh without them.
  ASSERT_TRUE(cache.GetInvalMessages().empty());
}

// What gets installed is exactly what comes back out, and both readers report the same thing.
// GetAll() supplies the catalog versions in heartbeat responses; GetForDb() supplies master-side
// catalog reads. Both read the same cached state, so they must never report different versions for
// the same database.
TEST_F(PgCatalogVersionsCacheTest, InstallThenRead) {
  PgCatalogVersionsCache cache;
  const auto v = MakeVersions({{kDb1, 5}, {kDb2, 7}});
  ASSERT_TRUE(Install(&cache, Ht(1000), v));

  ExpectCachedVersions(cache, v);

  DbOidToCatalogVersionMap got;
  uint64_t fingerprint = 0;
  HybridTime read_ht;
  ASSERT_TRUE(cache.GetAll(&got, &fingerprint, &read_ht));
  ASSERT_EQ(fingerprint, Fingerprint(v));
  ASSERT_EQ(read_ht, Ht(1000));
  ASSERT_EQ(CurrentVersion(cache, kDb1), 5);
  // An absent database is a miss, not version zero. GetForDb() leaves the out-params untouched on a
  // miss, so a caller that ignored the return value would read back whatever it had initialized
  // them to rather than anything from the cache.
  ASSERT_FALSE(cache.GetForDb(99999, nullptr, nullptr));
}

// The core invariant: the cache holds the newest snapshot any writer has read, so it can never
// report a version below one already broadcast to tservers from an equal-or-older snapshot.
TEST_F(PgCatalogVersionsCacheTest, OlderSnapshotDoesNotOverwriteNewer) {
  PgCatalogVersionsCache cache;
  ASSERT_TRUE(Install(&cache, Ht(2000), MakeVersions({{kDb1, 9}})));

  // A snapshot read earlier but arriving later must be dropped, even though it is a full and
  // internally consistent snapshot.
  ASSERT_FALSE(Install(&cache, Ht(1000), MakeVersions({{kDb1, 4}})));
  ASSERT_EQ(CurrentVersion(cache, kDb1), 9);

  // Equal read times are also dropped: there is nothing newer to gain, and installing would be
  // ordering by arrival rather than by snapshot time.
  ASSERT_FALSE(Install(&cache, Ht(2000), MakeVersions({{kDb1, 4}})));
  ASSERT_EQ(CurrentVersion(cache, kDb1), 9);

  // A genuinely newer snapshot still installs.
  ASSERT_TRUE(Install(&cache, Ht(3000), MakeVersions({{kDb1, 11}})));
  ASSERT_EQ(CurrentVersion(cache, kDb1), 11);
}

// A snapshot read before a Reset() must not be installed after it: the reset means this master
// stopped maintaining the cache, and the data predates whatever another leader has since done.
TEST_F(PgCatalogVersionsCacheTest, SnapshotReadBeforeResetIsDropped) {
  PgCatalogVersionsCache cache;
  ASSERT_TRUE(Install(&cache, Ht(1000), MakeVersions({{kDb1, 5}})));

  const auto generation_before_read = cache.Generation();
  cache.Reset();
  ASSERT_NE(cache.Generation(), generation_before_read);

  // Newer read time than anything cached, and the cache is empty, so only the generation check can
  // reject it.
  ASSERT_FALSE(cache.Install(
      generation_before_read, Ht(2000), MakeVersions({{kDb1, 6}}), 0 /* fingerprint */,
      false /* update_messages */, std::nullopt /* messages */));
  ASSERT_FALSE(cache.GetForDb(kDb1, nullptr, nullptr));

  // Reset() increments the generation. The same snapshot installs now because it carries
  // the post-reset generation.
  ASSERT_TRUE(Install(&cache, Ht(2000), MakeVersions({{kDb1, 6}})));
  ASSERT_EQ(CurrentVersion(cache, kDb1), 6);
}

// Reset must clear every field together. Leaving the read time behind would reject every later
// install; leaving the fingerprint behind suppressed an invalidation-messages re-read (#33596).
TEST_F(PgCatalogVersionsCacheTest, ResetClearsEverything) {
  PgCatalogVersionsCache cache;
  const auto v = MakeVersions({{kDb1, 5}});
  ASSERT_TRUE(cache.Install(
      cache.Generation(), Ht(5000), v, Fingerprint(v), true /* update_messages */,
      DbOidVersionToMessageListMap{{{kDb1, 5}, std::string("msg")}}));
  ASSERT_FALSE(cache.GetInvalMessages().empty());

  cache.Reset();

  ASSERT_FALSE(cache.GetForDb(kDb1, nullptr, nullptr));
  ASSERT_TRUE(cache.GetInvalMessages().empty());
  // Must come before the install below, which would overwrite fingerprint_ and make this pass
  // either way. Here it discriminates: an uncleared fingerprint would still equal Fingerprint(v)
  // and, with inval_messages_ an engaged empty map post-reset, would return false. This is the
  // assertion that covers #33596.
  ASSERT_TRUE(cache.NeedsMessagesRefresh(Fingerprint(v)));
  // An install with a read time *older* than the pre-reset one still succeeds, which is only
  // possible if the read time was cleared as well.
  ASSERT_TRUE(Install(&cache, Ht(4000), MakeVersions({{kDb1, 3}})));
  ASSERT_EQ(CurrentVersion(cache, kDb1), 3);
}

// GetAll() returns false only before the first Install(). After a Reset() it reports a hit with an
// empty map, which master_heartbeat_service sends as no catalog versions at all.
TEST_F(PgCatalogVersionsCacheTest, ResetLeavesAnEmptyHit) {
  PgCatalogVersionsCache cache;
  ASSERT_TRUE(Install(&cache, Ht(1000), MakeVersions({{kDb1, 5}})));
  cache.Reset();

  DbOidToCatalogVersionMap versions;
  ASSERT_TRUE(cache.GetAll(&versions, nullptr));
  ASSERT_TRUE(versions.empty());
  // GetForDb() has no way to say "hit, but empty", so it reports a miss.
  ASSERT_FALSE(cache.GetForDb(kDb1, nullptr, nullptr));
}

// Release-only: Install() reports an invalid read time with DFATAL, which aborts when DCHECKs are
// on, so there is no way to exercise the refusal off release.
#ifdef NDEBUG
TEST_F(PgCatalogVersionsCacheTest, InvalidReadTimeIsRefused) {
  PgCatalogVersionsCache cache;
  const auto v = MakeVersions({{kDb1, 5}});
  ASSERT_TRUE(Install(&cache, Ht(1000), v));

  // HybridTime::kInvalid is kMax - 1, so without an explicit check of is_valid() it would beat
  // every real snapshot and then disarm the ordering check for the next install.
  ASSERT_FALSE(cache.Install(
      cache.Generation(), HybridTime::kInvalid, MakeVersions({{kDb1, 99}}), 0 /* fingerprint */,
      false /* update_messages */, std::nullopt /* messages */));
  ASSERT_EQ(CurrentVersion(cache, kDb1), 5);
  // The ordering check must still be armed afterwards.
  ASSERT_FALSE(Install(&cache, Ht(500), MakeVersions({{kDb1, 1}})));
  ASSERT_EQ(CurrentVersion(cache, kDb1), 5);
}
#endif

TEST_F(PgCatalogVersionsCacheTest, MessagesUpdateModes) {
  PgCatalogVersionsCache cache;
  const auto v1 = MakeVersions({{kDb1, 5}});
  ASSERT_TRUE(cache.Install(
      cache.Generation(), Ht(1000), v1, Fingerprint(v1), true /* update_messages */,
      DbOidVersionToMessageListMap{{{kDb1, 5}, std::string("first")}}));
  ASSERT_EQ(cache.GetInvalMessages().size(), 1);

  // update_messages=false leaves the messages alone while the versions advance.
  const auto v2 = MakeVersions({{kDb1, 6}});
  ASSERT_TRUE(cache.Install(
      cache.Generation(), Ht(2000), v2, Fingerprint(v2), false /* update_messages */,
      std::nullopt /* messages */));
  ASSERT_EQ(cache.GetInvalMessages().size(), 1);

  // update_messages=true with nullopt marks them unavailable, which is what tells the next refresh
  // to re-read pg_yb_invalidation_messages.
  const auto v3 = MakeVersions({{kDb1, 7}});
  ASSERT_TRUE(cache.Install(
      cache.Generation(), Ht(3000), v3, Fingerprint(v3), true /* update_messages */,
      std::nullopt /* messages */));
  ASSERT_TRUE(cache.GetInvalMessages().empty());
  // Same fingerprint as what is cached, the messages need to be re-read because they are
  // unavailable.
  ASSERT_TRUE(cache.NeedsMessagesRefresh(Fingerprint(v3)));
}

TEST_F(PgCatalogVersionsCacheTest, NeedsMessagesRefreshFollowsFingerprint) {
  PgCatalogVersionsCache cache;
  const auto v = MakeVersions({{kDb1, 5}});
  ASSERT_TRUE(cache.Install(
      cache.Generation(), Ht(1000), v, Fingerprint(v), true /* update_messages */,
      DbOidVersionToMessageListMap{{{kDb1, 5}, std::string("m")}}));

  ASSERT_FALSE(cache.NeedsMessagesRefresh(Fingerprint(v)));
  // A new fingerprint means the versions moved, so the messages have to be re-read.
  ASSERT_TRUE(cache.NeedsMessagesRefresh(Fingerprint(MakeVersions({{kDb1, 6}}))));
}

}  // namespace yb::master

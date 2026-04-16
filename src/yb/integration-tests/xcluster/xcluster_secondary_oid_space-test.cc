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

#include <gtest/gtest.h>

#include "yb/client/client.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb {

class OidAllocationTest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() override {
    pgwrapper::PgMiniTestBase::SetUp();

    master::GetNamespaceInfoResponsePB resp;
    ASSERT_OK(client_->GetNamespaceInfo("yugabyte", YQL_DATABASE_PGSQL, &resp));
    namespace_id_ = resp.namespace_().id();

    google::SetVLOGLevel("catalog_manager*", 1);
  }

  NamespaceId namespace_id_;
};

TEST_F(OidAllocationTest, SimpleOidAllocation) {
  uint32_t begin_oid;
  uint32_t end_oid;

  auto ReservePgsqlOids = [&](uint32_t next_oid, uint32_t count, bool use_secondary_space) {
    return client_->ReservePgsqlOids(
        namespace_id_, next_oid, count, use_secondary_space, &begin_oid, &end_oid);
  };

  const uint32_t num_of_oids_to_request = 100;

  // Initial allocation.
  ASSERT_OK(ReservePgsqlOids(0, num_of_oids_to_request, false));
  ASSERT_GE(begin_oid, kPgFirstNormalObjectId);
  ASSERT_EQ(end_oid - begin_oid, num_of_oids_to_request);
  uint32_t previous_normal_end_oid = end_oid;
  ASSERT_OK(ReservePgsqlOids(0, num_of_oids_to_request, true));
  ASSERT_EQ(begin_oid, kPgFirstSecondarySpaceObjectId);
  ASSERT_EQ(end_oid - begin_oid, num_of_oids_to_request);
  uint32_t previous_secondary_end_oid = end_oid;

  // Continuing.
  ASSERT_OK(ReservePgsqlOids(0, num_of_oids_to_request, false));
  ASSERT_EQ(begin_oid, previous_normal_end_oid);
  ASSERT_EQ(end_oid - begin_oid, num_of_oids_to_request);
  ASSERT_OK(ReservePgsqlOids(0, num_of_oids_to_request, true));
  ASSERT_EQ(begin_oid, previous_secondary_end_oid);
  ASSERT_EQ(end_oid - begin_oid, num_of_oids_to_request);

  // Bumping to higher points.
  ASSERT_OK(ReservePgsqlOids(kPgFirstNormalObjectId + 100000, num_of_oids_to_request, false));
  ASSERT_EQ(begin_oid, kPgFirstNormalObjectId + 100000);
  ASSERT_EQ(end_oid - begin_oid, num_of_oids_to_request);
  ASSERT_OK(
      ReservePgsqlOids(kPgFirstSecondarySpaceObjectId + 100000, num_of_oids_to_request, true));
  ASSERT_EQ(begin_oid, kPgFirstSecondarySpaceObjectId + 100000);
  ASSERT_EQ(end_oid - begin_oid, num_of_oids_to_request);
}

TEST_F(OidAllocationTest, OidAllocationLimits) {
  uint32_t begin_oid;
  uint32_t end_oid;

  auto ReservePgsqlOids = [&](uint32_t next_oid, uint32_t count, bool use_secondary_space) {
    return client_->ReservePgsqlOids(
        namespace_id_, next_oid, count, use_secondary_space, &begin_oid, &end_oid);
  };

  const uint32_t num_of_oids_to_request = 100;

  // Up to limit.
  ASSERT_OK(ReservePgsqlOids(
      kPgUpperBoundNormalObjectId - num_of_oids_to_request, num_of_oids_to_request, false));
  ASSERT_EQ(end_oid, kPgUpperBoundNormalObjectId);
  ASSERT_EQ(end_oid - begin_oid, num_of_oids_to_request);
  ASSERT_OK(ReservePgsqlOids(
      kPgUpperBoundSecondarySpaceObjectId - num_of_oids_to_request, num_of_oids_to_request, true));
  ASSERT_EQ(end_oid, kPgUpperBoundSecondarySpaceObjectId);
  ASSERT_EQ(end_oid - begin_oid, num_of_oids_to_request);

  // Already at limit so no more OIDs available.
  ASSERT_NOK(ReservePgsqlOids(0, 1, false));
  ASSERT_NOK(ReservePgsqlOids(0, 1, true));

  // Try and bump beyond limit.
  ASSERT_NOK(ReservePgsqlOids(kPgUpperBoundNormalObjectId + 100, 1, false));
}

TEST_F(OidAllocationTest, OidAllocationOverlappingLimits) {
  uint32_t begin_oid;
  uint32_t end_oid;

  auto ReservePgsqlOids = [&](uint32_t next_oid, uint32_t count, bool use_secondary_space) {
    return client_->ReservePgsqlOids(
        namespace_id_, next_oid, count, use_secondary_space, &begin_oid, &end_oid);
  };

  // Close enough to limit that we get fewer than the OIDs we asked for.
  ASSERT_OK(ReservePgsqlOids(kPgUpperBoundNormalObjectId - 50 - 1, 100, false));
  ASSERT_EQ(end_oid, kPgUpperBoundNormalObjectId);
  ASSERT_OK(ReservePgsqlOids(kPgUpperBoundSecondarySpaceObjectId - 50 - 1, 100, true));
  ASSERT_EQ(end_oid, kPgUpperBoundSecondarySpaceObjectId);

  // Already at limit so no OIDs available.
  ASSERT_NOK(ReservePgsqlOids(0, 1, false));
  ASSERT_NOK(ReservePgsqlOids(0, 1, true));
}

TEST_F(OidAllocationTest, CacheInvalidation) {
  // Log information about OID reservations; search logs for "Reserve".
  google::SetVLOGLevel("catalog_manager*", 2);
  google::SetVLOGLevel("pg_client_service*", 2);

  auto conn = ASSERT_RESULT(Connect());
  auto AllocateOid = [&]() -> Result<uint32_t> {
    static int sequence_no = 0;
    sequence_no++;
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE SEQUENCE my_sequence_$0", sequence_no));
    uint32_t oid = VERIFY_RESULT(conn.FetchRow<pgwrapper::PGOid>(
        Format("SELECT oid FROM pg_class WHERE pg_class.relname = 'my_sequence_$0'", sequence_no)));
    LOG(INFO) << "allocated OID: " << oid;
    return oid;
  };

  uint32_t begin_oid;
  uint32_t end_oid;
  uint32_t oid_cache_invalidations_count;
  auto ReservePgsqlOids = [&](uint32_t next_oid, uint32_t count) {
    return client_->ReservePgsqlOids(
        namespace_id_, next_oid, count, /*use_secondary_space=*/false, &begin_oid, &end_oid,
        &oid_cache_invalidations_count);
  };

  // Ensure we have some OIDs cached and they are below the next allocation point we are going to
  // use.
  uint32_t oid = ASSERT_RESULT(AllocateOid());
  ASSERT_LT(oid, 50'000);

  // Reserve on master 100 OIDs at 100K without invalidating caches.  This should not affect the
  // next OIDs the TServer returns.
  ASSERT_OK(ReservePgsqlOids(100'000, 100));
  ASSERT_EQ(oid_cache_invalidations_count, 0);
  uint32_t oid1 = ASSERT_RESULT(AllocateOid());
  ASSERT_LT(oid1, 100'000);

  // Invalidate the caches; now when allocating we should see OIDs beyond the previous reservation.
  auto* catalog_manager_if = ASSERT_RESULT(catalog_manager());
  ASSERT_OK(catalog_manager_if->InvalidateTserverOidCaches());
  // We need to wait for each TServer to receive a heartbeat response from the master before their
  // caches will be effectively cleared.
  SleepFor(10s * kTimeMultiplier);
  uint32_t oid2 = ASSERT_RESULT(AllocateOid());
  ASSERT_GE(oid2, 100'100);

  // Reserve on master 100 OIDs at 200K and check if the invalidations count has been bumped.
  ASSERT_OK(ReservePgsqlOids(200'000, 100));
  ASSERT_EQ(oid_cache_invalidations_count, 1);
}

}  // namespace yb

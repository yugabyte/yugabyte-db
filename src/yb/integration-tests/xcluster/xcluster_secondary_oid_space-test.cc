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
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb {

class OidAllocationTest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() {
    pgwrapper::PgMiniTestBase::SetUp();

    master::GetNamespaceInfoResponsePB resp;
    ASSERT_OK(client_->GetNamespaceInfo(
        std::string() /* namespace_id */, "yugabyte", YQL_DATABASE_PGSQL, &resp));
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
        namespace_id_, next_oid, count, &begin_oid, &end_oid, use_secondary_space);
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
        namespace_id_, next_oid, count, &begin_oid, &end_oid, use_secondary_space);
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
        namespace_id_, next_oid, count, &begin_oid, &end_oid, use_secondary_space);
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

}  // namespace yb

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
// Tests for the client which are true unit tests and don't require a cluster, etc.

#include <array>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/client-internal.h"
#include "yb/client/retryable_request_tracker.h"
#include "yb/client/schema.h"

#include "yb/util/test_thread_holder.h"

namespace yb {
namespace client {

using std::string;
using std::vector;

using namespace std::literals;
using namespace std::placeholders;

const std::string kNoPrimaryKeyMessage = "Invalid argument: No primary key specified";

TEST(ClientUnitTest, TestSchemaBuilder_EmptySchema) {
  YBSchema s;
  YBSchemaBuilder b;
  ASSERT_EQ(kNoPrimaryKeyMessage, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_KeyNotSpecified) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(DataType::INT32)->NotNull();
  b.AddColumn("b")->Type(DataType::INT32)->NotNull();
  ASSERT_EQ(kNoPrimaryKeyMessage, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_DuplicateColumn) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(DataType::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("x")->Type(DataType::INT32);
  b.AddColumn("x")->Type(DataType::INT32);
  ASSERT_EQ("Invalid argument: Duplicate column name: x",
            b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_WrongPrimaryKeyOrder) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(DataType::INT32);
  b.AddColumn("x")->Type(DataType::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("x")->Type(DataType::INT32);
  const char *expected_status =
    "Invalid argument: Primary key column 'x' should be before regular column 'key'";
  ASSERT_EQ(expected_status, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_WrongHashKeyOrder) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(DataType::INT32)->PrimaryKey();
  b.AddColumn("b")->Type(DataType::INT32)->HashPrimaryKey();
  const char *expected_status =
    "Invalid argument: Hash primary key column 'b' should be before primary key 'a'";
  ASSERT_EQ(expected_status, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_SingleKey_GoodSchema) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(DataType::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("b")->Type(DataType::INT32);
  b.AddColumn("c")->Type(DataType::INT32)->NotNull();
  ASSERT_EQ("OK", b.Build(&s).ToString());
}

TEST(ClientUnitTest, RetryableRequestTrackerMaintainsMinimumActiveRequestId) {
  // A single stripe preserves the dense, exact-minimum behavior.
  internal::RetryableRequestTracker tracker(1);
  auto first = tracker.Register();
  auto second = tracker.Register();
  auto third = tracker.Register();

  ASSERT_EQ(first.request_id(), 0);
  ASSERT_EQ(second.request_id(), 1);
  ASSERT_EQ(third.request_id(), 2);
  ASSERT_EQ(first.min_running_request_id(), 0);
  ASSERT_EQ(second.min_running_request_id(), 0);
  ASSERT_EQ(third.min_running_request_id(), 0);

  std::array middle_registration = {&second};
  tracker.Unregister(middle_registration);
  ASSERT_EQ(tracker.TEST_ActiveRequestsCount(), 2);

  auto fourth = tracker.Register();
  ASSERT_EQ(fourth.request_id(), 3);
  ASSERT_EQ(fourth.min_running_request_id(), 0);

  std::array first_registration = {&first};
  tracker.Unregister(first_registration);
  auto fifth = tracker.Register();
  ASSERT_EQ(fifth.request_id(), 4);
  ASSERT_EQ(fifth.min_running_request_id(), 2);

  std::array remaining_registrations = {&third, &fourth, &fifth};
  tracker.Unregister(remaining_registrations);
  ASSERT_EQ(tracker.TEST_ActiveRequestsCount(), 0);

  auto sixth = tracker.Register();
  ASSERT_EQ(sixth.request_id(), 5);
  ASSERT_EQ(sixth.min_running_request_id(), 5);
  std::array sixth_registration = {&sixth};
  tracker.Unregister(sixth_registration);
}

TEST(ClientUnitTest, RetryableRequestTrackerTransfersMovedRegistration) {
  internal::RetryableRequestTracker tracker;
  auto registration = tracker.Register();
  auto moved_registration = std::move(registration);

  std::array registrations = {&moved_registration};
  tracker.Unregister(registrations);
  ASSERT_EQ(tracker.TEST_ActiveRequestsCount(), 0);
}

TEST(ClientUnitTest, RetryableRequestTrackerStripeCountRoundsUpToPowerOfTwo) {
  ASSERT_EQ(internal::RetryableRequestTracker(1).TEST_StripeCount(), 1);
  ASSERT_EQ(internal::RetryableRequestTracker(3).TEST_StripeCount(), 4);
  ASSERT_EQ(internal::RetryableRequestTracker(16).TEST_StripeCount(), 16);
  // 0 selects the flag default (16).
  ASSERT_EQ(internal::RetryableRequestTracker().TEST_StripeCount(), 16);
}

TEST(ClientUnitTest, RetryableRequestTrackerStripedIdsAreUniqueAndWatermarkConservative) {
  internal::RetryableRequestTracker tracker(4);
  const auto stripes = tracker.TEST_StripeCount();

  auto pinned = tracker.Register();
  std::vector<internal::RetryableRequestTracker::Registration> registrations;
  registrations.reserve(4 * stripes);
  std::unordered_set<RetryableRequestId> seen_ids = {pinned.request_id()};
  for (size_t i = 0; i != 4 * stripes; ++i) {
    auto registration = tracker.Register();
    // IDs are globally unique across stripes.
    ASSERT_TRUE(seen_ids.insert(registration.request_id()).second);
    // The watermark never exceeds the registration's own ID, nor the ID of a
    // request registered earlier and still active.
    ASSERT_LE(registration.min_running_request_id(), registration.request_id());
    ASSERT_LE(registration.min_running_request_id(), pinned.request_id());
    registrations.push_back(std::move(registration));
  }
  ASSERT_EQ(tracker.TEST_ActiveRequestsCount(), 1 + 4 * stripes);

  std::vector<internal::RetryableRequestTracker::Registration*> registration_ptrs;
  registration_ptrs.reserve(registrations.size());
  for (auto& registration : registrations) {
    registration_ptrs.push_back(&registration);
  }
  tracker.Unregister(registration_ptrs);
  std::array pinned_ptr = {&pinned};
  tracker.Unregister(pinned_ptr);
  ASSERT_EQ(tracker.TEST_ActiveRequestsCount(), 0);
}

TEST(ClientUnitTest, RetryableRequestTrackerWatermarkAdvancesAfterRetirement) {
  internal::RetryableRequestTracker tracker(4);
  const auto stripes = tracker.TEST_StripeCount();

  // The very first registration draws a stripe's initial ID, which is below
  // the stripe count.
  auto first = tracker.Register();
  ASSERT_LT(first.request_id(), static_cast<RetryableRequestId>(stripes));

  // One full round-robin cycle touches every stripe, advancing each next-ID
  // past its initial value.
  std::vector<internal::RetryableRequestTracker::Registration> cycle;
  cycle.reserve(stripes);
  for (size_t i = 0; i != stripes; ++i) {
    cycle.push_back(tracker.Register());
  }
  std::vector<internal::RetryableRequestTracker::Registration*> cycle_ptrs;
  for (auto& registration : cycle) {
    cycle_ptrs.push_back(&registration);
  }
  tracker.Unregister(cycle_ptrs);
  std::array first_ptr = {&first};
  tracker.Unregister(first_ptr);
  ASSERT_EQ(tracker.TEST_ActiveRequestsCount(), 0);

  // With every stripe used at least once and all requests retired, the
  // watermark has moved past the first request's ID.
  auto next = tracker.Register();
  ASSERT_GT(next.min_running_request_id(), first.request_id());
  ASSERT_GE(next.min_running_request_id(), static_cast<RetryableRequestId>(stripes));
  std::array next_ptr = {&next};
  tracker.Unregister(next_ptr);
}

TEST(ClientUnitTest, RetryableRequestTrackerConcurrentRegistrationAndRetirement) {
  constexpr size_t kNumThreads = 8;
  constexpr size_t kRequestsPerThread = 100;

  internal::RetryableRequestTracker tracker;
  // Stays registered for the whole test: no concurrently computed watermark
  // may ever exceed its ID.
  auto pinned = tracker.Register();
  const auto pinned_id = pinned.request_id();

  std::array<std::vector<RetryableRequestId>, kNumThreads> ids_by_thread;
  TestThreadHolder thread_holder;
  for (size_t thread_idx = 0; thread_idx != kNumThreads; ++thread_idx) {
    thread_holder.AddThreadFunctor([&tracker, pinned_id, &ids = ids_by_thread[thread_idx]] {
      std::vector<internal::RetryableRequestTracker::Registration> registrations;
      registrations.reserve(kRequestsPerThread);
      for (size_t request_idx = 0; request_idx != kRequestsPerThread; ++request_idx) {
        auto registration = tracker.Register();
        ASSERT_LE(registration.min_running_request_id(), registration.request_id());
        ASSERT_LE(registration.min_running_request_id(), pinned_id);
        ids.push_back(registration.request_id());
        registrations.push_back(std::move(registration));
      }

      std::vector<internal::RetryableRequestTracker::Registration*> registration_ptrs;
      registration_ptrs.reserve(registrations.size());
      for (auto& registration : registrations) {
        registration_ptrs.push_back(&registration);
      }
      tracker.Unregister(registration_ptrs);
    });
  }
  thread_holder.WaitAndStop(10s);

  std::unordered_set<RetryableRequestId> unique_ids = {pinned_id};
  for (const auto& ids : ids_by_thread) {
    ASSERT_EQ(ids.size(), kRequestsPerThread);
    for (const auto id : ids) {
      ASSERT_TRUE(unique_ids.insert(id).second) << "duplicate request id " << id;
    }
  }

  ASSERT_EQ(tracker.TEST_ActiveRequestsCount(), 1);
  std::array pinned_ptr = {&pinned};
  tracker.Unregister(pinned_ptr);
  ASSERT_EQ(tracker.TEST_ActiveRequestsCount(), 0);
}

} // namespace client
} // namespace yb

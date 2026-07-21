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
  internal::RetryableRequestTracker tracker;
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

TEST(ClientUnitTest, RetryableRequestTrackerConcurrentRegistrationAndRetirement) {
  constexpr size_t kNumThreads = 8;
  constexpr size_t kRequestsPerThread = 100;

  internal::RetryableRequestTracker tracker;
  TestThreadHolder thread_holder;
  for (size_t thread_idx = 0; thread_idx != kNumThreads; ++thread_idx) {
    thread_holder.AddThreadFunctor([&tracker] {
      std::vector<internal::RetryableRequestTracker::Registration> registrations;
      registrations.reserve(kRequestsPerThread);
      for (size_t request_idx = 0; request_idx != kRequestsPerThread; ++request_idx) {
        registrations.push_back(tracker.Register());
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

  ASSERT_EQ(tracker.TEST_ActiveRequestsCount(), 0);
  auto registration = tracker.Register();
  ASSERT_EQ(registration.request_id(), kNumThreads * kRequestsPerThread);
  ASSERT_EQ(registration.min_running_request_id(), registration.request_id());
  std::array registrations = {&registration};
  tracker.Unregister(registrations);
}

} // namespace client
} // namespace yb

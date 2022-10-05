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

#ifndef YB_MASTER_SYS_CATALOG_TEST_BASE_H_
#define YB_MASTER_SYS_CATALOG_TEST_BASE_H_

#include <gtest/gtest.h>

#include "yb/common/wire_protocol.h"
#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/server/rpc_server.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/result.h"
#include "yb/util/status_fwd.h"
#include "yb/util/test_util.h"

using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;

namespace yb {
namespace master {

class SysCatalogTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();

    // Start master with the create flag on.
    mini_master_.reset(
        new MiniMaster(Env::Default(), GetTestPath("Master"), AllocateFreePort(),
                       AllocateFreePort(), 0));
    ASSERT_OK(mini_master_->Start());
    master_ = mini_master_->master();
    ASSERT_OK(master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

    // Create a client proxy to it.
    MessengerBuilder bld("Client");
    client_messenger_ = ASSERT_RESULT(bld.Build());
    rpc::ProxyCache proxy_cache(client_messenger_.get());
  }

  void TearDown() override {
    if (client_messenger_) {
      client_messenger_->Shutdown();
    }
    mini_master_->Shutdown();
    YBTest::TearDown();
  }

  std::unique_ptr<Messenger> client_messenger_;
  std::unique_ptr<MiniMaster> mini_master_;
  Master* master_;
};

const int64_t kLeaderTerm = 1;

inline bool PbEquals(const google::protobuf::Message& a, const google::protobuf::Message& b) {
  return a.DebugString() == b.DebugString();
}

template<class C>
std::pair<std::string, std::string> AssertMetadataEqualsHelper(C* ti_a, C* ti_b) {
  auto l_a = ti_a->LockForRead();
  auto l_b = ti_b->LockForRead();
  return std::make_pair(l_a->pb.DebugString(), l_b->pb.DebugString());
}

// Similar to ASSERT_EQ but compares string representations of protobufs stored in two system
// catalog metadata objects.
//
// This uses a gtest internal macro for user-friendly output, as it shows the expressions passed to
// ASSERT_METADATA_EQ the same way ASSERT_EQ would show them. If the internal macro stops working
// the way it does now with a future version of gtest, it could be replaced with:
//
// ASSERT_EQ(string_reps.first, string_reps.second)
//     << "Expecting string representations of metadata protobufs to be the same for "
//     << #a << " and " << #b;

#define ASSERT_METADATA_EQ(a, b) do { \
    auto string_reps = AssertMetadataEqualsHelper((a), (b)); \
    GTEST_ASSERT_( \
      ::testing::internal::EqHelper::Compare \
          (#a, #b, string_reps.first, string_reps.second), \
          GTEST_FATAL_FAILURE_); \
  } while (false)



} // namespace master
} // namespace yb

#endif // YB_MASTER_SYS_CATALOG_TEST_BASE_H_

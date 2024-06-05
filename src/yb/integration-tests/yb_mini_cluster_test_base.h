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

#pragma once

#include <memory>

#include "yb/util/test_util.h"

#include "yb/client/client.h"

namespace yb {

namespace test {

YB_DEFINE_ENUM(Partitioning, (kHash)(kRange))

}

template <class T>
class YBMiniClusterTestBase: public YBTest {
 public:
  virtual void SetUp() override;

  // We override TearDown finally here, because we need to make sure DoBeforeTearDown is always
  // called before actual tear down which is to be performed in DoTearDown. Subclasses should
  // override DoTearDown if they need to customize tear down behaviour.
  // Subclasses should never call TearDown of YBMiniClusterTestBase, they should call DoTearDown
  // instead.
  //
  // Actually, subclasses should never know if TearDown method even exists, they should use/override
  // DoTearDown instead. For YBMiniClusterTestBase and its subclasses TearDown method is only
  // intended for calling from test framework and is a part of exclusively external interface.
  virtual void TearDown() override final;

  // In some tests we don't want to verify cluster at the end, because it can be broken on purpose.
  void DontVerifyClusterBeforeNextTearDown();

 protected:
  std::unique_ptr<T> cluster_;
  bool verify_cluster_before_next_tear_down_;
  bool saved_use_priority_thread_pool_for_flushes_;

  virtual void DoTearDown();
  virtual void DoBeforeTearDown();
};

template <class T>
class MiniClusterTestWithClient : public YBMiniClusterTestBase<T> {
 public:
  // Create a new YB session
  virtual client::YBSessionPtr NewSession();

 protected:
  virtual Status CreateClient();

  // Creates the client only if it has not been created before.
  virtual Status EnsureClientCreated();

  void DoTearDown() override;

  std::unique_ptr<client::YBClient> client_;
};

} // namespace yb

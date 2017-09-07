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

#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster.h"

///////////////////////////////////////////////////
// YBMiniClusterTestBase
///////////////////////////////////////////////////

namespace yb {

template <class T>
void YBMiniClusterTestBase<T>::SetUp() {
  YBTest::SetUp();
  verify_cluster_before_next_tear_down_ = true;
}

template <class T>
void YBMiniClusterTestBase<T>::TearDown() {
  DoBeforeTearDown();
  DoTearDown();
}

template <class T>
void YBMiniClusterTestBase<T>::DoBeforeTearDown() {
  if (cluster_ && verify_cluster_before_next_tear_down_) {
    LOG(INFO) << "Checking cluster consistency...";
    ASSERT_NO_FATALS(ClusterVerifier(cluster_.get()).CheckCluster());
  }
}

template <class T>
void YBMiniClusterTestBase<T>::DoTearDown() {
  YBTest::TearDown();
}

template <class T>
void YBMiniClusterTestBase<T>::DontVerifyClusterBeforeNextTearDown() {
  verify_cluster_before_next_tear_down_ = false;
}

// Instantiate explicitly to avoid recompilation a lot of dependent test class due to template
// implementation changes.
template class YBMiniClusterTestBase<MiniCluster>;
template class YBMiniClusterTestBase<ExternalMiniCluster>;

} // namespace yb

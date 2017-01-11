// Copyright (c) YugaByte, Inc.

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
    NO_FATALS(ClusterVerifier(cluster_.get()).CheckCluster());
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

//
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
//

#include "yb/client/ql-dml-test-base.h"

namespace yb {
namespace client {

const client::YBTableName kTableName("my_keyspace", "ql_client_test_table");

Status FlushSession(YBSession *session) {
  Synchronizer s;
  YBStatusMemberCallback<Synchronizer> cb(&s, &Synchronizer::StatusCB);
  session->FlushAsync(&cb);
  return s.Wait();
}

void QLDmlTestBase::SetUp() {
  YBMiniClusterTestBase::SetUp();

  // Start minicluster and wait for tablet servers to connect to master.
  MiniClusterOptions opts;
  opts.num_tablet_servers = 3;
  cluster_.reset(new MiniCluster(env_.get(), opts));
  ASSERT_OK(cluster_->Start());

  // Connect to the cluster.
  ASSERT_OK(cluster_->CreateClient(nullptr, &client_));

  // Create test table
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name()));
}

void QLDmlTestBase::DoTearDown() {
  // If we enable this, it will break FLAGS_mini_cluster_reuse_data
  //
  // This DeleteTable clean up seems to cause a crash because the delete may not succeed
  // immediately and is retried after the master is restarted (see ENG-663). So disable it for
  // now.
  //
  // if (table_) {
  //   ASSERT_OK(client_->DeleteTable(kTableName));
  // }
  if (cluster_) {
    cluster_->Shutdown();
    cluster_.reset();
  }
  YBMiniClusterTestBase::DoTearDown();
}

} // namespace client
} // namespace yb

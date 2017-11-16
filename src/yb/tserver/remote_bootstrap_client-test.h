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

#ifndef YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_TEST_H_
#define YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_TEST_H_

#include "yb/tserver/remote_bootstrap-test-base.h"

#include "yb/consensus/quorum_util.h"
#include "yb/gutil/strings/fastmem.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tserver/remote_bootstrap_client.h"
#include "yb/util/env_util.h"

using std::shared_ptr;

namespace yb {
namespace tserver {

using consensus::GetRaftConfigLeader;
using consensus::RaftPeerPB;
using tablet::TabletMetadata;
using tablet::TabletStatusListener;

class RemoteBootstrapClientTest : public RemoteBootstrapTest {
 public:
  explicit RemoteBootstrapClientTest(TableType table_type = DEFAULT_TABLE_TYPE)
      : RemoteBootstrapTest(table_type) {}

  virtual void SetUp() override {
    RemoteBootstrapTest::SetUp();

    fs_manager_.reset(new FsManager(Env::Default(), GetTestPath("client_tablet"), "tserver_test"));
    ASSERT_OK(fs_manager_->CreateInitialFileSystemLayout());
    ASSERT_OK(fs_manager_->Open());

    ASSERT_OK(tablet_peer_->WaitUntilConsensusRunning(MonoDelta::FromSeconds(10.0)));
    ASSERT_OK(rpc::MessengerBuilder(CURRENT_TEST_NAME()).Build().MoveTo(&messenger_));
    client_.reset(new RemoteBootstrapClient(GetTabletId(),
                                            fs_manager_.get(),
                                            messenger_,
                                            fs_manager_->uuid()));
    ASSERT_OK(GetRaftConfigLeader(tablet_peer_->consensus()
        ->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED), &leader_));

    HostPort host_port;
    HostPortFromPB(leader_.last_known_addr(), &host_port);
    ASSERT_OK(client_->Start(leader_.permanent_uuid(), host_port, &meta_));
  }

 protected:
  CHECKED_STATUS CompareFileContents(const string& path1, const string& path2);

  gscoped_ptr<FsManager> fs_manager_;
  shared_ptr<rpc::Messenger> messenger_;
  gscoped_ptr<RemoteBootstrapClient> client_;
  scoped_refptr<TabletMetadata> meta_;
  RaftPeerPB leader_;
};

Status RemoteBootstrapClientTest::CompareFileContents(const string& path1, const string& path2) {
  shared_ptr<RandomAccessFile> file1, file2;
  RETURN_NOT_OK(env_util::OpenFileForRandom(fs_manager_->env(), path1, &file1));
  RETURN_NOT_OK(env_util::OpenFileForRandom(fs_manager_->env(), path2, &file2));

  uint64_t size1, size2;
  RETURN_NOT_OK(file1->Size(&size1));
  RETURN_NOT_OK(file2->Size(&size2));
  if (size1 != size2) {
    return STATUS(Corruption, "Sizes of files don't match",
                              strings::Substitute("$0 vs $1 bytes", size1, size2));
  }

  Slice slice1, slice2;
  faststring scratch1, scratch2;
  scratch1.resize(size1);
  scratch2.resize(size2);
  RETURN_NOT_OK(env_util::ReadFully(file1.get(), 0, size1, &slice1, scratch1.data()));
  RETURN_NOT_OK(env_util::ReadFully(file2.get(), 0, size2, &slice2, scratch2.data()));
  int result = strings::fastmemcmp_inlined(slice1.data(), slice2.data(), size1);
  if (result != 0) {
    return STATUS(Corruption, "Files do not match");
  }
  return Status::OK();
}

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_TEST_H_

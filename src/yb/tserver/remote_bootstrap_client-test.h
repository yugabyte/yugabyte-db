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

#pragma once

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/quorum_util.h"

#include "yb/gutil/strings/fastmem.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/tablet/tablet_bootstrap_if.h"

#include "yb/tserver/remote_bootstrap_client.h"
#include "yb/tserver/remote_bootstrap-test-base.h"

#include "yb/util/env_util.h"
#include "yb/util/net/net_util.h"


namespace yb {
namespace tserver {

using consensus::GetRaftConfigLeader;
using consensus::RaftPeerPB;
using tablet::RaftGroupMetadata;
using tablet::RaftGroupMetadataPtr;
using tablet::TabletStatusListener;

class RemoteBootstrapClientTest : public RemoteBootstrapTest {
 public:
  explicit RemoteBootstrapClientTest(TableType table_type = DEFAULT_TABLE_TYPE)
      : RemoteBootstrapTest(table_type) {}

  void SetUp() override {
    RemoteBootstrapTest::SetUp();

    fs_manager_.reset(new FsManager(Env::Default(), GetTestPath("client_tablet"), "tserver_test"));
    ASSERT_OK(fs_manager_->CreateInitialFileSystemLayout());
    ASSERT_OK(fs_manager_->CheckAndOpenFileSystemRoots());

    ASSERT_OK(tablet_peer_->WaitUntilConsensusRunning(MonoDelta::FromSeconds(10.0)));
    SetUpRemoteBootstrapClient();
  }

  void TearDown() override {
    messenger_->Shutdown();
    RemoteBootstrapTest::TearDown();
  }

  virtual void SetUpRemoteBootstrapClient() {
    messenger_ = ASSERT_RESULT(rpc::MessengerBuilder(CURRENT_TEST_NAME()).Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());

    client_ = std::make_unique<RemoteBootstrapClient>(GetTabletId(), fs_manager_.get());
    ASSERT_OK(GetRaftConfigLeader(
        ASSERT_RESULT(tablet_peer_->GetConsensus())
            ->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED),
        &leader_));

    HostPort host_port = HostPortFromPB(leader_.last_known_private_addr()[0]);
    ASSERT_OK(client_->Start(leader_.permanent_uuid(), proxy_cache_.get(),
        host_port, ServerRegistrationPB(), &meta_));
  }

 protected:
  Status CompareFileContents(const std::string& path1, const std::string& path2);

  std::unique_ptr<FsManager> fs_manager_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  std::unique_ptr<RemoteBootstrapClient> client_;
  RaftGroupMetadataPtr meta_;
  RaftPeerPB leader_;
};

Status RemoteBootstrapClientTest::CompareFileContents(
    const std::string& path1, const std::string& path2) {
  std::shared_ptr<RandomAccessFile> file1, file2;
  RETURN_NOT_OK(env_util::OpenFileForRandom(fs_manager_->env(), path1, &file1));
  RETURN_NOT_OK(env_util::OpenFileForRandom(fs_manager_->env(), path2, &file2));

  uint64_t size1 = VERIFY_RESULT(file1->Size());
  uint64_t size2 = VERIFY_RESULT(file2->Size());
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

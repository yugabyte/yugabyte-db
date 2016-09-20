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
#ifndef YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_TEST_H_
#define YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_TEST_H_

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <memory>

#include "yb/tablet/tablet-test-util.h"

#include "yb/common/partial_row.h"
#include "yb/common/row_operations.h"
#include "yb/common/schema.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/log.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/opid_util.h"
#include "yb/fs/block_id.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/fastmem.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/messenger.h"
#include "yb/tserver/remote_bootstrap_session.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/crc.h"
#include "yb/util/metrics.h"
#include "yb/util/test_util.h"
#include "yb/util/threadpool.h"

METRIC_DECLARE_entity(tablet);

using std::shared_ptr;
using std::string;

namespace yb {
namespace tserver {

using std::unique_ptr;
using consensus::ConsensusMetadata;
using consensus::OpId;
using consensus::RaftConfigPB;
using consensus::RaftPeerPB;
using fs::ReadableBlock;
using log::Log;
using log::LogOptions;
using log::LogAnchorRegistry;
using rpc::Messenger;
using rpc::MessengerBuilder;
using strings::Substitute;
using tablet::ColumnDataPB;
using tablet::DeltaDataPB;
using tablet::YBTabletTest;
using tablet::RowSetDataPB;
using tablet::TabletPeer;
using tablet::TabletSuperBlockPB;
using tablet::WriteTransactionState;

class RemoteBootstrapTest : public YBTabletTest {
 public:
  explicit RemoteBootstrapTest(TableType table_type)
    : YBTabletTest(Schema({ ColumnSchema("key", STRING),
                            ColumnSchema("val", INT32) }, 1),
                   table_type) {
    CHECK_OK(ThreadPoolBuilder("test-exec").Build(&apply_pool_));
  }

  virtual void SetUp() OVERRIDE {
    YBTabletTest::SetUp();
    SetUpTabletPeer();
    ASSERT_NO_FATAL_FAILURE(PopulateTablet());
    InitSession();
  }

  virtual void TearDown() OVERRIDE {
    session_.reset();
    tablet_peer_->Shutdown();
    YBTabletTest::TearDown();
  }

 protected:
  void SetUpTabletPeer() {
    scoped_refptr<Log> log;
    CHECK_OK(Log::Open(LogOptions(), fs_manager(), tablet()->tablet_id(),
                       *tablet()->schema(),
                       0,  // schema_version
                       NULL, &log));

    scoped_refptr<MetricEntity> metric_entity =
      METRIC_ENTITY_tablet.Instantiate(&metric_registry_, CURRENT_TEST_NAME());

    RaftPeerPB config_peer;
    config_peer.set_permanent_uuid(fs_manager()->uuid());
    config_peer.set_member_type(RaftPeerPB::VOTER);

    tablet_peer_.reset(
        new TabletPeer(tablet()->metadata(),
                       config_peer,
                       apply_pool_.get(),
                       Bind(&RemoteBootstrapTest::TabletPeerStateChangedCallback,
                            Unretained(this),
                            tablet()->tablet_id())));

    // TODO similar to code in tablet_peer-test, consider refactor.
    RaftConfigPB config;
    config.set_local(true);
    config.add_peers()->CopyFrom(config_peer);
    config.set_opid_index(consensus::kInvalidOpIdIndex);

    gscoped_ptr<ConsensusMetadata> cmeta;
    CHECK_OK(ConsensusMetadata::Create(tablet()->metadata()->fs_manager(),
                                       tablet()->tablet_id(), fs_manager()->uuid(),
                                       config, consensus::kMinimumTerm, &cmeta));

    shared_ptr<Messenger> messenger;
    MessengerBuilder mbuilder(CURRENT_TEST_NAME());
    mbuilder.Build(&messenger);

    log_anchor_registry_.reset(new LogAnchorRegistry());
    tablet_peer_->SetBootstrapping();
    CHECK_OK(tablet_peer_->Init(tablet(),
                                clock(),
                                messenger,
                                log,
                                metric_entity));
    consensus::ConsensusBootstrapInfo boot_info;
    CHECK_OK(tablet_peer_->Start(boot_info));

    ASSERT_OK(tablet_peer_->WaitUntilConsensusRunning(MonoDelta::FromSeconds(2)));
  }

  void TabletPeerStateChangedCallback(const string& tablet_id,
                                      std::shared_ptr<consensus::StateChangeContext> context) {
    LOG(INFO) << "Tablet peer state changed for tablet " << tablet_id
              << ". Reason: " << context->ToString();
  }

  void PopulateTablet() {
    for (int32_t i = 0; i < 1000; i++) {
      unique_ptr<WriteRequestPB> req(new WriteRequestPB());
      req->set_tablet_id(tablet_peer_->tablet_id());
      ASSERT_OK(SchemaToPB(client_schema_, req->mutable_schema()));
      RowOperationsPB* data = req->mutable_row_operations();
      RowOperationsPBEncoder enc(data);
      YBPartialRow row(&client_schema_);

      string key = Substitute("key$0", i);
      ASSERT_OK(row.SetString(0, key));
      ASSERT_OK(row.SetInt32(1, i));
      enc.Add(RowOperationsPB::INSERT, row);

      WriteResponsePB resp;
      CountDownLatch latch(1);

      unique_ptr<const WriteRequestPB> key_value_write_request;
      std::vector<std::string> keys_locked;

      if (table_type_ == TableType::KEY_VALUE_TABLE_TYPE) {
        Status s = tablet()->CreateKeyValueWriteRequestPB(
            *req.get(), &key_value_write_request, &keys_locked);
        assert(s.ok());
      } else {
        key_value_write_request = std::move(req);
      }

      auto state = new WriteTransactionState(
          tablet_peer_.get(), key_value_write_request.get(), &resp);
      if (table_type_ == TableType::KEY_VALUE_TABLE_TYPE) {
        state->swap_docdb_locks(&keys_locked);
      }
      state->set_completion_callback(gscoped_ptr<tablet::TransactionCompletionCallback>(
          new tablet::LatchTransactionCompletionCallback<WriteResponsePB>(&latch, &resp)).Pass());
      ASSERT_OK(tablet_peer_->SubmitWrite(state));
      latch.Wait();
      ASSERT_FALSE(resp.has_error()) << "Request failed: " << resp.error().ShortDebugString();
      ASSERT_EQ(0, resp.per_row_errors_size()) << "Insert error: " << resp.ShortDebugString();
    }
    ASSERT_OK(tablet()->Flush());
  }

  void InitSession() {
    session_.reset(new RemoteBootstrapSession(tablet_peer_.get(), "TestSession", "FakeUUID",
                   fs_manager()));
    CHECK_OK(session_->Init());
  }

  // Read the specified BlockId, via the RemoteBootstrapSession, into a file.
  // 'path' will be populated with the name of the file used.
  // 'file' will be set to point to the SequentialFile containing the data.
  void FetchBlockToFile(const BlockId& block_id,
                        string* path,
                        gscoped_ptr<SequentialFile>* file) {
    string data;
    int64_t block_file_size = 0;
    RemoteBootstrapErrorPB::Code error_code;
    CHECK_OK(session_->GetBlockPiece(block_id, 0, 0, &data, &block_file_size, &error_code));
    if (block_file_size > 0) {
      CHECK_GT(data.size(), 0);
    }

    // Write the file to a temporary location.
    WritableFileOptions opts;
    string path_template = GetTestPath(Substitute("test_block_$0.tmp.XXXXXX", block_id.ToString()));
    gscoped_ptr<WritableFile> writable_file;
    CHECK_OK(Env::Default()->NewTempWritableFile(opts, path_template, path, &writable_file));
    CHECK_OK(writable_file->Append(Slice(data.data(), data.size())));
    CHECK_OK(writable_file->Close());

    CHECK_OK(Env::Default()->NewSequentialFile(*path, file));
  }

  MetricRegistry metric_registry_;
  scoped_refptr<LogAnchorRegistry> log_anchor_registry_;
  gscoped_ptr<ThreadPool> apply_pool_;
  scoped_refptr<TabletPeer> tablet_peer_;
  scoped_refptr<RemoteBootstrapSession> session_;
};

}  // namespace tserver
}  // namespace yb

#endif  // YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_TEST_H_

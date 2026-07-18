// Copyright (c) YugabyteDB, Inc.
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

#include "yb/fs/fs_manager.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/remote_bootstrap_file_downloader.h"
#include "yb/tserver/remote_bootstrap.pb.h"

#include "yb/util/crc.h"
#include "yb/util/test_util.h"

DECLARE_int64(remote_bootstrap_rate_limit_bytes_per_sec);
DECLARE_int32(remote_bootstrap_max_chunk_size);

namespace yb {
namespace tserver {

using namespace std::chrono_literals;
class RemoteBootstrapFileDownloaderTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();
    fs_manager_ = std::make_unique<FsManager>(
        Env::Default(), GetTestPath("fs_root"), "tserver_test");
    ASSERT_OK(fs_manager_->CreateInitialFileSystemLayout());
    ASSERT_OK(fs_manager_->CheckAndOpenFileSystemRoots());
  }

 protected:
  std::unique_ptr<FsManager> fs_manager_;
};

// Mock class to simulate a remote bootstrap file source.
class TestRemoteBootstrapFileSource {
 public:
  explicit TestRemoteBootstrapFileSource(const std::string& file_data): file_data_(file_data) {}

  Status FetchData(
      const FetchDataRequestPB& req, FetchDataResponsePB* resp, rpc::RpcController* controller) {
    SCHECK_EQ(
        req.max_length(), FLAGS_remote_bootstrap_max_chunk_size, IllegalState,
        Format("Unexpected max_length: $0", req.max_length()));

    resp->mutable_chunk()->set_offset(current_offset_);
    resp->mutable_chunk()->set_data(file_data_.substr(current_offset_, req.max_length()));
    resp->mutable_chunk()->set_crc32(
        crc::Crc32c(resp->chunk().data().data(), resp->chunk().data().length()));
    resp->mutable_chunk()->set_total_data_length(file_data_.length());
    current_offset_ += req.max_length();
    ++num_calls_;
    return Status::OK();
  }

  int NumCalls() const {
    return num_calls_;
  }

 private:
  int current_offset_ = 0;
  int num_calls_ = 0;
  std::string file_data_;
};

TEST_F(RemoteBootstrapFileDownloaderTest, ChunkSizeTest) {
  std::string kLogPrefix = "test_prefix";
  std::string kTestData = "test_data";
  RemoteBootstrapFileDownloader downloader(&kLogPrefix, fs_manager_.get());

  // Use no rate limiter to avoid a DFATAL when checking how many bootstrap sessions are created.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_remote_bootstrap_rate_limit_bytes_per_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_remote_bootstrap_max_chunk_size) = 1;
  auto test_source = std::make_shared<TestRemoteBootstrapFileSource>(kTestData);
  downloader.Start(FetchDataFunctionCreator(test_source), "test_session_id", 30s);

  // Download a test file and check that the FetchData function and chunk callback are called the
  // expected number of times.
  tablet::FilePB file_pb;
  file_pb.set_name("my_test_file");
  DataIdPB data_id;
  int num_calls = 0;
  ASSERT_OK(downloader.DownloadFile(file_pb, fs_manager_->GetDefaultRootDir(), &data_id,
      [&](size_t chunk_size) {
        ASSERT_EQ(chunk_size, FLAGS_remote_bootstrap_max_chunk_size);
        ++num_calls;
      }));
  // Chunk size is 1, so we should call FetchData and the callback once per character.
  ASSERT_EQ(test_source->NumCalls(), kTestData.size());
  ASSERT_EQ(num_calls, kTestData.size());

  // Check that the file was downloaded correctly.
  auto file_path = JoinPathSegments(fs_manager_->GetDefaultRootDir(), file_pb.name());
  faststring file_data;
  ASSERT_OK(ReadFileToString(Env::Default(), file_path, &file_data));
  ASSERT_EQ(file_data.ToString(), kTestData);
}

TEST_F(RemoteBootstrapFileDownloaderTest, SkipCompression) {
  // Check that we call the uncompressed fetch function if skip_compression is true.
  std::string kLogPrefix = "test_prefix";
  std::string kTestData = "test_data";
  RemoteBootstrapFileDownloader downloader(&kLogPrefix, fs_manager_.get());

  // Use no rate limiter to avoid a DFATAL when checking how many bootstrap sessions are created.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_remote_bootstrap_rate_limit_bytes_per_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_remote_bootstrap_max_chunk_size) = 1;
  auto test_source = std::make_shared<TestRemoteBootstrapFileSource>(kTestData);
  auto test_source_uncompressed = std::make_shared<TestRemoteBootstrapFileSource>(kTestData);
  auto fetch_data = [&](
      const FetchDataRequestPB& req, FetchDataResponsePB* resp, rpc::RpcController* controller) {
    return test_source->FetchData(req, resp, controller);
  };
  auto fetch_data_uncompressed = [&](
      const FetchDataRequestPB& req, FetchDataResponsePB* resp, rpc::RpcController* controller) {
    return test_source_uncompressed->FetchData(req, resp, controller);
  };
  downloader.Start(
      std::move(fetch_data), "test_session_id", 30s, std::move(fetch_data_uncompressed));

  // Download a test file and check that we skip compression if requested.
  tablet::FilePB file_pb;
  file_pb.set_name("my_test_file");
  DataIdPB data_id;
  ASSERT_OK(downloader.DownloadFile(
      file_pb, fs_manager_->GetDefaultRootDir(), &data_id, /*chunk_download_cb=*/ nullptr,
      /*skip_compression=*/ true));

  // Chunk size is 1, so we should call the uncompressed FetchData function once per character, and
  // never call the default FetchData function.
  ASSERT_EQ(test_source->NumCalls(), 0);
  ASSERT_EQ(test_source_uncompressed->NumCalls(), kTestData.size());

  // Check that the file was downloaded correctly.
  auto file_path = JoinPathSegments(fs_manager_->GetDefaultRootDir(), file_pb.name());
  faststring file_data;
  ASSERT_OK(ReadFileToString(Env::Default(), file_path, &file_data));
  ASSERT_EQ(file_data.ToString(), kTestData);
}

} // namespace tserver
} // namespace yb

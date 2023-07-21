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

#include "yb/tserver/remote_bootstrap_file_downloader.h"
#include "yb/tserver/remote_client_base.h"

#include <iomanip>

#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/fs/fs_manager.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/remote_bootstrap.proxy.h"

#include "yb/util/crc.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/rate_limiter.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/stopwatch.h"

using namespace yb::size_literals;

DECLARE_uint64(rpc_max_message_size);
DEFINE_UNKNOWN_int32(remote_bootstrap_max_chunk_size, 64_MB,
             "Maximum chunk size to be transferred at a time during remote bootstrap.");

DEPRECATE_FLAG(int64, remote_boostrap_rate_limit_bytes_per_sec, "10_2022");

DEFINE_UNKNOWN_int64(remote_bootstrap_rate_limit_bytes_per_sec, 256_MB,
             "Maximum transmission rate during a remote bootstrap. This is across all the remote "
             "bootstrap sessions for which this process is acting as a sender or receiver. So "
             "the total limit will be 2 * remote_bootstrap_rate_limit_bytes_per_sec because a "
             "tserver or master can act both as a sender and receiver at the same time.");

DEFINE_UNKNOWN_int32(bytes_remote_bootstrap_durable_write_mb, 1024,
             "Explicitly call fsync after downloading the specified amount of data in MB "
             "during a remote bootstrap session. If 0 fsync() is not called.");

// RETURN_NOT_OK_PREPEND() with a remote-error unwinding step.
#define RETURN_NOT_OK_UNWIND_PREPEND(status, controller, msg) \
  RETURN_NOT_OK_PREPEND(UnwindRemoteError(status, controller), msg)

namespace yb {
namespace tserver {

namespace {

// Decode the remote error into a human-readable Status object.
Status ExtractRemoteError(
    const rpc::ErrorStatusPB& remote_error, const Status& original_status) {
  if (!remote_error.HasExtension(RemoteBootstrapErrorPB::remote_bootstrap_error_ext)) {
    return original_status;
  }

  const RemoteBootstrapErrorPB& error =
      remote_error.GetExtension(RemoteBootstrapErrorPB::remote_bootstrap_error_ext);
  LOG(INFO) << "ExtractRemoteError: " << error.ShortDebugString();
  return StatusFromPB(error.status()).CloneAndPrepend(
      "Received error code " + RemoteBootstrapErrorPB::Code_Name(error.code()) +
          " from remote service");
}

} // namespace


RemoteBootstrapFileDownloader::RemoteBootstrapFileDownloader(
    const std::string* log_prefix, FsManager* fs_manager)
    : log_prefix_(*log_prefix), fs_manager_(*fs_manager) {
}

void RemoteBootstrapFileDownloader::Start(
    std::shared_ptr<RemoteBootstrapServiceProxy> proxy, std::string session_id,
    MonoDelta session_idle_timeout) {
  proxy_ = std::move(proxy);
  session_id_ = std::move(session_id);
  session_idle_timeout_ = session_idle_timeout;
}

Env& RemoteBootstrapFileDownloader::env() const {
  return *fs_manager_.env();
}

Status RemoteBootstrapFileDownloader::DownloadFile(
    const tablet::FilePB& file_pb, const std::string& dir, DataIdPB *data_id) {
  auto file_path = JoinPathSegments(dir, file_pb.name());
  RETURN_NOT_OK(env().CreateDirs(DirName(file_path)));

  if (file_pb.inode() != 0) {
    auto it = inode2file_.find(file_pb.inode());
    if (it != inode2file_.end()) {
      VLOG_WITH_PREFIX(2) << "File with the same inode already found: " << file_path
                          << " => " << it->second;
      auto link_status = env().LinkFile(it->second, file_path);
      if (link_status.ok()) {
        return Status::OK();
      }
      // TODO fallback to copy.
      LOG_WITH_PREFIX(ERROR) << "Failed to link file: " << file_path << " => " << it->second
                             << ": " << link_status;
    }
  }

  if (env().FileExists(file_path)) {
    LOG(INFO) << file_path << " already exists and will be replaced";
    RETURN_NOT_OK(env().DeleteFile(file_path));
  }
  std::unique_ptr<WritableFile> file;
  RETURN_NOT_OK(env().NewWritableFile(file_path, &file));

  data_id->set_file_name(file_pb.name());
  RETURN_NOT_OK_PREPEND(DownloadFile(*data_id, file.get()),
                        Format("Unable to download $0 file $1",
                               DataIdPB::IdType_Name(data_id->type()), file_path));
  VLOG_WITH_PREFIX(2) << "Downloaded file " << file_path;

  if (file_pb.inode() != 0) {
    inode2file_.emplace(file_pb.inode(), file_path);
  }

  return Status::OK();
}

template<class Appendable>
Status RemoteBootstrapFileDownloader::DownloadFile(
    const DataIdPB& data_id, Appendable* appendable) {
  constexpr int kBytesReservedForMessageHeaders = 16384;

  // For periodic sync, indicates number of bytes which need to be sync'ed.
  size_t periodic_sync_unsynced_bytes = 0;
  uint64_t offset = 0;
  auto max_length = std::min<size_t>(FLAGS_remote_bootstrap_max_chunk_size,
                                     FLAGS_rpc_max_message_size - kBytesReservedForMessageHeaders);

  std::unique_ptr<RateLimiter> rate_limiter;

  if (FLAGS_remote_bootstrap_rate_limit_bytes_per_sec > 0) {
    static auto rate_updater = []() {
      auto remote_bootstrap_clients_started = RemoteClientBase::StartedClientsCount();
      if (remote_bootstrap_clients_started < 1) {
        YB_LOG_EVERY_N(ERROR, 100) << "Invalid number of remote bootstrap sessions: "
                                   << remote_bootstrap_clients_started;
        return static_cast<uint64_t>(FLAGS_remote_bootstrap_rate_limit_bytes_per_sec);
      }
      return static_cast<uint64_t>(
          FLAGS_remote_bootstrap_rate_limit_bytes_per_sec / remote_bootstrap_clients_started);
    };

    rate_limiter = std::make_unique<RateLimiter>(rate_updater);
  } else {
    // Inactive RateLimiter.
    rate_limiter = std::make_unique<RateLimiter>();
  }

  rpc::RpcController controller;
  controller.set_timeout(session_idle_timeout_);
  FetchDataRequestPB req;
  Stopwatch verify_data_timer;
  Stopwatch append_data_timer;
  Stopwatch sync_timer;
  Stopwatch file_download_timer;
  file_download_timer.start();
  size_t iterations = 0;

  bool done = false;
  while (!done) {
    controller.Reset();
    req.set_session_id(session_id_);
    req.mutable_data_id()->CopyFrom(data_id);
    req.set_offset(offset);
    if (rate_limiter->active()) {
      auto max_size = rate_limiter->GetMaxSizeForNextTransmission();
      if (max_size > std::numeric_limits<decltype(max_length)>::max()) {
        max_size = std::numeric_limits<decltype(max_length)>::max();
      }
      max_length = std::min(max_length, decltype(max_length)(max_size));
    }
    req.set_max_length(max_length);

    FetchDataResponsePB resp;
    auto status = rate_limiter->SendOrReceiveData([this, &req, &resp, &controller]() {
      return proxy_->FetchData(req, &resp, &controller);
    }, [&resp]() { return resp.ByteSize(); });
    RETURN_NOT_OK_UNWIND_PREPEND(status, controller, "Unable to fetch data from remote");
    DCHECK_LE(resp.chunk().data().size(), max_length);
    iterations++;

    // Sanity-check for corruption.
    verify_data_timer.resume();
    RETURN_NOT_OK_PREPEND(VerifyData(offset, resp.chunk()),
                          Format("Error validating data item $0", data_id));
    verify_data_timer.stop();

    // Write the data.
    VLOG_WITH_PREFIX(3) << "Verifying received data";
    append_data_timer.resume();
    RETURN_NOT_OK(appendable->Append(resp.chunk().data()));
    append_data_timer.stop();
    VLOG_WITH_PREFIX(3) << "Verified and appended successfully: resp size: " << resp.ByteSize()
                        << ", chunk size: " << resp.chunk().data().size();

    if (offset + resp.chunk().data().size() ==
            implicit_cast<size_t>(resp.chunk().total_data_length())) {
      done = true;
    }
    offset += resp.chunk().data().size();
    if (FLAGS_bytes_remote_bootstrap_durable_write_mb != 0) {
      periodic_sync_unsynced_bytes += resp.chunk().data().size();
      if (periodic_sync_unsynced_bytes > FLAGS_bytes_remote_bootstrap_durable_write_mb * 1_MB) {
        sync_timer.resume();
        RETURN_NOT_OK(appendable->Sync());
        sync_timer.stop();
        periodic_sync_unsynced_bytes = 0;
      }
    }
  }

  sync_timer.resume();
  RETURN_NOT_OK(appendable->Sync());
  sync_timer.stop();

  file_download_timer.stop();

  const auto total_bytes = rate_limiter->total_bytes();
  LOG_WITH_PREFIX(INFO) << std::fixed << std::setprecision(3)
    << "Downloaded file: " << data_id.file_name()
    << "; Stats: Total time: " << file_download_timer.elapsed().wall_millis() << " ms"
    << ", iterations: " << iterations
    << ", Transmission rate: " << rate_limiter->GetRate()
    << ", RateLimiter total time slept: " << rate_limiter->total_time_slept().ToMilliseconds()
    << " ms, Total bytes: " << total_bytes
    << ", CRC/verify rate: " << (total_bytes / verify_data_timer.elapsed().wall_millis())
    << " bytes/msec (total_ms: " << verify_data_timer.elapsed().wall_millis()
    << "), Append rate " << (total_bytes / append_data_timer.elapsed().wall_millis())
    << " bytes/msec (total_ms: " << append_data_timer.elapsed().wall_millis()
    << "), File sync time " << sync_timer.elapsed().wall_millis() << "ms";

  return Status::OK();
}

Status RemoteBootstrapFileDownloader::VerifyData(uint64_t offset, const DataChunkPB& chunk) {
  // Verify the offset is what we expected.
  if (offset != chunk.offset()) {
    return STATUS_FORMAT(
        InvalidArgument, "Offset did not match what was asked for $0 vs $1",
        offset, chunk.offset());
  }

  // Verify the checksum.
  uint32_t crc32 = crc::Crc32c(chunk.data().data(), chunk.data().length());
  if (PREDICT_FALSE(crc32 != chunk.crc32())) {
    return STATUS_FORMAT(
        Corruption, "CRC32 does not match at offset $0 size $1: $2 vs $3",
        offset, chunk.data().size(), crc32, chunk.crc32());
  }
  return Status::OK();
}

// Enhance a RemoteError Status message with additional details from the remote.
Status UnwindRemoteError(const Status& status, const rpc::RpcController& controller) {
  if (!status.IsRemoteError()) {
    return status;
  }
  return ExtractRemoteError(*controller.error_response(), status);
}

} // namespace tserver
} // namespace yb

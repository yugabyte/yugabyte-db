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
#include <string>
#include <unordered_map>

#include "yb/rpc/rpc_fwd.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/tserver/remote_bootstrap.pb.h"

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

namespace yb {

class Env;
class FsManager;
class MonoDelta;

namespace tserver {

class RemoteBootstrapServiceProxy;

class RemoteBootstrapFileDownloader {
 public:
  RemoteBootstrapFileDownloader(const std::string* log_prefix, FsManager* fs_manager);

  void Start(
      std::shared_ptr<RemoteBootstrapServiceProxy> proxy, std::string session_id,
      MonoDelta session_idle_timeout);

  Status DownloadFile(
      const tablet::FilePB& file_pb, const std::string& dir, DataIdPB* data_id);

  // Download a single remote file. The block and WAL implementations delegate
  // to this method when downloading files.
  //
  // An Appendable is typically a WritableFile (WAL).
  //
  // Only used in one compilation unit, otherwise the implementation would
  // need to be in the header.
  template<class Appendable>
  Status DownloadFile(const DataIdPB& data_id, Appendable* appendable);

  FsManager& fs_manager() const {
    return fs_manager_;
  }

  const std::string& session_id() const {
    return session_id_;
  }

 private:
  Status VerifyData(uint64_t offset, const DataChunkPB& resp);

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  Env& env() const;

  const std::string& log_prefix_;
  FsManager& fs_manager_;

  std::shared_ptr<RemoteBootstrapServiceProxy> proxy_;
  std::string session_id_;
  MonoDelta session_idle_timeout_ = MonoDelta::kZero;
  std::unordered_map<uint64_t, std::string> inode2file_;
};

Status UnwindRemoteError(const Status& status, const rpc::RpcController& controller);

} // namespace tserver
} // namespace yb

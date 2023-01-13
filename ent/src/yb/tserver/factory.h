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

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/secure_stream.h"

#include "yb/yql/cql/cqlserver/cql_server_options.h"

#include "yb/server/secure.h"

#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/result.h"

DECLARE_string(cert_node_filename);

namespace yb {

namespace cqlserver {

class CQLServer;
class CQLServerOptions;

}

namespace tserver {

class TabletServer;
class TabletServerOptions;

namespace enterprise {

class CQLServer;
class TabletServer;

class CQLServerEnt : public cqlserver::CQLServer {
 public:
  template <class... Args>
  explicit CQLServerEnt(Args&&... args) : CQLServer(std::forward<Args>(args)...) {
  }

  Status ReloadKeysAndCertificates() override {
    if (!secure_context_) {
      return Status::OK();
    }

    return server::ReloadSecureContextKeysAndCertificates(
          secure_context_.get(),
          fs_manager_->GetDefaultRootDir(),
          server::SecureContextType::kExternal,
          options_.HostsString());
  }

 private:
  Status SetupMessengerBuilder(rpc::MessengerBuilder* builder) override {
    RETURN_NOT_OK(CQLServer::SetupMessengerBuilder(builder));
    if (!FLAGS_cert_node_filename.empty()) {
      secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
          fs_manager_->GetDefaultRootDir(),
          FLAGS_cert_node_filename,
          server::SecureContextType::kExternal,
          builder));
    } else {
      secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
          options_.HostsString(), *fs_manager_, server::SecureContextType::kExternal, builder));
    }
    return Status::OK();
  }

  std::unique_ptr<rpc::SecureContext> secure_context_;
};

class Factory {
 public:
  std::unique_ptr<TabletServer> CreateTabletServer(const TabletServerOptions& options) {
    return std::make_unique<TabletServer>(options);
  }

  std::unique_ptr<cqlserver::CQLServer> CreateCQLServer(
      const cqlserver::CQLServerOptions& options, IoService* io,
      tserver::TabletServer* tserver) {
    return std::make_unique<CQLServerEnt>(options, io, tserver);
  }
};

} // namespace enterprise
} // namespace tserver
} // namespace yb

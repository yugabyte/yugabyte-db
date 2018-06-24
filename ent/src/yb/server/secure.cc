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

#include "yb/server/secure.h"

#include "yb/fs/fs_manager.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"
#include "yb/rpc/tcp_stream.h"

#include "yb/util/env.h"
#include "yb/util/path_util.h"

DEFINE_bool(use_node_to_node_encryption, false, "Use node to node encryption");

DEFINE_string(certs_dir, "",
              "Directory that contains certificate authority, private key and certificates for "
              "this server. By default 'certs' subdir in data folder is used.");

namespace yb {
namespace server {

Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& hosts, FsManager* fs_manager, rpc::MessengerBuilder* builder) {
  std::vector<HostPort> host_ports;
  RETURN_NOT_OK(HostPort::ParseStrings(hosts, 0, &host_ports));

  return server::SetupSecureContext(
      DirName(fs_manager->GetTabletMetadataDir()), host_ports[0].host(), builder);
}

Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& root_dir, const std::string& name, rpc::MessengerBuilder* builder) {
  if (!FLAGS_use_node_to_node_encryption) {
    return std::unique_ptr<rpc::SecureContext>();
  }

  auto dir = FLAGS_certs_dir;
  if (dir.empty()) {
    dir = JoinPathSegments(root_dir, "certs");
  }

  LOG(INFO) << "Certs directory: " << dir << ", name: " << name;

  auto result = std::make_unique<rpc::SecureContext>();
  faststring data;
  RETURN_NOT_OK(ReadFileToString(Env::Default(), JoinPathSegments(dir, "ca.crt"), &data));
  RETURN_NOT_OK(result->AddCertificateAuthority(data));

  RETURN_NOT_OK(ReadFileToString(
      Env::Default(), JoinPathSegments(dir, Format("node.$0.key", name)), &data));
  RETURN_NOT_OK(result->UsePrivateKey(data));

  RETURN_NOT_OK(ReadFileToString(
      Env::Default(), JoinPathSegments(dir, Format("node.$0.crt", name)), &data));
  RETURN_NOT_OK(result->UseCertificate(data));

  builder->SetListenProtocol(rpc::SecureStream::StaticProtocol());
  builder->AddStreamFactory(
      rpc::SecureStream::StaticProtocol(),
      rpc::SecureStream::Factory(rpc::TcpStream::Factory(), result.get()));

  return std::move(result);
}

} // namespace server
} // namespace yb

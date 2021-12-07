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

#include "yb/rpc/compressed_stream.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"
#include "yb/rpc/tcp_stream.h"

#include "yb/util/env.h"
#include "yb/util/format.h"
#include "yb/util/net/net_util.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"

DEFINE_bool(use_node_to_node_encryption, false, "Use node to node encryption.");

DEFINE_bool(node_to_node_encryption_use_client_certificates, false,
            "Should client certificates be sent and verified for encrypted node to node "
            "communication.");

DEFINE_string(node_to_node_encryption_required_uid, "",
              "Allow only certificates with specified uid. Empty to allow any.");

DEFINE_string(certs_dir, "",
              "Directory that contains certificate authority, private key and certificates for "
              "this server. By default 'certs' subdir in data folder is used.");

DEFINE_bool(use_client_to_server_encryption, false, "Use client to server encryption");

DEFINE_string(certs_for_client_dir, "",
              "Directory that contains certificate authority, private key and certificates for "
              "this server that should be used for client to server communications. "
              "When empty, the same dir as for server to server communications is used.");

DEFINE_string(cert_node_filename, "",
              "The file name that will be used in the names of the node "
              "certificates and keys. These files will be named : "
              "'node.{cert_node_filename}.{key|crt}'. "
              "If this flag is not set, then --server_broadcast_addresses will be "
              "used if it is set, and if not, --rpc_bind_addresses will be used.");

DEFINE_string(key_file_pattern, "node.$0.key", "Pattern used for key file");

DEFINE_string(cert_file_pattern, "node.$0.crt", "Pattern used for certificate file");

DEFINE_bool(enable_stream_compression, true, "Whether it is allowed to use stream compression.");

namespace yb {
namespace server {
namespace {

string DefaultCertsDir(const string& root_dir) {
  return JoinPathSegments(root_dir, "certs");
}

} // namespace

string DefaultRootDir(const FsManager& fs_manager) {
  return DirName(fs_manager.GetRaftGroupMetadataDir());
}

string DefaultCertsDir(const FsManager& fs_manager) {
  return DefaultCertsDir(DefaultRootDir(fs_manager));
}

Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& hosts, const FsManager& fs_manager, SecureContextType type,
    rpc::MessengerBuilder* builder) {
  std::vector<HostPort> host_ports;
  RETURN_NOT_OK(HostPort::ParseStrings(hosts, 0, &host_ports));

  return server::SetupSecureContext(
      DefaultRootDir(fs_manager), host_ports[0].host(), type, builder);
}

Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& root_dir, const std::string& name,
    SecureContextType type, rpc::MessengerBuilder* builder) {
  return SetupSecureContext(std::string(), root_dir, name, type, builder);
}

void ApplyCompressedStream(
    rpc::MessengerBuilder* builder, const rpc::StreamFactoryPtr lower_layer_factory) {
  if (!FLAGS_enable_stream_compression) {
    return;
  }
  builder->SetListenProtocol(rpc::CompressedStreamProtocol());
  auto parent_mem_tracker = builder->last_used_parent_mem_tracker();
  auto buffer_tracker = MemTracker::FindOrCreateTracker(
      -1, "Compressed Read Buffer", parent_mem_tracker);
  builder->AddStreamFactory(
      rpc::CompressedStreamProtocol(),
      rpc::CompressedStreamFactory(std::move(lower_layer_factory), buffer_tracker));
}

Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& cert_dir, const std::string& root_dir, const std::string& name,
    SecureContextType type, rpc::MessengerBuilder* builder) {
  auto use = type == SecureContextType::kInternal ? FLAGS_use_node_to_node_encryption
                                                  : FLAGS_use_client_to_server_encryption;
  LOG(INFO) << __func__ << ": " << type << ", " << use;
  if (!use) {
    ApplyCompressedStream(builder, rpc::TcpStream::Factory());
    return nullptr;
  }

  std::string dir;
  if (!cert_dir.empty()) {
    dir = cert_dir;
  } else if (type == SecureContextType::kExternal) {
    dir = FLAGS_certs_for_client_dir;
  }
  if (dir.empty()) {
    dir = FLAGS_certs_dir;
  }
  if (dir.empty()) {
    dir = DefaultCertsDir(root_dir);
  }

  UseClientCerts use_client_certs = UseClientCerts::kFalse;
  std::string required_uid;
  if (type == SecureContextType::kInternal) {
    use_client_certs = UseClientCerts(FLAGS_node_to_node_encryption_use_client_certificates);
    required_uid = FLAGS_node_to_node_encryption_required_uid;
  }
  auto context = VERIFY_RESULT(CreateSecureContext(dir, use_client_certs, name, required_uid));
  ApplySecureContext(context.get(), builder);
  return context;
}

Result<std::unique_ptr<rpc::SecureContext>> CreateSecureContext(
    const std::string& certs_dir, UseClientCerts use_client_certs, const std::string& node_name,
    const std::string& required_uid) {

  LOG(INFO) << "Certs directory: " << certs_dir << ", node name: " << node_name;

  auto result = std::make_unique<rpc::SecureContext>();
  faststring data;
  RETURN_NOT_OK(result->AddCertificateAuthorityFile(JoinPathSegments(certs_dir, "ca.crt")));

  if (!node_name.empty()) {
    RETURN_NOT_OK(ReadFileToString(
        Env::Default(), JoinPathSegments(certs_dir, Format(FLAGS_key_file_pattern, node_name)),
        &data));
    RETURN_NOT_OK(result->UsePrivateKey(data));

    RETURN_NOT_OK(ReadFileToString(
        Env::Default(), JoinPathSegments(certs_dir, Format(FLAGS_cert_file_pattern, node_name)),
        &data));
    RETURN_NOT_OK(result->UseCertificate(data));
  }

  if (use_client_certs) {
    result->set_require_client_certificate(true);
    result->set_use_client_certificate(true);
  }

  result->set_required_uid(required_uid);

  return result;
}

void ApplySecureContext(const rpc::SecureContext* context, rpc::MessengerBuilder* builder) {
  auto parent_mem_tracker = builder->last_used_parent_mem_tracker();
  auto buffer_tracker = MemTracker::FindOrCreateTracker(
      -1, "Encrypted Read Buffer", parent_mem_tracker);

  auto secure_stream_factory = rpc::SecureStreamFactory(
      rpc::TcpStream::Factory(), buffer_tracker, context);
  builder->SetListenProtocol(rpc::SecureStreamProtocol());
  builder->AddStreamFactory(rpc::SecureStreamProtocol(), secure_stream_factory);
  ApplyCompressedStream(builder, secure_stream_factory);
}

} // namespace server
} // namespace yb

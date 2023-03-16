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
#include "yb/util/flags.h"

using std::string;

DEFINE_UNKNOWN_bool(use_node_to_node_encryption, false, "Use node to node encryption.");

DEFINE_UNKNOWN_bool(node_to_node_encryption_use_client_certificates, false,
            "Should client certificates be sent and verified for encrypted node to node "
            "communication.");

DEFINE_UNKNOWN_string(node_to_node_encryption_required_uid, "",
              "Allow only certificates with specified uid. Empty to allow any.");

DEFINE_UNKNOWN_string(certs_dir, "",
              "Directory that contains certificate authority, private key and certificates for "
              "this server. By default 'certs' subdir in data folder is used.");

DEFINE_UNKNOWN_bool(use_client_to_server_encryption, false, "Use client to server encryption");

DEFINE_UNKNOWN_string(certs_for_client_dir, "",
              "Directory that contains certificate authority, private key and certificates for "
              "this server that should be used for client to server communications. "
              "When empty, the same dir as for server to server communications is used.");

DEFINE_UNKNOWN_string(cert_node_filename, "",
              "The file name that will be used in the names of the node "
              "certificates and keys. These files will be named : "
              "'node.{cert_node_filename}.{key|crt}'. "
              "If this flag is not set, then --server_broadcast_addresses will be "
              "used if it is set, and if not, --rpc_bind_addresses will be used.");

DEFINE_UNKNOWN_string(key_file_pattern, "node.$0.key", "Pattern used for key file");

DEFINE_UNKNOWN_string(cert_file_pattern, "node.$0.crt", "Pattern used for certificate file");

DEFINE_UNKNOWN_bool(enable_stream_compression, true,
    "Whether it is allowed to use stream compression.");

namespace yb {
namespace server {

namespace {

string CertsDir(const std::string& root_dir, SecureContextType type) {
  string certs_dir;
  if (type == SecureContextType::kExternal) {
    certs_dir = FLAGS_certs_for_client_dir;
  }
  if (certs_dir.empty()) {
    certs_dir = FLAGS_certs_dir;
  }
  if (certs_dir.empty()) {
    certs_dir = FsManager::GetCertsDir(root_dir);
  }
  return certs_dir;
}

} // namespace

bool IsNodeToNodeEncryptionEnabled() {
  return FLAGS_use_node_to_node_encryption;
}

bool IsClientToServerEncryptionEnabled() {
  return FLAGS_use_client_to_server_encryption;
}

string DefaultCertsDir(const FsManager& fs_manager) {
  return fs_manager.GetCertsDir(fs_manager.GetDefaultRootDir());
}

Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& hosts, const FsManager& fs_manager, SecureContextType type,
    rpc::MessengerBuilder* builder) {
  std::vector<HostPort> host_ports;
  RETURN_NOT_OK(HostPort::ParseStrings(hosts, 0, &host_ports));

  return server::SetupSecureContext(
      fs_manager.GetDefaultRootDir(), host_ports[0].host(), type, builder);
}

Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& root_dir, const std::string& name,
    SecureContextType type, rpc::MessengerBuilder* builder) {
  return SetupSecureContext(std::string(), root_dir, name, type, builder);
}

Result<std::unique_ptr<rpc::SecureContext>> SetupInternalSecureContext(
    const string& local_hosts, const FsManager& fs_manager,
    rpc::MessengerBuilder* messenger_builder) {
  if (!FLAGS_cert_node_filename.empty()) {
    return VERIFY_RESULT(server::SetupSecureContext(
        fs_manager.GetDefaultRootDir(),
        FLAGS_cert_node_filename,
        server::SecureContextType::kInternal,
        messenger_builder));
  }

  return VERIFY_RESULT(server::SetupSecureContext(
      local_hosts, fs_manager, server::SecureContextType::kInternal, messenger_builder));
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

  std::string dir = cert_dir.empty() ? CertsDir(root_dir, type) : cert_dir;

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

  auto result = std::make_unique<rpc::SecureContext>(
      rpc::RequireClientCertificate(use_client_certs), rpc::UseClientCertificate(use_client_certs),
      required_uid);

  RETURN_NOT_OK(ReloadSecureContextKeysAndCertificates(result.get(), certs_dir, node_name));

  return result;
}

Status ReloadSecureContextKeysAndCertificates(
    rpc::SecureContext* context, const std::string& root_dir, SecureContextType type,
    const std::string& hosts) {
  std::string node_name = FLAGS_cert_node_filename;

  if (node_name.empty()) {
    std::vector<HostPort> host_ports;
    RETURN_NOT_OK(HostPort::ParseStrings(hosts, 0, &host_ports));
    node_name = host_ports[0].host();
  }

  return ReloadSecureContextKeysAndCertificates(context, node_name, root_dir, type);
}

Status ReloadSecureContextKeysAndCertificates(
    rpc::SecureContext* context, const std::string& node_name, const std::string& root_dir,
    SecureContextType type) {
  std::string certs_dir = CertsDir(root_dir, type);
  return ReloadSecureContextKeysAndCertificates(context, certs_dir, node_name);
}

Status ReloadSecureContextKeysAndCertificates(
    rpc::SecureContext* context, const std::string& certs_dir, const std::string& node_name) {

  LOG(INFO) << "Certs directory: " << certs_dir << ", node name: " << node_name;

  auto ca_cert_file = JoinPathSegments(certs_dir, "ca.crt");
  if (!node_name.empty()) {
    faststring cert_data, pkey_data;
    RETURN_NOT_OK(ReadFileToString(
        Env::Default(), JoinPathSegments(certs_dir, Format(FLAGS_cert_file_pattern, node_name)),
        &cert_data));
    RETURN_NOT_OK(ReadFileToString(
        Env::Default(), JoinPathSegments(certs_dir, Format(FLAGS_key_file_pattern, node_name)),
        &pkey_data));

    RETURN_NOT_OK(context->UseCertificates(ca_cert_file, cert_data, pkey_data));
  } else {
    RETURN_NOT_OK(context->AddCertificateAuthorityFile(ca_cert_file));
  }

  return Status::OK();
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

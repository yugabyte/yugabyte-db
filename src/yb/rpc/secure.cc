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

#include "yb/rpc/secure.h"

#include <mutex>
#include <optional>

#include "yb/gutil/thread_annotations.h"

#include "yb/rpc/compressed_stream.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"
#include "yb/rpc/tcp_stream.h"

#include "yb/util/env.h"
#include "yb/util/format.h"
#include "yb/util/net/net_util.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/flags.h"

using std::string;

DEFINE_NON_RUNTIME_bool(use_node_to_node_encryption, false, "Use node to node encryption.");

DEFINE_NON_RUNTIME_bool(node_to_node_encryption_use_client_certificates, false,
    "Should client certificates be sent and verified for encrypted node to node "
    "communication.");

DEFINE_NON_RUNTIME_string(node_to_node_encryption_required_uid, "",
    "Allow only certificates with specified uid. Empty to allow any.");

DEFINE_NON_RUNTIME_string(certs_dir, "",
    "Directory that contains certificate authority, private key and certificates for "
    "this server. By default 'certs' subdir in data folder is used.");

DEFINE_NON_RUNTIME_bool(use_client_to_server_encryption, false, "Use client to server encryption");

DEFINE_NON_RUNTIME_string(certs_for_client_dir, "",
    "Directory that contains certificate authority, private key and certificates for "
    "this server that should be used for client to server communications. "
    "When empty, the same dir as for server to server communications is used.");

DEFINE_NON_RUNTIME_string(cert_node_filename, "",
    "The file name that will be used in the names of the node "
    "certificates and keys. These files will be named : "
    "'node.{cert_node_filename}.{key|crt}'. "
    "If this flag is not set, then --server_broadcast_addresses will be "
    "used if it is set, and if not, --rpc_bind_addresses will be used.");

DEFINE_NON_RUNTIME_string(key_file_pattern, "node.$0.key", "Pattern used for key file");

DEFINE_NON_RUNTIME_string(cert_file_pattern, "node.$0.crt", "Pattern used for certificate file");

DEFINE_NON_RUNTIME_bool(enable_stream_compression, true,
    "Whether it is allowed to use stream compression.");

namespace yb {
namespace rpc {

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
    certs_dir = GetCertsDir(root_dir);
  }
  return certs_dir;
}

static constexpr char kSecureCertsDirName[] = "certs";

struct NodeCertificateLocation {
  std::string certs_dir;
  std::string node_name;
};

// The node certificate of the server running in this process, recorded by
// SetupInternalSecureContext so that clients of other universes can present it.
std::mutex local_node_certificate_mutex;
std::optional<NodeCertificateLocation> local_node_certificate
    GUARDED_BY(local_node_certificate_mutex);

std::optional<NodeCertificateLocation> LocalNodeCertificate() {
  std::lock_guard lock(local_node_certificate_mutex);
  return local_node_certificate;
}

Status LoadKeysAndCertificates(
    SecureContext* context, const std::string& ca_dir, const std::string& cert_dir,
    const std::string& node_name) {
  LOG(INFO) << "CA directory: " << ca_dir << ", certs directory: " << cert_dir
            << ", node name: " << node_name;

  auto ca_cert_file = JoinPathSegments(ca_dir, "ca.crt");
  if (node_name.empty()) {
    return context->AddCertificateAuthorityFile(ca_cert_file);
  }

  faststring cert_data, pkey_data;
  RETURN_NOT_OK(ReadFileToString(
      Env::Default(), JoinPathSegments(cert_dir, Format(FLAGS_cert_file_pattern, node_name)),
      &cert_data));
  RETURN_NOT_OK(ReadFileToString(
      Env::Default(), JoinPathSegments(cert_dir, Format(FLAGS_key_file_pattern, node_name)),
      &pkey_data));
  return context->UseCertificates(ca_cert_file, cert_data, pkey_data);
}

// Where a client of another universe loads that universe's CA from, and the certificate it
// presents. node_name is empty when it presents none.
struct RemoteUniverseCertificates {
  std::string ca_dir;
  std::string cert_dir;
  std::string node_name;
};

// A certificate placed beside the remote CA wins over the local node certificate, because only
// the operator knows whether the remote universe trusts the local CA. Without one, the local node
// certificate is presented only when client certificates are on: presenting a certificate the
// remote does not trust fails the handshake even when the remote does not require one.
RemoteUniverseCertificates GetRemoteUniverseCertificates(const std::string& remote_certs_dir) {
  auto local = LocalNodeCertificate();
  RemoteUniverseCertificates result;
  if (!remote_certs_dir.empty()) {
    result.ca_dir = remote_certs_dir;
  } else if (local) {
    result.ca_dir = local->certs_dir;
  } else {
    result.ca_dir = CertsDir(/* root_dir= */ "", SecureContextType::kInternal);
  }
  if (!local) {
    return result;
  }

  if (!remote_certs_dir.empty() &&
      Env::Default()->FileExists(
          JoinPathSegments(remote_certs_dir, Format(FLAGS_cert_file_pattern, local->node_name)))) {
    result.cert_dir = remote_certs_dir;
    result.node_name = local->node_name;
  } else if (FLAGS_node_to_node_encryption_use_client_certificates) {
    result.cert_dir = local->certs_dir;
    result.node_name = local->node_name;
  }
  return result;
}

} // namespace

std::string GetCertsDir(const std::string& root_dir) {
  return JoinPathSegments(root_dir, kSecureCertsDirName);
}

bool IsNodeToNodeEncryptionEnabled() {
  return FLAGS_use_node_to_node_encryption;
}

bool IsClientToServerEncryptionEnabled() {
  return FLAGS_use_client_to_server_encryption;
}

Result<std::unique_ptr<SecureContext>> SetupSecureContext(
    const std::string& root_dir, const std::string& name, SecureContextType type,
    MessengerBuilder* builder) {
  return SetupSecureContext(std::string(), root_dir, name, type, builder);
}

Result<std::unique_ptr<SecureContext>> SetupInternalSecureContext(
    const string& local_hosts, const std::string& root_dir, MessengerBuilder* messenger_builder) {
  std::string node_name = FLAGS_cert_node_filename;
  if (node_name.empty()) {
    std::vector<HostPort> host_ports;
    RETURN_NOT_OK(HostPort::ParseStrings(local_hosts, /*default_port=*/0, &host_ports));
    node_name = host_ports[0].host();
  }

  auto context = VERIFY_RESULT(rpc::SetupSecureContext(
      root_dir, node_name, SecureContextType::kInternal, messenger_builder));
  if (context) {
    std::lock_guard lock(local_node_certificate_mutex);
    local_node_certificate = NodeCertificateLocation{
        .certs_dir = CertsDir(root_dir, SecureContextType::kInternal), .node_name = node_name};
  }
  return context;
}

void ApplyCompressedStream(MessengerBuilder* builder, const StreamFactoryPtr lower_layer_factory) {
  if (!FLAGS_enable_stream_compression) {
    return;
  }
  builder->SetListenProtocol(CompressedStreamProtocol());
  auto parent_mem_tracker = builder->last_used_parent_mem_tracker();
  auto buffer_tracker = MemTracker::FindOrCreateTracker(
      -1, "Compressed Read Buffer", parent_mem_tracker);
  builder->AddStreamFactory(
      CompressedStreamProtocol(),
      CompressedStreamFactory(std::move(lower_layer_factory), buffer_tracker));
}

Result<std::unique_ptr<SecureContext>> SetupSecureContext(
    const std::string& cert_dir, const std::string& root_dir, const std::string& name,
    SecureContextType type, MessengerBuilder* builder) {
  auto use = type == SecureContextType::kInternal ? FLAGS_use_node_to_node_encryption
                                                  : FLAGS_use_client_to_server_encryption;
  LOG(INFO) << __func__ << ": " << type << ", " << use;
  if (!use) {
    ApplyCompressedStream(builder, TcpStream::Factory());
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

Result<std::unique_ptr<SecureContext>> CreateSecureContext(
    const std::string& certs_dir, UseClientCerts use_client_certs, const std::string& node_name,
    const std::string& required_uid) {
  // Without a node name only ca.crt is read, so a context that also claims a client certificate
  // would offer one it never loaded and index an empty vector on its first connection.
  SCHECK_FORMAT(
      !use_client_certs || !node_name.empty(), InvalidArgument,
      "Client certificates requested from $0 without a node name to load them by", certs_dir);

  auto result = std::make_unique<SecureContext>(
      RequireClientCertificate(use_client_certs), UseClientCertificate(use_client_certs),
      required_uid);

  RETURN_NOT_OK(ReloadSecureContextKeysAndCertificates(result.get(), certs_dir, node_name));

  return result;
}

Status ReloadSecureContextKeysAndCertificates(
    SecureContext* context, const std::string& root_dir, SecureContextType type,
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
    SecureContext* context, const std::string& node_name, const std::string& root_dir,
    SecureContextType type) {
  std::string certs_dir = CertsDir(root_dir, type);
  return ReloadSecureContextKeysAndCertificates(context, certs_dir, node_name);
}

Status ReloadSecureContextKeysAndCertificates(
    SecureContext* context, const std::string& certs_dir, const std::string& node_name) {
  return LoadKeysAndCertificates(context, certs_dir, certs_dir, node_name);
}

void ApplySecureContext(const SecureContext* context, MessengerBuilder* builder) {
  auto parent_mem_tracker = builder->last_used_parent_mem_tracker();
  auto buffer_tracker = MemTracker::FindOrCreateTracker(
      -1, "Encrypted Read Buffer", parent_mem_tracker);

  auto secure_stream_factory = SecureStreamFactory(TcpStream::Factory(), buffer_tracker, context);
  builder->SetListenProtocol(SecureStreamProtocol());
  builder->SetUncompressedProtocol(SecureStreamProtocol());
  builder->AddStreamFactory(SecureStreamProtocol(), secure_stream_factory);
  ApplyCompressedStream(builder, secure_stream_factory);
}

Result<std::unique_ptr<SecureContext>> SetupRemoteUniverseSecureContext(
    const std::string& remote_certs_dir, MessengerBuilder* builder) {
  if (!FLAGS_use_node_to_node_encryption) {
    ApplyCompressedStream(builder, TcpStream::Factory());
    return nullptr;
  }

  auto certs = GetRemoteUniverseCertificates(remote_certs_dir);
  auto use_client_certs = !certs.node_name.empty();
  SCHECK_FORMAT(
      use_client_certs || !FLAGS_node_to_node_encryption_use_client_certificates, IllegalState,
      "Client certificates requested for the universe with CA in $0, but this process has not "
      "loaded a node certificate", certs.ca_dir);

  auto context = std::make_unique<SecureContext>(
      RequireClientCertificate(use_client_certs), UseClientCertificate(use_client_certs),
      FLAGS_node_to_node_encryption_required_uid);
  RETURN_NOT_OK(
      LoadKeysAndCertificates(context.get(), certs.ca_dir, certs.cert_dir, certs.node_name));
  ApplySecureContext(context.get(), builder);
  return context;
}

Status ReloadRemoteUniverseSecureContext(
    SecureContext* context, const std::string& remote_certs_dir) {
  auto certs = GetRemoteUniverseCertificates(remote_certs_dir);
  return LoadKeysAndCertificates(context, certs.ca_dir, certs.cert_dir, certs.node_name);
}

}  // namespace rpc
} // namespace yb

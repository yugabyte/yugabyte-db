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

#include "yb/rpc/secure_stream.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <boost/tokenizer.hpp>

#include "yb/encryption/encryption_util.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/outbound_data.h"
#include "yb/rpc/refined_stream.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/enums.h"
#include "yb/util/errno.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;

DEFINE_bool(allow_insecure_connections, true, "Whether we should allow insecure connections.");
DEFINE_bool(dump_certificate_entries, false, "Whether we should dump certificate entries.");
DEFINE_bool(verify_client_endpoint, false, "Whether client endpoint should be verified.");
DEFINE_bool(verify_server_endpoint, true, "Whether server endpoint should be verified.");
DEFINE_string(ssl_protocols, "",
              "List of allowed SSL protocols (ssl2, ssl3, tls10, tls11, tls12). "
                  "Empty to allow TLS only.");

DEFINE_string(cipher_list, "",
              "Define the list of available ciphers (TLSv1.2 and below).");

DEFINE_string(ciphersuites, "",
              "Define the available TLSv1.3 ciphersuites.");

#define YB_RPC_SSL_TYPE(name) \
  struct BOOST_PP_CAT(name, Free) { \
    void operator()(name* value) const { \
      BOOST_PP_CAT(name, _free)(value); \
    } \
  }; \
  typedef std::unique_ptr<name, BOOST_PP_CAT(name, Free)> BOOST_PP_CAT(name, Ptr);

namespace yb {
namespace rpc {

namespace {

typedef struct evp_pkey_st EVP_PKEY;
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct x509_st X509;

YB_RPC_SSL_TYPE(BIO)
YB_RPC_SSL_TYPE(EVP_PKEY)
YB_RPC_SSL_TYPE(SSL)
YB_RPC_SSL_TYPE(SSL_CTX)
YB_RPC_SSL_TYPE(X509)

const unsigned char kContextId[] = { 'Y', 'u', 'g', 'a', 'B', 'y', 't', 'e' };

std::string SSLErrorMessage(uint64_t error) {
  auto message = ERR_reason_error_string(error);
  return message ? message : "no error";
}

#define SSL_STATUS(type, format) STATUS_FORMAT(type, format, SSLErrorMessage(ERR_get_error()))

Result<BIOPtr> BIOFromSlice(const Slice& data) {
  BIOPtr bio(BIO_new_mem_buf(data.data(), narrow_cast<int>(data.size())));
  if (!bio) {
    return SSL_STATUS(IOError, "Create BIO failed: $0");
  }
  return std::move(bio);
}

Result<X509Ptr> X509FromSlice(const Slice& data) {
  ERR_clear_error();

  auto bio = VERIFY_RESULT(BIOFromSlice(data));

  X509Ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  if (!cert) {
    return SSL_STATUS(IOError, "Read cert failed: $0");
  }

  return std::move(cert);
}

YB_RPC_SSL_TYPE(ASN1_INTEGER);
YB_RPC_SSL_TYPE(RSA);
YB_RPC_SSL_TYPE(X509_NAME);

Result<EVP_PKEYPtr> GeneratePrivateKey(int bits) {
  RSAPtr rsa(RSA_generate_key(bits, 65537, nullptr, nullptr));
  if (!rsa) {
    return SSL_STATUS(InvalidArgument, "Failed to generate private key: $0");
  }

  EVP_PKEYPtr pkey(EVP_PKEY_new());
  auto res = EVP_PKEY_assign_RSA(pkey.get(), rsa.release());
  if (res != 1) {
    return SSL_STATUS(InvalidArgument, "Failed to assign private key: $0");
  }

  return std::move(pkey);
}

class ExtensionConfigurator {
 public:
  explicit ExtensionConfigurator(X509 *cert) : cert_(cert) {
    // No configuration database
    X509V3_set_ctx_nodb(&ctx_);
    // Both issuer and subject certs
    X509V3_set_ctx(&ctx_, cert, cert, nullptr, nullptr, 0);
  }

  Status Add(int nid, const char* value) {
    X509_EXTENSION *ex = X509V3_EXT_conf_nid(nullptr, &ctx_, nid, value);
    if (!ex) {
      return SSL_STATUS(InvalidArgument, "Failed to create extension: $0");
    }

    X509_add_ext(cert_, ex, -1);
    X509_EXTENSION_free(ex);

    return Status::OK();
  }

 private:
  X509V3_CTX ctx_;
  X509* cert_;
};

Result<X509Ptr> CreateCertificate(
    EVP_PKEY* key, const std::string& common_name, EVP_PKEY* ca_pkey, X509* ca_cert) {
  X509Ptr cert(X509_new());
  if (!cert) {
    return SSL_STATUS(IOError, "Failed to create new certificate: $0");
  }

  if (X509_set_version(cert.get(), 2) != 1) {
    return SSL_STATUS(IOError, "Failed to set certificate version: $0");
  }

  ASN1_INTEGERPtr aserial(ASN1_INTEGER_new());
  ASN1_INTEGER_set(aserial.get(), 0);
  if (!X509_set_serialNumber(cert.get(), aserial.get())) {
    return SSL_STATUS(IOError, "Failed to set serial number: $0");
  }

  X509_NAMEPtr name(X509_NAME_new());
  auto bytes = pointer_cast<const unsigned char*>(common_name.c_str());
  if (!X509_NAME_add_entry_by_txt(
      name.get(), "CN", MBSTRING_ASC, bytes, narrow_cast<int>(common_name.length()), -1, 0)) {
    return SSL_STATUS(IOError, "Failed to create subject: $0");
  }

  if (X509_set_subject_name(cert.get(), name.get()) != 1) {
    return SSL_STATUS(IOError, "Failed to set subject: $0");
  }

  X509_NAME* issuer = name.get();
  if (ca_cert) {
    issuer = X509_get_subject_name(ca_cert);
    if (!issuer) {
      return SSL_STATUS(IOError, "Failed to get CA subject name: $0");
    }
  } else {
    ExtensionConfigurator configurator(cert.get());
    RETURN_NOT_OK(configurator.Add(NID_basic_constraints, "critical,CA:TRUE"));
  }

  if (X509_set_issuer_name(cert.get(), issuer) != 1) {
    return SSL_STATUS(IOError, "Failed to set issuer: $0");
  }

  if (X509_set_pubkey(cert.get(), key) != 1) {
    return SSL_STATUS(IOError, "Failed to set public key: $0");
  }

  if (!X509_gmtime_adj(X509_get_notBefore(cert.get()), 0)) {
    return SSL_STATUS(IOError, "Failed to set not before: $0");
  }

  const auto k1Year = 365 * 24h;
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(k1Year).count();
  if (!X509_gmtime_adj(X509_get_notAfter(cert.get()), seconds)) {
    return SSL_STATUS(IOError, "Failed to set not after: $0");
  }

  if (ca_cert) {
    X509V3_CTX ctx;
    X509V3_set_ctx(&ctx, ca_cert, cert.get(), nullptr, nullptr, 0);
  }

  if (!X509_sign(cert.get(), ca_pkey, EVP_sha256())) {
    return SSL_STATUS(IOError, "Sign failed: $0");
  }

  return std::move(cert);
}

const std::unordered_map<std::string, int64_t>& SSLProtocolMap() {
  static const std::unordered_map<std::string, int64_t> result = {
      {"ssl2", SSL_OP_NO_SSLv2},
      {"ssl3", SSL_OP_NO_SSLv3},
      {"tls10", SSL_OP_NO_TLSv1},
      {"tls11", SSL_OP_NO_TLSv1_1},
      {"tls12", SSL_OP_NO_TLSv1_2},
      {"tls13", SSL_OP_NO_TLSv1_3},
  };
  return result;
}

int64_t ProtocolsOption() {
  constexpr int64_t kDefaultProtocols = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;

  const std::string& ssl_protocols = FLAGS_ssl_protocols;
  if (ssl_protocols.empty()) {
    return kDefaultProtocols;
  }

  const auto& protocol_map = SSLProtocolMap();
  int64_t result = SSL_OP_NO_SSL_MASK;
  boost::tokenizer<> tokenizer(ssl_protocols);
  for (const auto& protocol : tokenizer) {
    auto it = protocol_map.find(protocol);
    if (it == protocol_map.end()) {
      LOG(DFATAL) << "Unknown SSL protocol: " << protocol;
      return kDefaultProtocols;
    }
    result &= ~it->second;
  }

  return result;
}

class OpenSSLInitializer {
 public:
  OpenSSLInitializer() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    OpenSSL_add_all_ciphers();
  }

  ~OpenSSLInitializer() {
    ERR_free_strings();
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();
    ERR_remove_thread_state(nullptr);
    SSL_COMP_free_compression_methods();
  }
};

YB_STRONGLY_TYPED_BOOL(UseCertificateKeyPair);

} // namespace

void InitOpenSSL() {
  static OpenSSLInitializer initializer;
}

class SecureContext::Impl {
 public:
  Impl(
      RequireClientCertificate require_client_certificate,
      UseClientCertificate use_client_certificate,
      const std::string& required_uid);

  Impl(const Impl&) = delete;
  void operator=(const Impl&) = delete;

  Status AddCertificateAuthorityFile(const std::string& file) EXCLUDES(mutex_);

  Status UseCertificates(
      const std::string& ca_cert_file, const Slice& certificate_data,
      const Slice& pkey_data) EXCLUDES(mutex_);

  // Generates and uses temporary keys, should be used only during testing.
  Status TEST_GenerateKeys(int bits, const std::string& common_name,
                           MatchingCertKeyPair matching_cert_key_pair) EXCLUDES(mutex_);

  Result<SSLPtr> Create(rpc::UseCertificateKeyPair use_certificate_key_pair)
      const EXCLUDES(mutex_);

  RequireClientCertificate require_client_certificate() const {
    return require_client_certificate_;
  }

  UseClientCertificate use_client_certificate() const {
    return use_client_certificate_;
  }

  const std::string& required_uid() const {
    return required_uid_;
  }

 private:
  Status AddCertificateAuthorityFileUnlocked(const std::string& file) REQUIRES(mutex_);

  Status UseCertificateKeyPair(
      const Slice& certificate_data, const Slice& pkey_data) REQUIRES(mutex_);

  Status UseCertificateKeyPair(X509Ptr&& certificate, EVP_PKEYPtr&& pkey) REQUIRES(mutex_);

  Status AddCertificateAuthority(X509* cert) REQUIRES(mutex_);

  Result<SSLPtr> Create(
      const X509Ptr& certificate, const EVP_PKEYPtr& pkey,
      rpc::UseCertificateKeyPair use_certificate_key_pair) const REQUIRES_SHARED(mutex_);

  mutable rw_spinlock mutex_;
  SSL_CTXPtr context_ GUARDED_BY(mutex_);
  EVP_PKEYPtr pkey_ GUARDED_BY(mutex_);
  X509Ptr certificate_ GUARDED_BY(mutex_);

  RequireClientCertificate require_client_certificate_;
  UseClientCertificate use_client_certificate_;
  std::string required_uid_;
};

SecureContext::Impl::Impl(
    RequireClientCertificate require_client_certificate,
    UseClientCertificate use_client_certificate, const std::string& required_uid):
  require_client_certificate_(require_client_certificate),
  use_client_certificate_(use_client_certificate), required_uid_(required_uid) {

  InitOpenSSL();

  context_.reset(SSL_CTX_new(SSLv23_method()));
  DCHECK(context_);

  int64_t protocols = ProtocolsOption();
  VLOG(1) << "Protocols option: " << protocols;
  SSL_CTX_set_options(context_.get(), protocols | SSL_OP_NO_COMPRESSION);

  auto cipher_list = FLAGS_cipher_list;
  if (!cipher_list.empty()) {
    LOG(INFO) << "Use cipher list: " << cipher_list;
    auto res = SSL_CTX_set_cipher_list(context_.get(), cipher_list.c_str());
    LOG_IF(DFATAL, res != 1) << "Failed to set cipher list: "
                             << SSLErrorMessage(ERR_get_error());
  }

  auto ciphersuites = FLAGS_ciphersuites;
  if (!ciphersuites.empty()) {
    LOG(INFO) << "Use cipher suites: " << ciphersuites;
    auto res = SSL_CTX_set_ciphersuites(context_.get(), ciphersuites.c_str());
    LOG_IF(DFATAL, res != 1) << "Failed to set ciphersuites: "
                           << SSLErrorMessage(ERR_get_error());
  }

  auto res = SSL_CTX_set_session_id_context(context_.get(), kContextId, sizeof(kContextId));
  LOG_IF(DFATAL, res != 1) << "Failed to set session id for SSL context: "
                           << SSLErrorMessage(ERR_get_error());
}

Result<SSLPtr> SecureContext::Impl::Create(
    rpc::UseCertificateKeyPair use_certificate_key_pair) const {
  SharedLock<rw_spinlock> lock(mutex_);
  return Create(certificate_, pkey_, use_certificate_key_pair);
}

Result<SSLPtr> SecureContext::Impl::Create(
    const X509Ptr& certificate, const EVP_PKEYPtr& pkey,
    rpc::UseCertificateKeyPair use_certificate_key_pair) const {
  auto ssl = SSLPtr(SSL_new(context_.get()));
  if (use_certificate_key_pair) {
    auto res = SSL_use_certificate(ssl.get(), certificate.get());
    if (res != 1) {
      return SSL_STATUS(InvalidArgument, "Failed to use certificate: $0");
    }
    res = SSL_use_PrivateKey(ssl.get(), pkey.get());
    if (res != 1) {
      return SSL_STATUS(InvalidArgument, "Failed to use private key: $0");
    }
  }
  return ssl;
}

Status SecureContext::Impl::AddCertificateAuthorityFile(const std::string& file) {
  UNIQUE_LOCK(lock, mutex_);
  return AddCertificateAuthorityFileUnlocked(file);
}

Status SecureContext::Impl::AddCertificateAuthorityFileUnlocked(const std::string& file) {
  X509_STORE* store = SSL_CTX_get_cert_store(context_.get());
  if (!store) {
    return SSL_STATUS(IllegalState, "Failed to get store: $0");
  }

  auto bytes = pointer_cast<const char*>(file.c_str());
  auto res = X509_STORE_load_locations(store, bytes, nullptr);
  if (res != 1) {
    return SSL_STATUS(InvalidArgument, "Failed to add certificate file: $0");
  }

  return Status::OK();
}

Status SecureContext::Impl::AddCertificateAuthority(X509* cert) {
  X509_STORE* store = SSL_CTX_get_cert_store(context_.get());
  if (!store) {
    return SSL_STATUS(IllegalState, "Failed to get store: $0");
  }

  auto res = X509_STORE_add_cert(store, cert);
  if (res != 1) {
    return SSL_STATUS(InvalidArgument, "Failed to add certificate: $0");
  }

  return Status::OK();
}

Status SecureContext::Impl::UseCertificateKeyPair(
    const Slice& certificate_data, const Slice& pkey_data) {
  ERR_clear_error();
  auto certificate = VERIFY_RESULT(X509FromSlice(certificate_data));

  ERR_clear_error();
  auto bio = VERIFY_RESULT(BIOFromSlice(pkey_data));
  auto pkey = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
  if (!pkey) {
    return SSL_STATUS(IOError, "Failed to read private key: $0");
  }

  return UseCertificateKeyPair(std::move(certificate), EVP_PKEYPtr(pkey));
}

Status SecureContext::Impl::UseCertificateKeyPair(X509Ptr&& certificate, EVP_PKEYPtr&& pkey) {
  RETURN_NOT_OK(Create(certificate, pkey, rpc::UseCertificateKeyPair::kTrue));

  certificate_ = std::move(certificate);
  pkey_ = std::move(pkey);

  return Status::OK();
}

Status SecureContext::Impl::UseCertificates(
    const std::string& ca_cert_file, const Slice& certificate_data, const Slice& pkey_data) {
  UNIQUE_LOCK(lock, mutex_);

  RETURN_NOT_OK(AddCertificateAuthorityFileUnlocked(ca_cert_file));
  RETURN_NOT_OK(UseCertificateKeyPair(certificate_data, pkey_data));

  return Status::OK();
}

Status SecureContext::Impl::TEST_GenerateKeys(int bits, const std::string& common_name,
                                              MatchingCertKeyPair matching_cert_key_pair) {
  auto ca_key = VERIFY_RESULT(GeneratePrivateKey(bits));
  auto ca_cert = VERIFY_RESULT(CreateCertificate(ca_key.get(), "YugaByte", ca_key.get(), nullptr));
  auto key = VERIFY_RESULT(GeneratePrivateKey(bits));
  auto cert = VERIFY_RESULT(CreateCertificate(key.get(), common_name, ca_key.get(), ca_cert.get()));

  if (!matching_cert_key_pair) {
    key = VERIFY_RESULT(GeneratePrivateKey(bits));
  }

  UNIQUE_LOCK(lock, mutex_);
  RETURN_NOT_OK(AddCertificateAuthority(ca_cert.get()));
  RETURN_NOT_OK(UseCertificateKeyPair(std::move(cert), std::move(key)));

  return Status::OK();
}

SecureContext::SecureContext(RequireClientCertificate require_client_certificate,
                             UseClientCertificate use_client_certificate,
                             const std::string& required_uid):
  impl_(std::make_unique<Impl>(require_client_certificate, use_client_certificate, required_uid)) {
}

SecureContext::~SecureContext() { }

Status SecureContext::AddCertificateAuthorityFile(const std::string& file) {
  return impl_->AddCertificateAuthorityFile(file);
}

Status SecureContext::UseCertificates(
    const std::string& ca_cert_file, const Slice& certificate_data, const Slice& pkey_data) {
  return impl_->UseCertificates(ca_cert_file, certificate_data, pkey_data);
}

Status SecureContext::TEST_GenerateKeys(int bits, const std::string& common_name,
                                        MatchingCertKeyPair matching_cert_key_pair) {
  return impl_->TEST_GenerateKeys(bits, common_name, matching_cert_key_pair);
}

class SecureRefiner : public StreamRefiner {
 public:
  SecureRefiner(const SecureContext& context, const StreamCreateData& data)
    : secure_context_(*context.impl_), remote_hostname_(data.remote_hostname) {
  }

 private:
  void Start(RefinedStream* stream) override {
    stream_ = stream;
  }

  Status Handshake() ON_REACTOR_THREAD override;
  Status Init();

  Status Send(OutboundDataPtr data) ON_REACTOR_THREAD override;
  Status ProcessHeader() ON_REACTOR_THREAD override;
  Result<ReadBufferFull> Read(StreamReadBuffer* out) override;

  std::string ToString() const override {
    return "SECURE";
  }

  const Protocol* GetProtocol() override {
    return SecureStreamProtocol();
  }

  static int VerifyCallback(int preverified, X509_STORE_CTX* store_context);
  Status Verify(bool preverified, X509_STORE_CTX* store_context);
  bool MatchEndpoint(X509* cert, GENERAL_NAMES* gens);
  bool MatchUid(X509* cert, GENERAL_NAMES* gens);
  bool MatchUidEntry(const Slice& value, const char* name);
  Result<bool> WriteEncrypted(OutboundDataPtr data) ON_REACTOR_THREAD;
  void DecryptReceived();

  Status Established(RefinedStreamState state) ON_REACTOR_THREAD {
    VLOG_WITH_PREFIX(4) << "Established with state: " << state << ", used cipher: "
                        << SSL_get_cipher_name(ssl_.get());

    return stream_->Established(state);
  }

  const std::string& LogPrefix() const {
    return stream_->LogPrefix();
  }

  const SecureContext::Impl& secure_context_;
  const std::string remote_hostname_;
  RefinedStream* stream_ = nullptr;
  std::vector<std::string> certificate_entries_;

  BIOPtr bio_;
  SSLPtr ssl_;
  Status verification_status_;
};

Status SecureRefiner::Send(OutboundDataPtr data) {
  boost::container::small_vector<RefCntBuffer, 10> queue;
  data->Serialize(&queue);
  for (const auto& buf : queue) {
    Slice slice(buf.data(), buf.size());
    for (;;) {
      int slice_size = narrow_cast<int>(slice.size());
      auto len = SSL_write(ssl_.get(), slice.data(), slice_size);
      if (len == slice_size) {
        break;
      }
      auto error = len <= 0 ? SSL_get_error(ssl_.get(), len) : SSL_ERROR_NONE;
      VLOG_WITH_PREFIX(4) << "SSL_write was not full: " << slice.size() << ", written: " << len
                          << ", error: " << error;
      if (error != SSL_ERROR_NONE) {
        if (error != SSL_ERROR_WANT_WRITE || !VERIFY_RESULT(WriteEncrypted(nullptr))) {
          return STATUS_FORMAT(
              NetworkError, "SSL write failed: $0 ($1)", SSLErrorMessage(error), error);
        }
      } else {
        RETURN_NOT_OK(WriteEncrypted(nullptr));
      }
      if (len > 0) {
        slice.remove_prefix(len);
      }
    }
  }
  return ResultToStatus(WriteEncrypted(std::move(data)));
}

Result<bool> SecureRefiner::WriteEncrypted(OutboundDataPtr data) {
  auto pending = BIO_ctrl_pending(bio_.get());
  if (pending == 0) {
    return data ? STATUS(NetworkError, "No pending data during write") : Result<bool>(false);
  }
  RefCntBuffer buf(pending);
  int buf_size = narrow_cast<int>(buf.size());
  auto len = BIO_read(bio_.get(), buf.data(), buf_size);
  LOG_IF_WITH_PREFIX(DFATAL, len != buf_size)
      << "BIO_read was not full: " << buf.size() << ", read: " << len;
  VLOG_WITH_PREFIX(4) << "Write encrypted: " << len << ", " << AsString(data);
  RETURN_NOT_OK(stream_->SendToLower(std::make_shared<SingleBufferOutboundData>(
      buf, std::move(data))));
  return true;
}

Status SecureRefiner::ProcessHeader() {
  auto data = stream_->ReadBuffer().AppendedVecs();
  if (data.empty() || data[0].iov_len < 2) {
    return Status::OK();
  }

  const auto* bytes = static_cast<const uint8_t*>(data[0].iov_base);
  if (bytes[0] == 0x16 && bytes[1] == 0x03) { // TLS handshake header
    RETURN_NOT_OK(Init());
    return stream_->StartHandshake();
  }

  if (!FLAGS_allow_insecure_connections) {
    return STATUS_FORMAT(NetworkError, "Insecure connection header: $0",
                         Slice(bytes, 2).ToDebugHexString());
  }

  return Established(RefinedStreamState::kDisabled);
}

// Tries to do SSL_read up to num bytes from buf. Possible results:
// > 0 - number of bytes actually read.
// = 0 - in case of SSL_ERROR_WANT_READ.
// Status with network error - in case of other errors.
Result<ReadBufferFull> SecureRefiner::Read(StreamReadBuffer* out) {
  DecryptReceived();
  auto total = 0;
  auto iovecs = VERIFY_RESULT(out->PrepareAppend());
  auto iov_it = iovecs.begin();
  for (;;) {
    auto len = SSL_read(ssl_.get(), iov_it->iov_base, narrow_cast<int>(iov_it->iov_len));

    if (len <= 0) {
      auto error = SSL_get_error(ssl_.get(), len);
      if (error == SSL_ERROR_WANT_READ) {
        VLOG_WITH_PREFIX(4) << "Read decrypted: SSL_ERROR_WANT_READ";
        break;
      }
      auto status = STATUS_FORMAT(
          NetworkError, "SSL read failed: $0 ($1)", SSLErrorMessage(error), error);
      LOG_WITH_PREFIX(INFO) << status;
      return status;
    }

    VLOG_WITH_PREFIX(4) << "Read decrypted: " << len;
    total += len;
    IoVecRemovePrefix(len, &*iov_it);
    if (iov_it->iov_len == 0) {
      if (++iov_it == iovecs.end()) {
        break;
      }
    }
  }
  out->DataAppended(total);
  return ReadBufferFull(out->Full());
}

void SecureRefiner::DecryptReceived() {
  auto& inp = stream_->ReadBuffer();
  if (inp.Empty()) {
    return;
  }
  size_t total = 0;
  for (const auto& iov : inp.AppendedVecs()) {
    auto res = BIO_write(bio_.get(), iov.iov_base, narrow_cast<int>(iov.iov_len));
    VLOG_WITH_PREFIX(4) << "Decrypted: " << res << " of " << iov.iov_len;
    if (res <= 0) {
      break;
    }
    total += res;
    if (implicit_cast<size_t>(res) < iov.iov_len) {
      break;
    }
  }
  inp.Consume(total, {});
}

Status SecureRefiner::Handshake() {
  RETURN_NOT_OK(Init());

  DecryptReceived();

  for (;;) {
    if (stream_->IsConnected()) {
      return Status::OK();
    }

    auto pending_before = BIO_ctrl_pending(bio_.get());
    ERR_clear_error();
    int result = stream_->local_side() == LocalSide::kClient
        ? SSL_connect(ssl_.get()) : SSL_accept(ssl_.get());
    int ssl_error = SSL_get_error(ssl_.get(), result);
    int sys_error = static_cast<int>(ERR_get_error());
    auto pending_after = BIO_ctrl_pending(bio_.get());

    if (ssl_error == SSL_ERROR_SSL || ssl_error == SSL_ERROR_SYSCALL) {
      std::string message = verification_status_.ok()
          ? (ssl_error == SSL_ERROR_SSL ? SSLErrorMessage(sys_error) : ErrnoToString(sys_error))
          : verification_status_.ToString();
      std::string message_suffix;
      if (FLAGS_dump_certificate_entries) {
        message_suffix = Format(", certificate entries: $0", certificate_entries_);
      }
      return STATUS_FORMAT(NetworkError, "Handshake failed: $0, address: $1, hostname: $2$3",
                           message, stream_->Remote().address(), remote_hostname_, message_suffix);
    }

    if (ssl_error == SSL_ERROR_WANT_WRITE || pending_after > pending_before) {
      // SSL expects that we would write to underlying transport.
      RefCntBuffer buffer(pending_after);
      int len = BIO_read(bio_.get(), buffer.data(), narrow_cast<int>(buffer.size()));
      DCHECK_EQ(len, pending_after);
      RETURN_NOT_OK(stream_->SendToLower(
          std::make_shared<SingleBufferOutboundData>(buffer, nullptr)));
      // If SSL_connect/SSL_accept returned positive result it means that TLS connection
      // was succesfully established. We just have to send last portion of data.
      if (result > 0) {
        RETURN_NOT_OK(Established(RefinedStreamState::kEnabled));
      }
    } else if (ssl_error == SSL_ERROR_WANT_READ) {
      // SSL expects that we would read from underlying transport.
      return Status::OK();
    } else if (SSL_get_shutdown(ssl_.get()) & SSL_RECEIVED_SHUTDOWN) {
      return STATUS(Aborted, "Handshake aborted");
    } else {
      return Established(RefinedStreamState::kEnabled);
    }
  }
}

Status SecureRefiner::Init() {
  if (ssl_) {
    return Status::OK();
  }

  ssl_ = VERIFY_RESULT(secure_context_.Create(UseCertificateKeyPair(
      stream_->local_side() == LocalSide::kServer || secure_context_.use_client_certificate())));
  SSL_set_mode(ssl_.get(), SSL_MODE_ENABLE_PARTIAL_WRITE);
  SSL_set_mode(ssl_.get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
  SSL_set_mode(ssl_.get(), SSL_MODE_RELEASE_BUFFERS);
  SSL_set_app_data(ssl_.get(), this);

  BIO* int_bio = nullptr;
  BIO* temp_bio = nullptr;
  BIO_new_bio_pair(&int_bio, 0, &temp_bio, 0);
  SSL_set_bio(ssl_.get(), int_bio, int_bio);
  bio_.reset(temp_bio);

  int verify_mode = SSL_VERIFY_PEER;
  if (secure_context_.require_client_certificate()) {
    verify_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
  }
  SSL_set_verify(ssl_.get(), verify_mode, &VerifyCallback);

  return Status::OK();
}

int SecureRefiner::VerifyCallback(int preverified, X509_STORE_CTX* store_context) {
  if (!store_context) {
    return preverified;
  }

  auto ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(
      store_context, SSL_get_ex_data_X509_STORE_CTX_idx()));
  if (!ssl) {
    return preverified;
  }

  auto refiner = static_cast<SecureRefiner*>(SSL_get_app_data(ssl));

  if (!refiner) {
    return preverified;
  }

  auto status = refiner->Verify(preverified != 0, store_context);
  if (status.ok()) {
    return 1;
  }

  VLOG(4) << refiner->LogPrefix() << status;
  refiner->verification_status_ = status;
  return 0;
}

namespace {

// Matches pattern from RFC 2818:
// Names may contain the wildcard character * which is considered to match any single domain name
// component or component fragment. E.g., *.a.com matches foo.a.com but not bar.foo.a.com.
// f*.com matches foo.com but not bar.com.
bool MatchPattern(Slice pattern, Slice host) {
  const char* p = pattern.cdata();
  const char* p_end = pattern.cend();
  const char* h = host.cdata();
  const char* h_end = host.cend();

  while (p != p_end && h != h_end) {
    if (*p == '*') {
      ++p;
      while (h != h_end && *h != '.') {
        if (MatchPattern(Slice(p, p_end), Slice(h, h_end))) {
          return true;
        }
        ++h;
      }
    } else if (std::tolower(*p) == std::tolower(*h)) {
      ++p;
      ++h;
    } else {
      return false;
    }
  }

  return p == p_end && h == h_end;
}

Slice GetEntryByNid(X509* cert, int nid) {
  X509_NAME* name = X509_get_subject_name(cert);
  int last_i = -1;
  for (int i = -1; (i = X509_NAME_get_index_by_NID(name, nid, i)) >= 0; ) {
    last_i = i;
  }
  if (last_i == -1) {
    return Slice();
  }
  auto* name_entry = X509_NAME_get_entry(name, last_i);
  if (!name_entry) {
    LOG(DFATAL) << "No name entry in certificate at index: " << last_i;
    return Slice();
  }
  auto* common_name = X509_NAME_ENTRY_get_data(name_entry);

  if (common_name && common_name->data && common_name->length) {
    return Slice(common_name->data, common_name->length);
  }

  return Slice();
}

Slice GetCommonName(X509* cert) {
  return GetEntryByNid(cert, NID_commonName);
}

} // namespace

bool SecureRefiner::MatchEndpoint(X509* cert, GENERAL_NAMES* gens) {
  auto address = stream_->Remote().address();

  for (int i = 0; i < sk_GENERAL_NAME_num(gens); ++i) {
    GENERAL_NAME* gen = sk_GENERAL_NAME_value(gens, i);
    if (gen->type == GEN_DNS) {
      ASN1_IA5STRING* domain = gen->d.dNSName;
      if (domain->type == V_ASN1_IA5STRING && domain->data && domain->length) {
        Slice domain_slice(domain->data, domain->length);
        VLOG_WITH_PREFIX(4) << "Domain: " << domain_slice.ToBuffer() << " vs " << remote_hostname_;
        if (FLAGS_dump_certificate_entries) {
          certificate_entries_.push_back(Format("DNS:$0", domain_slice.ToBuffer()));
        }
        if (MatchPattern(domain_slice, remote_hostname_)) {
          return true;
        }
      }
    } else if (gen->type == GEN_IPADD) {
      ASN1_OCTET_STRING* ip_address = gen->d.iPAddress;
      if (ip_address->type == V_ASN1_OCTET_STRING && ip_address->data) {
        if (ip_address->length == 4) {
          boost::asio::ip::address_v4::bytes_type bytes;
          memcpy(&bytes, ip_address->data, bytes.size());
          auto allowed_address = boost::asio::ip::address_v4(bytes);
          VLOG_WITH_PREFIX(4) << "IPv4: " << allowed_address.to_string() << " vs " << address;
          if (FLAGS_dump_certificate_entries) {
            certificate_entries_.push_back(Format("IP Address:$0", allowed_address));
          }
          if (address == allowed_address) {
            return true;
          }
        } else if (ip_address->length == 16) {
          boost::asio::ip::address_v6::bytes_type bytes;
          memcpy(&bytes, ip_address->data, bytes.size());
          auto allowed_address = boost::asio::ip::address_v6(bytes);
          VLOG_WITH_PREFIX(4) << "IPv6: " << allowed_address.to_string() << " vs " << address;
          if (FLAGS_dump_certificate_entries) {
            certificate_entries_.push_back(Format("IP Address:$0", allowed_address));
          }
          if (address == allowed_address) {
            return true;
          }
        }
      }
    }
  }

  // No match in the alternate names, so try the common names. We should only
  // use the "most specific" common name, which is the last one in the list.
  Slice common_name = GetCommonName(cert);
  if (!common_name.empty()) {
    VLOG_WITH_PREFIX(4) << "Common name: " << common_name.ToBuffer() << " vs "
                        << stream_->Remote().address() << "/" << remote_hostname_;
    if (common_name == stream_->Remote().address().to_string() ||
        MatchPattern(common_name, remote_hostname_)) {
      return true;
    }
  }

  VLOG_WITH_PREFIX(4) << "Nothing suitable for " << stream_->Remote().address() << "/"
                      << remote_hostname_;

  return false;
}

bool SecureRefiner::MatchUidEntry(const Slice& value, const char* name) {
  if (value == secure_context_.required_uid()) {
    VLOG_WITH_PREFIX(4) << "Accepted " << name << ": " << value.ToBuffer();
    return true;
  } else if (!value.empty()) {
    VLOG_WITH_PREFIX(4) << "Rejected " << name << ": " << value.ToBuffer() << ", while "
                        << secure_context_.required_uid() << " required";
  }
  return false;
}

bool IsStringType(int type) {
  switch (type) {
    case V_ASN1_UTF8STRING: FALLTHROUGH_INTENDED;
    case V_ASN1_IA5STRING: FALLTHROUGH_INTENDED;
    case V_ASN1_UNIVERSALSTRING: FALLTHROUGH_INTENDED;
    case V_ASN1_BMPSTRING: FALLTHROUGH_INTENDED;
    case V_ASN1_VISIBLESTRING: FALLTHROUGH_INTENDED;
    case V_ASN1_PRINTABLESTRING: FALLTHROUGH_INTENDED;
    case V_ASN1_TELETEXSTRING: FALLTHROUGH_INTENDED;
    case V_ASN1_GENERALSTRING: FALLTHROUGH_INTENDED;
    case V_ASN1_NUMERICSTRING:
      return true;
  }
  return false;
}

bool SecureRefiner::MatchUid(X509* cert, GENERAL_NAMES* gens) {
  if (MatchUidEntry(GetCommonName(cert), "common name")) {
    return true;
  }

  auto uid = GetEntryByNid(cert, NID_userId);
  if (!uid.empty()) {
    if (FLAGS_dump_certificate_entries) {
      certificate_entries_.push_back(Format("UID:$0", uid.ToBuffer()));
    }
    if (MatchUidEntry(uid, "uid")) {
      return true;
    }
  }

  for (int i = 0; i < sk_GENERAL_NAME_num(gens); ++i) {
    GENERAL_NAME* gen = sk_GENERAL_NAME_value(gens, i);
    if (gen->type == GEN_OTHERNAME) {
      auto value = gen->d.otherName->value;
      if (IsStringType(value->type)) {
        Slice other_name(value->value.asn1_string->data, value->value.asn1_string->length);
        if (!other_name.empty()) {
          if (FLAGS_dump_certificate_entries) {
            certificate_entries_.push_back(Format("ON:$0", other_name.ToBuffer()));
          }
          if (MatchUidEntry(other_name, "other name")) {
            return true;
          }
        }
      }
    }
  }
  VLOG_WITH_PREFIX(4) << "Not found entry for UID " << secure_context_.required_uid();

  return false;
}

// Verify according to RFC 2818.
Status SecureRefiner::Verify(bool preverified, X509_STORE_CTX* store_context) {
  // Don't bother looking at certificates that have failed pre-verification.
  if (!preverified) {
    auto err = X509_STORE_CTX_get_error(store_context);
    return STATUS_FORMAT(
        NetworkError, "Unverified certificate: $0", X509_verify_cert_error_string(err));
  }

  // We're only interested in checking the certificate at the end of the chain.
  int depth = X509_STORE_CTX_get_error_depth(store_context);
  if (depth > 0) {
    VLOG_WITH_PREFIX(4) << "Intermediate certificate";
    return Status::OK();
  }

  X509* cert = X509_STORE_CTX_get_current_cert(store_context);
  auto gens = static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(
      cert, NID_subject_alt_name, nullptr, nullptr));
  auto se = ScopeExit([gens] {
    GENERAL_NAMES_free(gens);
  });

  if (FLAGS_dump_certificate_entries) {
    certificate_entries_.push_back(Format("CN:$0", GetCommonName(cert).ToBuffer()));
  }

  if (!secure_context_.required_uid().empty()) {
    if (!MatchUid(cert, gens)) {
      return STATUS_FORMAT(
          NetworkError, "Uid does not match: $0", secure_context_.required_uid());
    }
  } else {
    VLOG_WITH_PREFIX(4) << "Skip UID verification";
  }

  bool verify_endpoint = stream_->local_side() == LocalSide::kClient ? FLAGS_verify_server_endpoint
                                                                     : FLAGS_verify_client_endpoint;
  if (verify_endpoint) {
    if (!MatchEndpoint(cert, gens)) {
      return STATUS(NetworkError, "Endpoint does not match");
    }
  } else {
    VLOG_WITH_PREFIX(4) << "Skip endpoint verification";
  }

  return Status::OK();
}

const Protocol* SecureStreamProtocol() {
  static Protocol result("tcps");
  return &result;
}

StreamFactoryPtr SecureStreamFactory(
    StreamFactoryPtr lower_layer_factory, const MemTrackerPtr& buffer_tracker,
    const SecureContext* context) {
  return std::make_shared<RefinedStreamFactory>(
      std::move(lower_layer_factory), buffer_tracker, [context](const StreamCreateData& data) {
    return std::make_unique<SecureRefiner>(*context, data);
  });
}

} // namespace rpc
} // namespace yb

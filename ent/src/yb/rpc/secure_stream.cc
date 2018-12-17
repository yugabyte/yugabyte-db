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

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <boost/scope_exit.hpp>

#include "yb/rpc/outbound_data.h"

#include "yb/util/errno.h"
#include "yb/util/size_literals.h"

using namespace std::literals;

DEFINE_bool(allow_insecure_connections, true, "Whether we should allow insecure connections.");

namespace yb {
namespace rpc {

namespace {

const unsigned char kContextId[] = { 'Y', 'u', 'g', 'a', 'B', 'y', 't', 'e' };

std::vector<std::unique_ptr<std::mutex>> crypto_mutexes;

__attribute__((unused)) void NO_THREAD_SAFETY_ANALYSIS LockingCallback(
    int mode, int n, const char* /*file*/, int /*line*/) {
  CHECK_LT(static_cast<size_t>(n), crypto_mutexes.size());
  if (mode & CRYPTO_LOCK) {
    crypto_mutexes[n]->lock();
  } else {
    crypto_mutexes[n]->unlock();
  }
}

std::string SSLErrorMessage(int error) {
  auto message = ERR_reason_error_string(error);
  return message ? message : "no error";
}

class SecureOutboundData : public OutboundData {
 public:
  SecureOutboundData(RefCntBuffer buffer, OutboundDataPtr lower_data)
      : buffer_(std::move(buffer)), lower_data_(std::move(lower_data)) {}

  void Transferred(const Status& status, Connection* conn) override {
    if (lower_data_) {
      lower_data_->Transferred(status, conn);
    }
  }

  bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override {
    return false;
  }

  void Serialize(boost::container::small_vector_base<RefCntBuffer>* output) const override {
    output->push_back(buffer_);
  }

  std::string ToString() const override {
    return Format("Secure[$0]", lower_data_);
  }

 private:
  RefCntBuffer buffer_;
  OutboundDataPtr lower_data_;
};

#define YB_RPC_SSL_TYPE_DEFINE(name) \
  void BOOST_PP_CAT(name, Free)::operator()(name* value) const { \
    BOOST_PP_CAT(name, _free)(value); \
  } \

#define YB_RPC_SSL_TYPE(name) YB_RPC_SSL_TYPE_DECLARE(name) YB_RPC_SSL_TYPE_DEFINE(name)

#define SSL_STATUS(type, format) STATUS_FORMAT(type, format, SSLErrorMessage(ERR_get_error()))

Result<detail::BIOPtr> BIOFromSlice(const Slice& data) {
  detail::BIOPtr bio(BIO_new_mem_buf(data.data(), data.size()));
  if (!bio) {
    return SSL_STATUS(IOError, "Create BIO failed: $0");
  }
  return std::move(bio);
}

Result<detail::X509Ptr> X509FromSlice(const Slice& data) {
  ERR_clear_error();

  auto bio = VERIFY_RESULT(BIOFromSlice(data));

  detail::X509Ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  if (!cert) {
    return SSL_STATUS(IOError, "Read cert failed: $0");
  }

  return std::move(cert);
}

YB_RPC_SSL_TYPE(ASN1_INTEGER);
YB_RPC_SSL_TYPE(RSA);
YB_RPC_SSL_TYPE(X509_NAME);

Result<detail::EVP_PKEYPtr> GeneratePrivateKey(int bits) {
  RSAPtr rsa(RSA_generate_key(bits, 65537, nullptr, nullptr));
  if (!rsa) {
    return SSL_STATUS(InvalidArgument, "Failed to generate private key: $0");
  }

  detail::EVP_PKEYPtr pkey(EVP_PKEY_new());
  auto res = EVP_PKEY_assign_RSA(pkey.get(), rsa.release());
  if (res != 1) {
    return SSL_STATUS(InvalidArgument, "Failed to assign private key: $0");
  }

  return std::move(pkey);
}

Result<detail::X509Ptr> CreateCertificate(
    EVP_PKEY* key, const std::string& common_name, EVP_PKEY* ca_pkey, X509* ca_cert) {
  detail::X509Ptr cert(X509_new());
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
      name.get(), "CN", MBSTRING_ASC, bytes, common_name.length(), -1, 0)) {
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

class OpenSSLInitializer {
 public:
  OpenSSLInitializer() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    OpenSSL_add_all_ciphers();

    while (crypto_mutexes.size() != CRYPTO_num_locks()) {
      crypto_mutexes.emplace_back(std::make_unique<std::mutex>());
    }
    CRYPTO_set_locking_callback(&LockingCallback);
  }

  ~OpenSSLInitializer() {
    CRYPTO_set_locking_callback(nullptr);
    ERR_free_strings();
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();
    ERR_remove_thread_state(nullptr);
    SSL_COMP_free_compression_methods();
  }
};

OpenSSLInitializer* InitOpenSSL() {
  static std::unique_ptr<OpenSSLInitializer> initializer = std::make_unique<OpenSSLInitializer>();
  return initializer.get();
}

} // namespace

namespace detail {

YB_RPC_SSL_TYPE_DEFINE(BIO)
YB_RPC_SSL_TYPE_DEFINE(EVP_PKEY)
YB_RPC_SSL_TYPE_DEFINE(SSL)
YB_RPC_SSL_TYPE_DEFINE(SSL_CTX)
YB_RPC_SSL_TYPE_DEFINE(X509)

}

SecureContext::SecureContext() {
  InitOpenSSL();

  context_.reset(SSL_CTX_new(SSLv23_method()));
  DCHECK(context_);

  SSL_CTX_set_options(context_.get(), SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);
  auto res = SSL_CTX_set_session_id_context(context_.get(), kContextId, sizeof(kContextId));
  LOG_IF(DFATAL, res != 1) << "Failed to set session id for SSL context: "
                           << SSLErrorMessage(ERR_get_error());
}

detail::SSLPtr SecureContext::Create() const {
  return detail::SSLPtr(SSL_new(context_.get()));
}

Status SecureContext::AddCertificateAuthority(const Slice& data) {
  return AddCertificateAuthority(VERIFY_RESULT(X509FromSlice(data)).get());
}

Status SecureContext::AddCertificateAuthority(X509* cert) {
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

Status SecureContext::TEST_GenerateKeys(int bits, const std::string& common_name) {
  auto ca_key = VERIFY_RESULT(GeneratePrivateKey(bits));
  auto ca_cert = VERIFY_RESULT(CreateCertificate(ca_key.get(), "YugaByte", ca_key.get(), nullptr));
  auto key = VERIFY_RESULT(GeneratePrivateKey(bits));
  auto cert = VERIFY_RESULT(CreateCertificate(key.get(), common_name, ca_key.get(), ca_cert.get()));

  RETURN_NOT_OK(AddCertificateAuthority(ca_cert.get()));
  pkey_ = std::move(key);
  certificate_ = std::move(cert);

  return Status::OK();
}

Status SecureContext::UsePrivateKey(const Slice& slice) {
  ERR_clear_error();

  auto bio = VERIFY_RESULT(BIOFromSlice(slice));

  auto pkey = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
  if (!pkey) {
    return SSL_STATUS(IOError, "Failed to read private key: $0");
  }

  pkey_.reset(pkey);
  return Status::OK();
}

Status SecureContext::UseCertificate(const Slice& data) {
  ERR_clear_error();

  certificate_ = VERIFY_RESULT(X509FromSlice(data));

  return Status::OK();
}

SecureStream::SecureStream(const SecureContext& context, std::unique_ptr<Stream> lower_stream,
                           GrowableBufferAllocator* allocator, size_t limit)
    : secure_context_(context), lower_stream_(std::move(lower_stream)),
      received_data_(allocator, limit) {
}

Status SecureStream::Start(bool connect, ev::loop_ref* loop, StreamContext* context) {
  context_ = context;
  need_connect_ = connect;
  return lower_stream_->Start(connect, loop, this);
}

void SecureStream::Close() {
  lower_stream_->Close();
}

void SecureStream::Shutdown(const Status& status) {
  for (auto& data : pending_data_) {
    if (data) {
      context_->Transferred(data, status);
    }
  }
  pending_data_.clear();

  lower_stream_->Shutdown(status);
}

void SecureStream::Send(OutboundDataPtr data) {
  switch (state_) {
  case SecureState::kInitial:
  case SecureState::kHandshake:
    pending_data_.push_back(std::move(data));
    break;
  case SecureState::kEnabled: {
      {
        boost::container::small_vector<RefCntBuffer, 10> queue;
        data->Serialize(&queue);
        for (const auto& buf : queue) {
          auto len = SSL_write(ssl_.get(), buf.data(), buf.size());
          DCHECK_EQ(len, buf.size());
        }
      }
      RefCntBuffer buf(BIO_ctrl_pending(bio_.get()));
      auto len2 = BIO_read(bio_.get(), buf.data(), buf.size());
      DCHECK_EQ(len2, buf.size());
      lower_stream_->Send(std::make_shared<SecureOutboundData>(buf, data));
    } break;
  case SecureState::kDisabled:
    lower_stream_->Send(std::move(data));
    break;
  }
}

Status SecureStream::TryWrite() {
  return lower_stream_->TryWrite();
}

void SecureStream::ParseReceived() {
  lower_stream_->ParseReceived();
}

bool SecureStream::Idle(std::string* reason) {
  return lower_stream_->Idle(reason);
}

bool SecureStream::IsConnected() {
  return lower_stream_->IsConnected();
}

void SecureStream::DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) {
  lower_stream_->DumpPB(req, resp);
}

const Endpoint& SecureStream::Remote() {
  return lower_stream_->Remote();
}

const Endpoint& SecureStream::Local() {
  return lower_stream_->Local();
}

void SecureStream::UpdateLastActivity() {
  context_->UpdateLastActivity();
}

void SecureStream::Transferred(const OutboundDataPtr& data, const Status& status) {
  context_->Transferred(data, status);
}

void SecureStream::Destroy(const Status& status) {
  context_->Destroy(status);
}

Result<size_t> SecureStream::ProcessReceived(const IoVecs& data, ReadBufferFull read_buffer_full) {
  switch (state_) {
    case SecureState::kInitial: {
      if (data[0].iov_len < 2) {
        return 0;
      }
      const uint8_t* bytes = static_cast<const uint8_t*>(data[0].iov_base);
      if (bytes[0] == 0x16 && bytes[1] == 0x03) { // TLS handshake header
        state_ = SecureState::kHandshake;
        RETURN_NOT_OK(Init());
      } else if (FLAGS_allow_insecure_connections) {
        Established(SecureState::kDisabled);
      } else {
        return STATUS_FORMAT(NetworkError, "Insecure connection header: $0",
                             Slice(bytes, 2).ToDebugHexString());
      }
      return ProcessReceived(data, read_buffer_full);
    }

    case SecureState::kDisabled:
      return context_->ProcessReceived(data, read_buffer_full);

    case SecureState::kHandshake: {
      size_t result = 0;
      for (const auto& iov : data) {
        auto written = BIO_write(bio_.get(), iov.iov_base, iov.iov_len);
        result += written;
        DCHECK_EQ(written, iov.iov_len);
      }
      auto handshake_status = Handshake();
      LOG_IF(INFO, !handshake_status.ok()) << "Handshake failed: " << handshake_status;
      RETURN_NOT_OK(handshake_status);
      return result;
    }

    case SecureState::kEnabled: {
      size_t result = 0;
      for (const auto& iov : data) {
        auto len = BIO_write(bio_.get(), iov.iov_base, iov.iov_len);
        if (len < 0) {
          break;
        }
        result += len;
      }
      // TODO handle IsBusy
      auto out = VERIFY_RESULT(received_data_.PrepareAppend());
      size_t appended = 0;
      for (auto iov = out.begin(); iov != out.end();) {
        auto len = SSL_read(ssl_.get(), iov->iov_base, iov->iov_len);
        if (len < 0) {
          break;
        }
        appended += len;
        iov->iov_base = static_cast<char*>(iov->iov_base) + len;
        iov->iov_len -= len;
        if (iov->iov_len <= 0) {
          ++iov;
        }
      }
      received_data_.DataAppended(appended);
      auto temp = VERIFY_RESULT(context_->ProcessReceived(
          received_data_.AppendedVecs(), ReadBufferFull(received_data_.full())));
      received_data_.Consume(temp);
      return result;
    }
  }

  return STATUS_FORMAT(IllegalState, "Unexpected state: $0", to_underlying(state_));
}

void SecureStream::Connected() {
  if (need_connect_) {
    auto status = Init();
    if (status.ok()) {
      status = Handshake();
    }
    if (!status.ok()) {
      context_->Destroy(status);
    }
  }
}

Status SecureStream::Handshake() {
  for (;;) {
    if (state_ == SecureState::kEnabled) {
      return Status::OK();
    }

    auto pending_before = BIO_ctrl_pending(bio_.get());
    ERR_clear_error();
    int result = need_connect_ ? SSL_connect(ssl_.get()) : SSL_accept(ssl_.get());
    int ssl_error = SSL_get_error(ssl_.get(), result);
    int sys_error = static_cast<int>(ERR_get_error());
    auto pending_after = BIO_ctrl_pending(bio_.get());

    if (ssl_error == SSL_ERROR_SSL) {
      return STATUS_FORMAT(NetworkError, "Handshake failed: $0", SSLErrorMessage(sys_error));
    }

    if (ssl_error == SSL_ERROR_SYSCALL) {
      return STATUS_FORMAT(NetworkError, "Handshake failed: $0", ErrnoToString(sys_error));
    }

    if (ssl_error == SSL_ERROR_WANT_WRITE || pending_after > pending_before) {
      // SSL expects that we would write to underlying transport.
      RefCntBuffer buffer(pending_after);
      int len = BIO_read(bio_.get(), buffer.data(), buffer.size());
      DCHECK_EQ(len, pending_after);
      auto data = std::make_shared<SecureOutboundData>(buffer, nullptr);
      lower_stream_->Send(data);
      // If SSL_connect/SSL_accept returned positive result it means that TLS connection
      // was succesfully established. We just have to send last portion of data.
      if (result > 0) {
        Established(SecureState::kEnabled);
      }
    } else if (ssl_error == SSL_ERROR_WANT_READ) {
      // SSL expects that we would read from underlying transport.
      return Status::OK();
    } else if (SSL_get_shutdown(ssl_.get()) & SSL_RECEIVED_SHUTDOWN) {
      return STATUS(Aborted, "Handshake aborted");
    } else {
      Established(SecureState::kEnabled);
      return Status::OK();
    }
  }
}

Status SecureStream::Init() {
  if (!ssl_) {
    ssl_ = secure_context_.Create();
    SSL_set_mode(ssl_.get(), SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_set_mode(ssl_.get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_set_mode(ssl_.get(), SSL_MODE_RELEASE_BUFFERS);
    SSL_set_app_data(ssl_.get(), this);

    if (!need_connect_) {
      auto res = SSL_use_PrivateKey(ssl_.get(), secure_context_.private_key());
      if (res != 1) {
        return SSL_STATUS(InvalidArgument, "Failed to use private key: $0");
      }
      res = SSL_use_certificate(ssl_.get(), secure_context_.certificate());
      if (res != 1) {
        return SSL_STATUS(InvalidArgument, "Failed to use certificate: $0");
      }
    }

    BIO* int_bio = nullptr;
    BIO* temp_bio = nullptr;
    BIO_new_bio_pair(&int_bio, 0, &temp_bio, 0);
    SSL_set_bio(ssl_.get(), int_bio, int_bio);
    bio_.reset(temp_bio);

    SSL_set_verify(ssl_.get(), SSL_VERIFY_PEER, &VerifyCallback);
  }

  return Status::OK();
}

void SecureStream::Established(SecureState state) {
  VLOG(4) << "Established with state: " << state;

  state_ = state;
  context_->Connected();
  for (auto& data : pending_data_) {
    Send(std::move(data));
  }
  pending_data_.clear();
}

int SecureStream::VerifyCallback(int preverified, X509_STORE_CTX* store_context) {
  if (store_context) {
    auto ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(
        store_context, SSL_get_ex_data_X509_STORE_CTX_idx()));
    if (ssl) {
      auto stream = static_cast<SecureStream*>(SSL_get_app_data(ssl));
      if (stream) {
        return stream->Verify(preverified != 0, store_context) ? 1 : 0;
      }
    }
  }

  return preverified;
}

// Verify according to RFC 2818.
bool SecureStream::Verify(bool preverified, X509_STORE_CTX* store_context) {
  // Don't bother looking at certificates that have failed pre-verification.
  if (!preverified) {
    VLOG(4) << "Unverified certificate";
    return false;
  }

  // We're only interested in checking the certificate at the end of the chain.
  int depth = X509_STORE_CTX_get_error_depth(store_context);
  if (depth > 0) {
    VLOG(4) << "Intermediate certificate";
    return true;
  }

  X509* cert = X509_STORE_CTX_get_current_cert(store_context);

  auto gens = static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0));
  BOOST_SCOPE_EXIT(gens) {
    GENERAL_NAMES_free(gens);
  } BOOST_SCOPE_EXIT_END;

  auto address = Remote().address();

  for (int i = 0; i < sk_GENERAL_NAME_num(gens); ++i) {
    GENERAL_NAME* gen = sk_GENERAL_NAME_value(gens, i);
    if (gen->type == GEN_DNS) {
      ASN1_IA5STRING* domain = gen->d.dNSName;
      if (domain->type == V_ASN1_IA5STRING && domain->data && domain->length) {
        VLOG(4) << "Domain: " << Slice(domain->data, domain->length).ToBuffer();
      }
    } else if (gen->type == GEN_IPADD) {
      ASN1_OCTET_STRING* ip_address = gen->d.iPAddress;
      if (ip_address->type == V_ASN1_OCTET_STRING && ip_address->data) {
        if (ip_address->length == 4) {
          boost::asio::ip::address_v4::bytes_type bytes;
          memcpy(&bytes, ip_address->data, bytes.size());
          auto allowed_address = boost::asio::ip::address_v4(bytes);
          VLOG(4) << "IPv4: " << allowed_address.to_string();
          if (address == allowed_address) {
            return true;
          }
        } else if (ip_address->length == 16) {
          boost::asio::ip::address_v6::bytes_type bytes;
          memcpy(&bytes, ip_address->data, bytes.size());
          auto allowed_address = boost::asio::ip::address_v6(bytes);
          VLOG(4) << "IPv6: " << allowed_address.to_string();
          if (address == allowed_address) {
            return true;
          }
        }
      }
    }
  }

  // No match in the alternate names, so try the common names. We should only
  // use the "most specific" common name, which is the last one in the list.
  X509_NAME* name = X509_get_subject_name(cert);
  int i = -1;
  ASN1_STRING* common_name = 0;
  while ((i = X509_NAME_get_index_by_NID(name, NID_commonName, i)) >= 0) {
    X509_NAME_ENTRY* name_entry = X509_NAME_get_entry(name, i);
    common_name = X509_NAME_ENTRY_get_data(name_entry);
  }
  if (common_name && common_name->data && common_name->length) {
    auto common_name_str = Slice(common_name->data, common_name->length).ToBuffer();
    VLOG(4) << "Common name: " << common_name_str;
    if (common_name_str == Remote().address().to_string()) {
      return true;
    }
  }

  VLOG(4) << "Nothing suitable for " << Remote().address();
  return false;
}

const Protocol* SecureStream::StaticProtocol() {
  static Protocol result("tcps");
  return &result;
}

StreamFactoryPtr SecureStream::Factory(
    StreamFactoryPtr lower_layer_factory, SecureContext* context) {
  class SecureStreamFactory : public StreamFactory {
   public:
    SecureStreamFactory(StreamFactoryPtr lower_layer_factory, SecureContext* context)
        : lower_layer_factory_(std::move(lower_layer_factory)), context_(context) {
    }

   private:
    std::unique_ptr<Stream> Create(
        const Endpoint& remote, Socket socket, GrowableBufferAllocator* allocator, size_t limit)
            override {
      auto lower_stream = lower_layer_factory_->Create(remote, std::move(socket), allocator, limit);
      return std::make_unique<SecureStream>(
          *context_, std::move(lower_stream), allocator, limit);
    }

    StreamFactoryPtr lower_layer_factory_;
    SecureContext* context_;
  };

  return std::make_shared<SecureStreamFactory>(std::move(lower_layer_factory), context);
}


} // namespace rpc
} // namespace yb

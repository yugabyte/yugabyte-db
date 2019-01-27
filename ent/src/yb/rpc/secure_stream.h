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

#ifndef ENT_SRC_YB_RPC_SECURE_STREAM_H
#define ENT_SRC_YB_RPC_SECURE_STREAM_H

#include "yb/rpc/stream.h"

#include "yb/rpc/growable_buffer.h"

#include "yb/util/enums.h"

typedef struct bio_st BIO;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct x509_st X509;
typedef struct x509_store_ctx_st X509_STORE_CTX;

namespace yb {
namespace rpc {

YB_DEFINE_ENUM(SecureState, (kInitial)(kHandshake)(kEnabled)(kDisabled));

#define YB_RPC_SSL_TYPE_DECLARE(name) \
  struct BOOST_PP_CAT(name, Free) { \
    void operator()(name* value) const; \
  }; \
  \
  typedef std::unique_ptr<name, BOOST_PP_CAT(name, Free)> BOOST_PP_CAT(name, Ptr);


namespace detail {

YB_RPC_SSL_TYPE_DECLARE(BIO);
YB_RPC_SSL_TYPE_DECLARE(EVP_PKEY);
YB_RPC_SSL_TYPE_DECLARE(SSL);
YB_RPC_SSL_TYPE_DECLARE(SSL_CTX);
YB_RPC_SSL_TYPE_DECLARE(X509);

} // namespace detail

class SecureContext {
 public:
  SecureContext();

  SecureContext(const SecureContext&) = delete;
  void operator=(const SecureContext&) = delete;

  CHECKED_STATUS AddCertificateAuthority(const Slice& data);

  CHECKED_STATUS UsePrivateKey(const Slice& data);
  CHECKED_STATUS UseCertificate(const Slice& data);

  // Generates and uses temporary keys, should be used only during testing.
  CHECKED_STATUS TEST_GenerateKeys(int bits, const std::string& common_name);

  detail::SSLPtr Create() const;
  EVP_PKEY* private_key() const { return pkey_.get(); }
  X509* certificate() const { return certificate_.get(); }

 private:
  CHECKED_STATUS AddCertificateAuthority(X509* cert);

  detail::SSL_CTXPtr context_;
  detail::EVP_PKEYPtr pkey_;
  detail::X509Ptr certificate_;
};

class SecureStream : public Stream, public StreamContext {
 public:
  SecureStream(const SecureContext& context, std::unique_ptr<Stream> lower_stream,
               const StreamCreateData& data);

  SecureStream(const SecureStream&) = delete;
  void operator=(const SecureStream&) = delete;

  static const Protocol* StaticProtocol();
  static StreamFactoryPtr Factory(
      StreamFactoryPtr lower_layer_factory, SecureContext* context);

  size_t GetPendingWriteBytes() override {
    return lower_stream_->GetPendingWriteBytes();
  }

 private:
  CHECKED_STATUS Start(bool connect, ev::loop_ref* loop, StreamContext* context) override;
  void Close() override;
  void Shutdown(const Status& status) override;
  void Send(OutboundDataPtr data) override;
  CHECKED_STATUS TryWrite() override;
  void ParseReceived() override;

  bool Idle(std::string* reason_not_idle) override;
  bool IsConnected() override;
  void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) override;

  const Endpoint& Remote() override;
  const Endpoint& Local() override;

  const Protocol* GetProtocol() override {
    return StaticProtocol();
  }

  void UpdateLastActivity() override;
  void Transferred(const OutboundDataPtr& data, const Status& status) override;
  void Destroy(const Status& status) override;
  Result<size_t> ProcessReceived(const IoVecs& data, ReadBufferFull read_buffer_full) override;
  void Connected() override;

  CHECKED_STATUS Handshake();

  CHECKED_STATUS Init();
  void Established(SecureState state);
  static int VerifyCallback(int preverified, X509_STORE_CTX* store_context);
  bool Verify(bool preverified, X509_STORE_CTX* store_context);

  const SecureContext& secure_context_;
  std::unique_ptr<Stream> lower_stream_;
  const std::string remote_hostname_;
  StreamContext* context_;
  SecureState state_ = SecureState::kInitial;
  bool need_connect_ = false;
  std::vector<OutboundDataPtr> pending_data_;

  GrowableBuffer received_data_;

  detail::BIOPtr bio_;
  detail::SSLPtr ssl_;
};

} // namespace rpc
} // namespace yb

#endif // ENT_SRC_YB_RPC_SECURE_STREAM_H

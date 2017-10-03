// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/rpc/sasl_server.h"

#include <glog/logging.h>
#include <google/protobuf/message_lite.h>
#include <limits>
#include <sasl/sasl.h>
#include <set>
#include <string>

#include "kudu/gutil/endian.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/rpc/blocking_ops.h"
#include "kudu/rpc/auth_store.h"
#include "kudu/rpc/constants.h"
#include "kudu/rpc/serialization.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/net/socket.h"
#include "kudu/util/trace.h"

namespace kudu {
namespace rpc {

static int SaslServerGetoptCb(void* sasl_server, const char* plugin_name, const char* option,
                       const char** result, unsigned* len) {
  return static_cast<SaslServer*>(sasl_server)
    ->GetOptionCb(plugin_name, option, result, len);
}

static int SaslServerPlainAuthCb(sasl_conn_t *conn, void *sasl_server, const char *user,
    const char *pass, unsigned passlen, struct propctx *propctx) {
  return static_cast<SaslServer*>(sasl_server)
    ->PlainAuthCb(conn, user, pass, passlen, propctx);
}

SaslServer::SaslServer(string app_name, int fd)
    : app_name_(std::move(app_name)),
      sock_(fd),
      helper_(SaslHelper::SERVER),
      server_state_(SaslNegotiationState::NEW),
      negotiated_mech_(SaslMechanism::INVALID),
      deadline_(MonoTime::Max()) {
  callbacks_.push_back(SaslBuildCallback(SASL_CB_GETOPT,
      reinterpret_cast<int (*)()>(&SaslServerGetoptCb), this));
  callbacks_.push_back(SaslBuildCallback(SASL_CB_SERVER_USERDB_CHECKPASS,
      reinterpret_cast<int (*)()>(&SaslServerPlainAuthCb), this));
  callbacks_.push_back(SaslBuildCallback(SASL_CB_LIST_END, nullptr, nullptr));
}

SaslServer::~SaslServer() {
  sock_.Release();  // Do not close the underlying socket when this object is destroyed.
}

Status SaslServer::EnableAnonymous() {
  DCHECK_EQ(server_state_, SaslNegotiationState::INITIALIZED);
  return helper_.EnableAnonymous();
}

Status SaslServer::EnablePlain(gscoped_ptr<AuthStore> authstore) {
  DCHECK_EQ(server_state_, SaslNegotiationState::INITIALIZED);
  RETURN_NOT_OK(helper_.EnablePlain());
  authstore_.swap(authstore);
  return Status::OK();
}

SaslMechanism::Type SaslServer::negotiated_mechanism() const {
  DCHECK_EQ(server_state_, SaslNegotiationState::NEGOTIATED);
  return negotiated_mech_;
}

const std::string& SaslServer::plain_auth_user() const {
  DCHECK_EQ(server_state_, SaslNegotiationState::NEGOTIATED);
  DCHECK_EQ(negotiated_mech_, SaslMechanism::PLAIN);
  return plain_auth_user_;
}

void SaslServer::set_local_addr(const Sockaddr& addr) {
  DCHECK_EQ(server_state_, SaslNegotiationState::NEW);
  helper_.set_local_addr(addr);
}

void SaslServer::set_remote_addr(const Sockaddr& addr) {
  DCHECK_EQ(server_state_, SaslNegotiationState::NEW);
  helper_.set_remote_addr(addr);
}

void SaslServer::set_server_fqdn(const string& domain_name) {
  DCHECK_EQ(server_state_, SaslNegotiationState::NEW);
  helper_.set_server_fqdn(domain_name);
}

void SaslServer::set_deadline(const MonoTime& deadline) {
  DCHECK_NE(server_state_, SaslNegotiationState::NEGOTIATED);
  deadline_ = deadline;
}

// calls sasl_server_init() and sasl_server_new()
Status SaslServer::Init(const string& service_type) {
  RETURN_NOT_OK(SaslInit(app_name_.c_str()));

  // Ensure we are not called more than once.
  if (server_state_ != SaslNegotiationState::NEW) {
    return Status::IllegalState("Init() may only be called once per SaslServer object.");
  }

  // TODO: Support security flags.
  unsigned secflags = 0;

  sasl_conn_t* sasl_conn = nullptr;
  int result = sasl_server_new(
      service_type.c_str(),         // Registered name of the service using SASL. Required.
      helper_.server_fqdn(),        // The fully qualified domain name of this server.
      nullptr,                      // Permits multiple user realms on server. NULL == use default.
      helper_.local_addr_string(),  // Local and remote IP address strings. (NULL disables
      helper_.remote_addr_string(), //   mechanisms which require this info.)
      &callbacks_[0],               // Connection-specific callbacks.
      secflags,                     // Security flags.
      &sasl_conn);

  if (PREDICT_FALSE(result != SASL_OK)) {
    return Status::RuntimeError("Unable to create new SASL server",
        SaslErrDesc(result, sasl_conn_.get()));
  }
  sasl_conn_.reset(sasl_conn);

  server_state_ = SaslNegotiationState::INITIALIZED;
  return Status::OK();
}

Status SaslServer::Negotiate() {
  DVLOG(4) << "Called SaslServer::Negotiate()";

  // Ensure we are called exactly once, and in the right order.
  if (server_state_ == SaslNegotiationState::NEW) {
    return Status::IllegalState("SaslServer: Init() must be called before calling Negotiate()");
  } else if (server_state_ == SaslNegotiationState::NEGOTIATED) {
    return Status::IllegalState("SaslServer: Negotiate() may only be called once per object.");
  }

  // Ensure we can use blocking calls on the socket during negotiation.
  RETURN_NOT_OK(EnsureBlockingMode(&sock_));

  faststring recv_buf;

  // Read connection header
  RETURN_NOT_OK(ValidateConnectionHeader(&recv_buf));

  nego_ok_ = false;
  while (!nego_ok_) {
    TRACE("Waiting for next SASL message...");
    RequestHeader header;
    Slice param_buf;
    RETURN_NOT_OK(ReceiveFramedMessageBlocking(&sock_, &recv_buf, &header, &param_buf, deadline_));

    SaslMessagePB request;
    RETURN_NOT_OK(ParseSaslMsgRequest(header, param_buf, &request));

    switch (request.state()) {
      // NEGOTIATE: They want a list of available mechanisms.
      case SaslMessagePB::NEGOTIATE:
        RETURN_NOT_OK(HandleNegotiateRequest(request));
        break;

      // INITIATE: They want to initiate negotiation based on their specified mechanism.
      case SaslMessagePB::INITIATE:
        RETURN_NOT_OK(HandleInitiateRequest(request));
        break;

      // RESPONSE: Client sent a new request as a follow-up to a CHALLENGE response.
      case SaslMessagePB::RESPONSE:
        RETURN_NOT_OK(HandleResponseRequest(request));
        break;

      // Client sent us some unsupported SASL request.
      default: {
        TRACE("SASL Server: Received unsupported request from client");
        Status s = Status::InvalidArgument("RPC server doesn't support SASL state in request",
            SaslMessagePB::SaslState_Name(request.state()));
        RETURN_NOT_OK(SendSaslError(ErrorStatusPB::FATAL_UNAUTHORIZED, s));
        return s;
      }
    }
  }

  TRACE("SASL Server: Successful negotiation");
  server_state_ = SaslNegotiationState::NEGOTIATED;
  return Status::OK();
}

Status SaslServer::ValidateConnectionHeader(faststring* recv_buf) {
  TRACE("Waiting for connection header");
  size_t num_read;
  const size_t conn_header_len = kMagicNumberLength + kHeaderFlagsLength;
  recv_buf->resize(conn_header_len);
  RETURN_NOT_OK(sock_.BlockingRecv(recv_buf->data(), conn_header_len, &num_read, deadline_));
  DCHECK_EQ(conn_header_len, num_read);

  RETURN_NOT_OK(serialization::ValidateConnHeader(*recv_buf));
  TRACE("Connection header received");
  return Status::OK();
}

Status SaslServer::ParseSaslMsgRequest(const RequestHeader& header, const Slice& param_buf,
    SaslMessagePB* request) {
  Status s = helper_.SanityCheckSaslCallId(header.call_id());
  if (!s.ok()) {
    RETURN_NOT_OK(SendSaslError(ErrorStatusPB::FATAL_INVALID_RPC_HEADER, s));
  }

  s = helper_.ParseSaslMessage(param_buf, request);
  if (!s.ok()) {
    RETURN_NOT_OK(SendSaslError(ErrorStatusPB::FATAL_DESERIALIZING_REQUEST, s));
    return s;
  }

  return Status::OK();
}

Status SaslServer::SendSaslMessage(const SaslMessagePB& msg) {
  DCHECK_NE(server_state_, SaslNegotiationState::NEW)
      << "Must not send SASL messages before calling Init()";
  DCHECK_NE(server_state_, SaslNegotiationState::NEGOTIATED)
      << "Must not send SASL messages after Negotiate() succeeds";

  // Create header with SASL-specific callId
  ResponseHeader header;
  header.set_call_id(kSaslCallId);
  return helper_.SendSaslMessage(&sock_, header, msg, deadline_);
}

Status SaslServer::SendSaslError(ErrorStatusPB::RpcErrorCodePB code, const Status& err) {
  DCHECK_NE(server_state_, SaslNegotiationState::NEW)
      << "Must not send SASL messages before calling Init()";
  DCHECK_NE(server_state_, SaslNegotiationState::NEGOTIATED)
      << "Must not send SASL messages after Negotiate() succeeds";
  if (err.ok()) {
    return Status::InvalidArgument("Cannot send error message using OK status");
  }

  // Create header with SASL-specific callId
  ResponseHeader header;
  header.set_call_id(kSaslCallId);
  header.set_is_error(true);

  // Get RPC error code from Status object
  ErrorStatusPB msg;
  msg.set_code(code);
  msg.set_message(err.ToString());

  RETURN_NOT_OK(helper_.SendSaslMessage(&sock_, header, msg, deadline_));
  TRACE("Sent SASL error: $0", ErrorStatusPB::RpcErrorCodePB_Name(code));
  return Status::OK();
}

Status SaslServer::HandleNegotiateRequest(const SaslMessagePB& request) {
  TRACE("SASL Server: Received NEGOTIATE request from client");

  // Authentication mechanisms this server supports (i.e. plugins).
  set<string> server_mechs = helper_.LocalMechs();
  if (PREDICT_FALSE(server_mechs.empty())) {
    // This will happen if no mechanisms are enabled before calling Init()
    Status s = Status::IllegalState("SASL server mechanism list is empty!");
    LOG(ERROR) << s.ToString();
    TRACE("SASL Server: Sending FATAL_UNAUTHORIZED response to client");
    RETURN_NOT_OK(SendSaslError(ErrorStatusPB::FATAL_UNAUTHORIZED, s));
    return s;
  }

  RETURN_NOT_OK(SendNegotiateResponse(server_mechs));
  return Status::OK();
}

Status SaslServer::SendNegotiateResponse(const set<string>& server_mechs) {
  SaslMessagePB response;
  response.set_state(SaslMessagePB::NEGOTIATE);

  for (const string& mech : server_mechs) {
    SaslMessagePB::SaslAuth* auth = response.add_auths();

    // The 'method' field is deprecated, but older versions of Kudu marked it 'required'.
    // So, we have to set it to something to keep compatibility. At some point, we can
    // consider removing it and breaking compatibility with Kudu <=0.6.
    auth->set_method("");
    auth->set_mechanism(mech);
  }

  RETURN_NOT_OK(SendSaslMessage(response));
  TRACE("Sent NEGOTIATE response");
  return Status::OK();
}


Status SaslServer::HandleInitiateRequest(const SaslMessagePB& request) {
  TRACE("SASL Server: Received INITIATE request from client");

  if (request.auths_size() != 1) {
    Status s = Status::NotAuthorized(StringPrintf(
          "SASL INITIATE request must include exactly one SaslAuth section, found: %d",
          request.auths_size()));
    RETURN_NOT_OK(SendSaslError(ErrorStatusPB::FATAL_UNAUTHORIZED, s));
    return s;
  }

  const SaslMessagePB::SaslAuth& auth = request.auths(0);
  TRACE("SASL Server: Client requested to use mechanism: $0", auth.mechanism());

  // Security issue to display this. Commented out but left for debugging purposes.
  //DVLOG(3) << "SASL server: Client token: " << request.token();

  const char* server_out = nullptr;
  uint32_t server_out_len = 0;
  TRACE("SASL Server: Calling sasl_server_start()");
  int result = sasl_server_start(
      sasl_conn_.get(),         // The SASL connection context created by init()
      auth.mechanism().c_str(), // The mechanism requested by the client.
      request.token().c_str(),  // Optional string the client gave us.
      request.token().length(), // Client string len.
      &server_out,              // The output of the SASL library, might not be NULL terminated
      &server_out_len);         // Output len.

  if (PREDICT_FALSE(result != SASL_OK && result != SASL_CONTINUE)) {
    Status s = Status::NotAuthorized("Unable to negotiate SASL connection",
        SaslErrDesc(result, sasl_conn_.get()));
    RETURN_NOT_OK(SendSaslError(ErrorStatusPB::FATAL_UNAUTHORIZED, s));
    return s;
  }
  negotiated_mech_ = SaslMechanism::value_of(auth.mechanism());

  // We have a valid mechanism match
  if (result == SASL_OK) {
    nego_ok_ = true;
    RETURN_NOT_OK(SendSuccessResponse(server_out, server_out_len));
  } else { // result == SASL_CONTINUE
    RETURN_NOT_OK(SendChallengeResponse(server_out, server_out_len));
  }
  return Status::OK();
}

Status SaslServer::SendChallengeResponse(const char* challenge, unsigned clen) {
  if (clen < 1) {
    Status s = Status::NotAuthorized("SASL library did not provide challenge token!");
    RETURN_NOT_OK(SendSaslError(ErrorStatusPB::FATAL_UNAUTHORIZED, s));
    return s;
  }

  SaslMessagePB response;
  response.set_state(SaslMessagePB::CHALLENGE);
  response.mutable_token()->assign(challenge, clen);
  TRACE("SASL Server: Sending CHALLENGE response to client");
  RETURN_NOT_OK(SendSaslMessage(response));
  return Status::OK();
}

Status SaslServer::SendSuccessResponse(const char* token, unsigned tlen) {
  SaslMessagePB response;
  response.set_state(SaslMessagePB::SUCCESS);
  if (PREDICT_FALSE(tlen > 0)) {
    response.mutable_token()->assign(token, tlen);
  }
  TRACE("SASL Server: Sending SUCCESS response to client");
  RETURN_NOT_OK(SendSaslMessage(response));
  return Status::OK();
}


Status SaslServer::HandleResponseRequest(const SaslMessagePB& request) {
  TRACE("SASL Server: Received RESPONSE request from client");

  if (!request.has_token()) {
    Status s = Status::InvalidArgument("No token in CHALLENGE RESPONSE from client");
    RETURN_NOT_OK(SendSaslError(ErrorStatusPB::FATAL_UNAUTHORIZED, s));
    return s;
  }

  const char* server_out = nullptr;
  uint32_t server_out_len = 0;
  TRACE("SASL Server: Calling sasl_server_step()");
  int result = sasl_server_step(
      sasl_conn_.get(),         // The SASL connection context created by init()
      request.token().c_str(),  // Optional string the client gave us
      request.token().length(), // Client string len
      &server_out,              // The output of the SASL library, might not be NULL terminated
      &server_out_len);         // Output len

  if (result != SASL_OK && result != SASL_CONTINUE) {
    Status s = Status::NotAuthorized("Unable to negotiate SASL connection",
        SaslErrDesc(result, sasl_conn_.get()));
    RETURN_NOT_OK(SendSaslError(ErrorStatusPB::FATAL_UNAUTHORIZED, s));
    return s;
  }

  SaslMessagePB msg;
  if (result == SASL_OK) {
    nego_ok_ = true;
    RETURN_NOT_OK(SendSuccessResponse(server_out, server_out_len));
  } else { // result == SASL_CONTINUE
    RETURN_NOT_OK(SendChallengeResponse(server_out, server_out_len));
  }
  return Status::OK();
}

int SaslServer::GetOptionCb(const char* plugin_name, const char* option,
                            const char** result, unsigned* len) {
  return helper_.GetOptionCb(plugin_name, option, result, len);
}

int SaslServer::PlainAuthCb(sasl_conn_t *conn, const char *user, const char *pass,
                            unsigned passlen, struct propctx *propctx) {
  TRACE("SASL Server: Checking PLAIN auth credentials");
  if (PREDICT_FALSE(!helper_.IsPlainEnabled())) {
    LOG(DFATAL) << "Password authentication callback called while PLAIN auth disabled";
    return SASL_BADPARAM;
  }
  if (PREDICT_FALSE(!authstore_)) {
    LOG(DFATAL) << "AuthStore not initialized";
    return SASL_FAIL;
  }
  Status s = authstore_->Authenticate(user, string(pass, passlen));
  TRACE("SASL Server: PLAIN user authentication status: $0", s.ToString());
  if (!s.ok()) {
    LOG(INFO) << "Failed login for user: " << user;
    return SASL_FAIL;
  }
  plain_auth_user_ = user; // Store username of authenticated user.
  return SASL_OK;
}

} // namespace rpc
} // namespace kudu

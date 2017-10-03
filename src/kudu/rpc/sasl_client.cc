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

#include "kudu/rpc/sasl_client.h"

#include <string.h>

#include <map>
#include <set>
#include <string>

#include <glog/logging.h>
#include <sasl/sasl.h>

#include "kudu/gutil/endian.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/rpc/blocking_ops.h"
#include "kudu/rpc/constants.h"
#include "kudu/rpc/rpc_header.pb.h"
#include "kudu/rpc/sasl_common.h"
#include "kudu/rpc/sasl_helper.h"
#include "kudu/rpc/serialization.h"
#include "kudu/util/faststring.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/net/socket.h"
#include "kudu/util/trace.h"

namespace kudu {
namespace rpc {

using std::map;
using std::set;
using std::string;

static int SaslClientGetoptCb(void* sasl_client, const char* plugin_name, const char* option,
                       const char** result, unsigned* len) {
  return static_cast<SaslClient*>(sasl_client)
    ->GetOptionCb(plugin_name, option, result, len);
}

static int SaslClientSimpleCb(void *sasl_client, int id,
                       const char **result, unsigned *len) {
  return static_cast<SaslClient*>(sasl_client)->SimpleCb(id, result, len);
}

static int SaslClientSecretCb(sasl_conn_t* conn, void *sasl_client, int id,
                       sasl_secret_t** psecret) {
  return static_cast<SaslClient*>(sasl_client)->SecretCb(conn, id, psecret);
}

// Return an appropriately-typed Status object based on an ErrorStatusPB returned
// from an Error RPC.
// In case there is no relevant Status type, return a RuntimeError.
static Status StatusFromRpcError(const ErrorStatusPB& error) {
  DCHECK(error.IsInitialized()) << "Error status PB must be initialized";
  if (PREDICT_FALSE(!error.has_code())) {
    return Status::RuntimeError(error.message());
  }
  string code_name = ErrorStatusPB::RpcErrorCodePB_Name(error.code());
  switch (error.code()) {
    case ErrorStatusPB_RpcErrorCodePB_FATAL_UNAUTHORIZED:
      return Status::NotAuthorized(code_name, error.message());
    default:
      return Status::RuntimeError(code_name, error.message());
  }
}

SaslClient::SaslClient(string app_name, int fd)
    : app_name_(std::move(app_name)),
      sock_(fd),
      helper_(SaslHelper::CLIENT),
      client_state_(SaslNegotiationState::NEW),
      negotiated_mech_(SaslMechanism::INVALID),
      deadline_(MonoTime::Max()) {
  callbacks_.push_back(SaslBuildCallback(SASL_CB_GETOPT,
      reinterpret_cast<int (*)()>(&SaslClientGetoptCb), this));
  callbacks_.push_back(SaslBuildCallback(SASL_CB_AUTHNAME,
      reinterpret_cast<int (*)()>(&SaslClientSimpleCb), this));
  callbacks_.push_back(SaslBuildCallback(SASL_CB_PASS,
      reinterpret_cast<int (*)()>(&SaslClientSecretCb), this));
  callbacks_.push_back(SaslBuildCallback(SASL_CB_LIST_END, nullptr, nullptr));
}

SaslClient::~SaslClient() {
  sock_.Release();  // Do not close the underlying socket when this object is destroyed.
}

Status SaslClient::EnableAnonymous() {
  DCHECK_EQ(client_state_, SaslNegotiationState::INITIALIZED);
  return helper_.EnableAnonymous();
}

Status SaslClient::EnablePlain(const string& user, const string& pass) {
  DCHECK_EQ(client_state_, SaslNegotiationState::INITIALIZED);
  RETURN_NOT_OK(helper_.EnablePlain());
  plain_auth_user_ = user;
  plain_pass_ = pass;
  return Status::OK();
}

SaslMechanism::Type SaslClient::negotiated_mechanism() const {
  DCHECK_EQ(client_state_, SaslNegotiationState::NEGOTIATED);
  return negotiated_mech_;
}

void SaslClient::set_local_addr(const Sockaddr& addr) {
  DCHECK_EQ(client_state_, SaslNegotiationState::NEW);
  helper_.set_local_addr(addr);
}

void SaslClient::set_remote_addr(const Sockaddr& addr) {
  DCHECK_EQ(client_state_, SaslNegotiationState::NEW);
  helper_.set_remote_addr(addr);
}

void SaslClient::set_server_fqdn(const string& domain_name) {
  DCHECK_EQ(client_state_, SaslNegotiationState::NEW);
  helper_.set_server_fqdn(domain_name);
}

void SaslClient::set_deadline(const MonoTime& deadline) {
  DCHECK_NE(client_state_, SaslNegotiationState::NEGOTIATED);
  deadline_ = deadline;
}

// calls sasl_client_init() and sasl_client_new()
Status SaslClient::Init(const string& service_type) {
  RETURN_NOT_OK(SaslInit(app_name_.c_str()));

  // Ensure we are not called more than once.
  if (client_state_ != SaslNegotiationState::NEW) {
    return Status::IllegalState("Init() may only be called once per SaslClient object.");
  }

  // TODO: Support security flags.
  unsigned secflags = 0;

  sasl_conn_t* sasl_conn = nullptr;
  int result = sasl_client_new(
      service_type.c_str(),         // Registered name of the service using SASL. Required.
      helper_.server_fqdn(),        // The fully qualified domain name of the remote server.
      helper_.local_addr_string(),  // Local and remote IP address strings. (NULL disables
      helper_.remote_addr_string(), //   mechanisms which require this info.)
      &callbacks_[0],               // Connection-specific callbacks.
      secflags,                     // Security flags.
      &sasl_conn);

  if (PREDICT_FALSE(result != SASL_OK)) {
    return Status::RuntimeError("Unable to create new SASL client",
        SaslErrDesc(result, sasl_conn));
  }
  sasl_conn_.reset(sasl_conn);

  client_state_ = SaslNegotiationState::INITIALIZED;
  return Status::OK();
}

Status SaslClient::Negotiate() {
  TRACE("Called SaslClient::Negotiate()");

  // Ensure we called exactly once, and in the right order.
  if (client_state_ == SaslNegotiationState::NEW) {
    return Status::IllegalState("SaslClient: Init() must be called before calling Negotiate()");
  } else if (client_state_ == SaslNegotiationState::NEGOTIATED) {
    return Status::IllegalState("SaslClient: Negotiate() may only be called once per object.");
  }

  // Ensure we can use blocking calls on the socket during negotiation.
  RETURN_NOT_OK(EnsureBlockingMode(&sock_));

  // Start by asking the server for a list of available auth mechanisms.
  RETURN_NOT_OK(SendNegotiateMessage());

  faststring recv_buf;
  nego_ok_ = false;

  // We set nego_ok_ = true when the SASL library returns SASL_OK to us.
  // We set nego_response_expected_ = true each time we send a request to the server.
  // When using ANONYMOUS, we get SASL_OK back immediately but still send INITIATE to the server.
  while (!nego_ok_ || nego_response_expected_) {
    ResponseHeader header;
    Slice param_buf;
    RETURN_NOT_OK(ReceiveFramedMessageBlocking(&sock_, &recv_buf, &header, &param_buf, deadline_));
    nego_response_expected_ = false;

    SaslMessagePB response;
    RETURN_NOT_OK(ParseSaslMsgResponse(header, param_buf, &response));

    switch (response.state()) {
      // NEGOTIATE: Server has sent us its list of supported SASL mechanisms.
      case SaslMessagePB::NEGOTIATE:
        RETURN_NOT_OK(HandleNegotiateResponse(response));
        break;

      // CHALLENGE: Server sent us a follow-up to an INITIATE or RESPONSE request.
      case SaslMessagePB::CHALLENGE:
        RETURN_NOT_OK(HandleChallengeResponse(response));
        break;

      // SUCCESS: Server has accepted our authentication request. Negotiation successful.
      case SaslMessagePB::SUCCESS:
        RETURN_NOT_OK(HandleSuccessResponse(response));
        break;

      // Client sent us some unsupported SASL response.
      default:
        LOG(ERROR) << "SASL Client: Received unsupported response from server";
        return Status::InvalidArgument("RPC client doesn't support SASL state in response",
            SaslMessagePB::SaslState_Name(response.state()));
    }
  }

  TRACE("SASL Client: Successful negotiation");
  client_state_ = SaslNegotiationState::NEGOTIATED;
  return Status::OK();
}

Status SaslClient::SendSaslMessage(const SaslMessagePB& msg) {
  DCHECK_NE(client_state_, SaslNegotiationState::NEW)
      << "Must not send SASL messages before calling Init()";
  DCHECK_NE(client_state_, SaslNegotiationState::NEGOTIATED)
      << "Must not send SASL messages after Negotiate() succeeds";

  // Create header with SASL-specific callId
  RequestHeader header;
  header.set_call_id(kSaslCallId);
  return helper_.SendSaslMessage(&sock_, header, msg, deadline_);
}

Status SaslClient::ParseSaslMsgResponse(const ResponseHeader& header, const Slice& param_buf,
    SaslMessagePB* response) {
  RETURN_NOT_OK(helper_.SanityCheckSaslCallId(header.call_id()));

  if (header.is_error()) {
    return ParseError(param_buf);
  }

  return helper_.ParseSaslMessage(param_buf, response);
}

Status SaslClient::SendNegotiateMessage() {
  SaslMessagePB msg;
  msg.set_state(SaslMessagePB::NEGOTIATE);
  TRACE("SASL Client: Sending NEGOTIATE request to server.");
  RETURN_NOT_OK(SendSaslMessage(msg));
  nego_response_expected_ = true;
  return Status::OK();
}

Status SaslClient::SendInitiateMessage(const SaslMessagePB_SaslAuth& auth,
    const char* init_msg, unsigned init_msg_len) {
  SaslMessagePB msg;
  msg.set_state(SaslMessagePB::INITIATE);
  msg.mutable_token()->assign(init_msg, init_msg_len);
  msg.add_auths()->CopyFrom(auth);
  TRACE("SASL Client: Sending INITIATE request to server.");
  RETURN_NOT_OK(SendSaslMessage(msg));
  nego_response_expected_ = true;
  return Status::OK();
}

Status SaslClient::SendResponseMessage(const char* resp_msg, unsigned resp_msg_len) {
  SaslMessagePB reply;
  reply.set_state(SaslMessagePB::RESPONSE);
  reply.mutable_token()->assign(resp_msg, resp_msg_len);
  TRACE("SASL Client: Sending RESPONSE request to server.");
  RETURN_NOT_OK(SendSaslMessage(reply));
  nego_response_expected_ = true;
  return Status::OK();
}

Status SaslClient::DoSaslStep(const string& in, const char** out, unsigned* out_len, int* result) {
  TRACE("SASL Client: Calling sasl_client_step()");
  int res = sasl_client_step(sasl_conn_.get(), in.c_str(), in.length(), nullptr, out, out_len);
  *result = res;
  if (res == SASL_OK) {
    nego_ok_ = true;
  }
  if (PREDICT_FALSE(res != SASL_OK && res != SASL_CONTINUE)) {
    return Status::NotAuthorized("Unable to negotiate SASL connection",
        SaslErrDesc(res, sasl_conn_.get()));
  }
  return Status::OK();
}

Status SaslClient::HandleNegotiateResponse(const SaslMessagePB& response) {
  TRACE("SASL Client: Received NEGOTIATE response from server");
  map<string, SaslMessagePB::SaslAuth> mech_auth_map;

  string mech_list;
  mech_list.reserve(64);  // Avoid resizing the buffer later.
  for (const SaslMessagePB::SaslAuth& auth : response.auths()) {
    if (mech_list.length() > 0) mech_list.append(" ");
    string mech = auth.mechanism();
    mech_list.append(mech);
    mech_auth_map[mech] = auth;
  }
  TRACE("SASL Client: Server mech list: $0", mech_list);

  const char* init_msg = nullptr;
  unsigned init_msg_len = 0;
  const char* negotiated_mech = nullptr;

  /* select a mechanism for a connection
   *  mechlist      -- mechanisms server has available (punctuation ignored)
   * output:
   *  prompt_need   -- on SASL_INTERACT, list of prompts needed to continue
   *  clientout     -- the initial client response to send to the server
   *  mech          -- set to mechanism name
   *
   * Returns:
   *  SASL_OK       -- success
   *  SASL_CONTINUE -- negotiation required
   *  SASL_NOMEM    -- not enough memory
   *  SASL_NOMECH   -- no mechanism meets requested properties
   *  SASL_INTERACT -- user interaction needed to fill in prompt_need list
   */
  TRACE("SASL Client: Calling sasl_client_start()");
  int result = sasl_client_start(
      sasl_conn_.get(),     // The SASL connection context created by init()
      mech_list.c_str(),    // The list of mechanisms from the server.
      nullptr,              // Disables INTERACT return if NULL.
      &init_msg,            // Filled in on success.
      &init_msg_len,        // Filled in on success.
      &negotiated_mech);    // Filled in on success.

  if (PREDICT_FALSE(result == SASL_OK)) {
    nego_ok_ = true;
  } else if (PREDICT_FALSE(result != SASL_CONTINUE)) {
    return Status::NotAuthorized("Unable to negotiate SASL connection",
        SaslErrDesc(result, sasl_conn_.get()));
  }

  // The server matched one of our mechanisms.
  SaslMessagePB::SaslAuth* auth = FindOrNull(mech_auth_map, negotiated_mech);
  if (PREDICT_FALSE(auth == nullptr)) {
    return Status::IllegalState("Unable to find auth in map, unexpected error", negotiated_mech);
  }
  negotiated_mech_ = SaslMechanism::value_of(negotiated_mech);

  // Handle the case where the server sent a challenge with the NEGOTIATE response.
  if (auth->has_challenge()) {
    if (PREDICT_FALSE(nego_ok_)) {
      LOG(DFATAL) << "Server sent challenge after sasl_client_start() returned SASL_OK";
    }
    RETURN_NOT_OK(DoSaslStep(auth->challenge(), &init_msg, &init_msg_len, &result));
  }

  RETURN_NOT_OK(SendInitiateMessage(*auth, init_msg, init_msg_len));
  return Status::OK();
}

Status SaslClient::HandleChallengeResponse(const SaslMessagePB& response) {
  TRACE("SASL Client: Received CHALLENGE response from server");
  if (PREDICT_FALSE(nego_ok_)) {
    LOG(DFATAL) << "Server sent CHALLENGE response after client library returned SASL_OK";
  }

  if (PREDICT_FALSE(!response.has_token())) {
    return Status::InvalidArgument("No token in CHALLENGE response from server");
  }

  const char* out = nullptr;
  unsigned out_len = 0;
  int result = 0;
  RETURN_NOT_OK(DoSaslStep(response.token(), &out, &out_len, &result));

  RETURN_NOT_OK(SendResponseMessage(out, out_len));
  return Status::OK();
}

Status SaslClient::HandleSuccessResponse(const SaslMessagePB& response) {
  TRACE("SASL Client: Received SUCCESS response from server");
  if (!nego_ok_) {
    const char* out = nullptr;
    unsigned out_len = 0;
    int result = 0;
    RETURN_NOT_OK(DoSaslStep(response.token(), &out, &out_len, &result));
    if (out_len > 0) {
      return Status::IllegalState("SASL client library generated spurious token after SUCCESS",
          string(out, out_len));
    }
    if (PREDICT_FALSE(result != SASL_OK)) {
      return Status::NotAuthorized("Unable to negotiate SASL connection",
          SaslErrDesc(result, sasl_conn_.get()));
    }
  }
  nego_ok_ = true;
  return Status::OK();
}

// Parse error status message from raw bytes of an ErrorStatusPB.
Status SaslClient::ParseError(const Slice& err_data) {
  ErrorStatusPB error;
  if (!error.ParseFromArray(err_data.data(), err_data.size())) {
    return Status::IOError("Invalid error response, missing fields",
        error.InitializationErrorString());
  }
  Status s = StatusFromRpcError(error);
  TRACE("SASL Client: Received error response from server: $0", s.ToString());
  return s;
}

int SaslClient::GetOptionCb(const char* plugin_name, const char* option,
                            const char** result, unsigned* len) {
  return helper_.GetOptionCb(plugin_name, option, result, len);
}

// Used for PLAIN and ANONYMOUS.
// SASL callback for SASL_CB_USER, SASL_CB_AUTHNAME, SASL_CB_LANGUAGE
int SaslClient::SimpleCb(int id, const char** result, unsigned* len) {
  if (PREDICT_FALSE(result == nullptr)) {
    LOG(DFATAL) << "SASL Client: result outparam is NULL";
    return SASL_BADPARAM;
  }
  switch (id) {
    // TODO: Support impersonation?
    // For impersonation, USER is the impersonated user, AUTHNAME is the "sudoer".
    case SASL_CB_USER:
      TRACE("SASL Client: callback for SASL_CB_USER");
      if (helper_.IsPlainEnabled()) {
        *result = plain_auth_user_.c_str();
        if (len != nullptr) *len = plain_auth_user_.length();
      } else if (helper_.IsAnonymousEnabled()) {
        *result = nullptr;
      }
      break;
    case SASL_CB_AUTHNAME:
      TRACE("SASL Client: callback for SASL_CB_AUTHNAME");
      if (helper_.IsPlainEnabled()) {
        *result = plain_auth_user_.c_str();
        if (len != nullptr) *len = plain_auth_user_.length();
      }
      break;
    case SASL_CB_LANGUAGE:
      LOG(DFATAL) << "SASL Client: Unable to handle SASL callback type SASL_CB_LANGUAGE"
        << "(" << id << ")";
      return SASL_BADPARAM;
    default:
      LOG(DFATAL) << "SASL Client: Unexpected SASL callback type: " << id;
      return SASL_BADPARAM;
  }

  return SASL_OK;
}

// Used for PLAIN.
// SASL callback for SASL_CB_PASS: User password.
int SaslClient::SecretCb(sasl_conn_t* conn, int id, sasl_secret_t** psecret) {
  if (PREDICT_FALSE(!helper_.IsPlainEnabled())) {
    LOG(DFATAL) << "SASL Client: Plain secret callback called, but PLAIN auth is not enabled";
    return SASL_FAIL;
  }
  switch (id) {
    case SASL_CB_PASS: {
      if (!conn || !psecret) return SASL_BADPARAM;

      int len = plain_pass_.length();
      *psecret = reinterpret_cast<sasl_secret_t*>(malloc(sizeof(sasl_secret_t) + len));
      if (!*psecret) {
        return SASL_NOMEM;
      }
      psecret_.reset(*psecret);  // Ensure that we free() this structure later.
      (*psecret)->len = len;
      memcpy(reinterpret_cast<char *>((*psecret)->data), plain_pass_.c_str(), len + 1);
      break;
    }
    default:
      LOG(DFATAL) << "SASL Client: Unexpected SASL callback type: " << id;
      return SASL_BADPARAM;
  }

  return SASL_OK;
}

} // namespace rpc
} // namespace kudu

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

#ifndef KUDU_RPC_SASL_CLIENT_H
#define KUDU_RPC_SASL_CLIENT_H

#include <set>
#include <string>
#include <vector>

#include <sasl/sasl.h>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/rpc/sasl_common.h"
#include "kudu/rpc/sasl_helper.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"
#include "kudu/util/net/socket.h"

namespace kudu {
namespace rpc {

using std::string;

class ResponseHeader;
class SaslMessagePB;
class SaslMessagePB_SaslAuth;

// Class for doing SASL negotiation with a SaslServer over a bidirectional socket.
// Operations on this class are NOT thread-safe.
class SaslClient {
 public:
  // Does not take ownership of the socket indicated by the fd.
  SaslClient(string app_name, int fd);
  ~SaslClient();

  // Enable ANONYMOUS authentication.
  // Call after Init().
  Status EnableAnonymous();

  // Enable PLAIN authentication.
  // Call after Init().
  Status EnablePlain(const string& user, const string& pass);

  // Returns mechanism negotiated by this connection.
  // Call after Negotiate().
  SaslMechanism::Type negotiated_mechanism() const;

  // Specify IP:port of local side of connection.
  // Call before Init(). Required for some mechanisms.
  void set_local_addr(const Sockaddr& addr);

  // Specify IP:port of remote side of connection.
  // Call before Init(). Required for some mechanisms.
  void set_remote_addr(const Sockaddr& addr);

  // Specify the fully-qualified domain name of the remote server.
  // Call before Init(). Required for some mechanisms.
  void set_server_fqdn(const string& domain_name);

  // Set deadline for connection negotiation.
  void set_deadline(const MonoTime& deadline);

  // Get deadline for connection negotiation.
  const MonoTime& deadline() const { return deadline_; }

  // Initialize a new SASL client. Must be called before Negotiate().
  // Returns OK on success, otherwise RuntimeError.
  Status Init(const string& service_type);

  // Begin negotiation with the SASL server on the other side of the fd socket
  // that this client was constructed with.
  // Returns OK on success.
  // Otherwise, it may return NotAuthorized, NotSupported, or another non-OK status.
  Status Negotiate();

  // SASL callback for plugin options, supported mechanisms, etc.
  // Returns SASL_FAIL if the option is not handled, which does not fail the handshake.
  int GetOptionCb(const char* plugin_name, const char* option,
                  const char** result, unsigned* len);

  // SASL callback for SASL_CB_USER, SASL_CB_AUTHNAME, SASL_CB_LANGUAGE
  int SimpleCb(int id, const char** result, unsigned* len);

  // SASL callback for SASL_CB_PASS
  int SecretCb(sasl_conn_t* conn, int id, sasl_secret_t** psecret);

 private:
  // Encode and send the specified SASL message to the server.
  Status SendSaslMessage(const SaslMessagePB& msg);

  // Validate that header does not indicate an error, parse param_buf into response.
  Status ParseSaslMsgResponse(const ResponseHeader& header, const Slice& param_buf,
      SaslMessagePB* response);

  // Send an NEGOTIATE message to the server.
  Status SendNegotiateMessage();

  // Send an INITIATE message to the server.
  Status SendInitiateMessage(const SaslMessagePB_SaslAuth& auth,
                             const char* init_msg, unsigned init_msg_len);

  // Send a RESPONSE message to the server.
  Status SendResponseMessage(const char* resp_msg, unsigned resp_msg_len);

  // Perform a client-side step of the SASL negotiation.
  // Input is what came from the server. Output is what we will send back to the server.
  // Return code from sasl_client_step is stored in result.
  // Returns Status::OK if sasl_client_step returns SASL_OK or SASL_CONTINUE; otherwise,
  // returns Status::NotAuthorized.
  Status DoSaslStep(const string& in, const char** out, unsigned* out_len, int* result);

  // Handle case when server sends NEGOTIATE response.
  Status HandleNegotiateResponse(const SaslMessagePB& response);

  // Handle case when server sends CHALLENGE response.
  Status HandleChallengeResponse(const SaslMessagePB& response);

  // Handle case when server sends SUCCESS response.
  Status HandleSuccessResponse(const SaslMessagePB& response);

  // Parse error status message from raw bytes of an ErrorStatusPB.
  Status ParseError(const Slice& err_data);

  string app_name_;
  Socket sock_;
  std::vector<sasl_callback_t> callbacks_;
  gscoped_ptr<sasl_conn_t, SaslDeleter> sasl_conn_;
  SaslHelper helper_;

  string plain_auth_user_;
  string plain_pass_;
  gscoped_ptr<sasl_secret_t, FreeDeleter> psecret_;

  SaslNegotiationState::Type client_state_;

  // The mechanism we negotiated with the server.
  SaslMechanism::Type negotiated_mech_;

  // Intra-negotiation state.
  bool nego_ok_;  // During negotiation: did we get a SASL_OK response from the SASL library?
  bool nego_response_expected_;  // During negotiation: Are we waiting for a server response?

  // Negotiation timeout deadline.
  MonoTime deadline_;

  DISALLOW_COPY_AND_ASSIGN(SaslClient);
};

} // namespace rpc
} // namespace kudu

#endif  // KUDU_RPC_SASL_CLIENT_H

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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#ifndef YB_RPC_SASL_HELPER_H
#define YB_RPC_SASL_HELPER_H

#include <sasl/sasl.h>

#include <set>
#include <string>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/util/net/socket.h"

namespace google {
namespace protobuf {
class MessageLite;
} // namespace protobuf
} // namespace google

namespace yb {

class MonoTime;
class Status;

namespace rpc {

using std::string;

class SaslMessagePB;

// Helper class which contains functionality that is common to SaslClient & SaslServer.
// Most of these methods are convenience methods for interacting with the libsasl2 library.
class SaslHelper {
 public:
  enum PeerType {
    CLIENT,
    SERVER
  };

  explicit SaslHelper(PeerType peer_type);
  ~SaslHelper();

  // Specify IP:port of local side of connection.
  void set_local_addr(const Endpoint& addr);
  const char* local_addr_string() const;

  // Specify IP:port of remote side of connection.
  void set_remote_addr(const Endpoint& addr);
  const char* remote_addr_string() const;

  // Specify the fully-qualified domain name of the remote server.
  void set_server_fqdn(const string& domain_name);
  const char* server_fqdn() const;

  // Globally-registered available SASL plugins.
  const std::set<string>& GlobalMechs() const;

  // Helper functions for managing the list of active SASL mechanisms.
  void AddToLocalMechList(const string& mech);
  const std::set<string>& LocalMechs() const;

  // Returns space-delimited local mechanism list string suitable for passing
  // to libsasl2, such as via "mech_list" callbacks.
  // The returned pointer is valid only until the next call to LocalMechListString().
  const char* LocalMechListString() const;

  // Implements the client_mech_list / mech_list callbacks.
  int GetOptionCb(const char* plugin_name, const char* option, const char** result, unsigned* len);

  // Enable the ANONYMOUS SASL mechanism.
  CHECKED_STATUS EnableAnonymous();

  // Check for the ANONYMOUS SASL mechanism.
  bool IsAnonymousEnabled() const;

  // Enable the PLAIN SASL mechanism.
  CHECKED_STATUS EnablePlain();

  // Check for the PLAIN SASL mechanism.
  bool IsPlainEnabled() const;

  // Sanity check that the call ID is the SASL call ID.
  // Logs DFATAL if call_id does not match.
  CHECKED_STATUS SanityCheckSaslCallId(int32_t call_id) const;

  // Parse msg from the given Slice.
  CHECKED_STATUS ParseSaslMessage(const Slice& param_buf, SaslMessagePB* msg);

  // Encode and send a message over a socket, sending the connection header if necessary.
  CHECKED_STATUS SendSaslMessage(Socket* sock, const google::protobuf::MessageLite& header,
      const google::protobuf::MessageLite& msg, const MonoTime& deadline);

 private:
  string local_addr_;
  string remote_addr_;
  string server_fqdn_;

  // Authentication types and data.
  const PeerType peer_type_;
  bool conn_header_exchanged_;
  string tag_;
  mutable gscoped_ptr< std::set<string> > global_mechs_;  // Cache of global mechanisms.
  std::set<string> mechs_;    // Active mechanisms.
  mutable string mech_list_;  // Mechanism list string returned by callbacks.

  bool anonymous_enabled_;
  bool plain_enabled_;

  DISALLOW_COPY_AND_ASSIGN(SaslHelper);
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_SASL_HELPER_H

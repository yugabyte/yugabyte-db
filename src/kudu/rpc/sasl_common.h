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

#ifndef KUDU_RPC_SASL_COMMON_H
#define KUDU_RPC_SASL_COMMON_H

#include <stdint.h> // Required for sasl/sasl.h

#include <string>
#include <set>

#include <sasl/sasl.h>

#include "kudu/util/status.h"

namespace kudu {

class Sockaddr;

namespace rpc {

using std::string;

// Constants
extern const char* const kSaslMechAnonymous;
extern const char* const kSaslMechPlain;

// Initialize the SASL library.
// appname: Name of the application for logging messages & sasl plugin configuration.
//          Note that this string must remain allocated for the lifetime of the program.
// This function must be called before using SASL.
// If the library initializes without error, calling more than once has no effect.
//
// Some SASL plugins take time to initialize random number generators and other things,
// so the first time this function is invoked it may execute for several seconds.
// After that, it should be very fast. This function should be invoked as early as possible
// in the application lifetime to avoid SASL initialization taking place in a
// performance-critical section.
//
// This function is thread safe and uses a static lock.
// This function should NOT be called during static initialization.
Status SaslInit(const char* const app_name);

// Return a string describing the SASL error response.
string SaslErrDesc(int status, sasl_conn_t* conn);

// Return <ip>;<port> string formatted for SASL library use.
string SaslIpPortString(const Sockaddr& addr);

// Return available plugin mechanisms for the given connection.
std::set<string> SaslListAvailableMechs();

// Initialize and return a libsasl2 callback data structure based on the passed args.
// id: A SASL callback identifier (e.g., SASL_CB_GETOPT).
// proc: A C-style callback with appropriate signature based on the callback id, or NULL.
// context: An object to pass to the callback as the context pointer, or NULL.
sasl_callback_t SaslBuildCallback(int id, int (*proc)(void), void* context);

// Deleter for sasl_conn_t instances, for use with gscoped_ptr after calling sasl_*_new()
struct SaslDeleter {
  inline void operator()(sasl_conn_t* conn) {
    sasl_dispose(&conn);
  }
};

struct SaslNegotiationState {
  enum Type {
    NEW,
    INITIALIZED,
    NEGOTIATED
  };
};

struct SaslMechanism {
  enum Type {
    INVALID,
    ANONYMOUS,
    PLAIN
  };
  static Type value_of(const std::string& mech);
};

} // namespace rpc
} // namespace kudu

#endif

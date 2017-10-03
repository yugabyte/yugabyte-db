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

#include "kudu/rpc/sasl_common.h"

#include <string>
#include <boost/algorithm/string/predicate.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <sasl/sasl.h>

#include "kudu/gutil/macros.h"
#include "kudu/gutil/once.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/net/sockaddr.h"

using std::set;

namespace kudu {
namespace rpc {

const char* const kSaslMechAnonymous = "ANONYMOUS";
const char* const kSaslMechPlain = "PLAIN";

// Output Sasl messages.
// context: not used.
// level: logging level.
// message: message to output;
static int SaslLogCallback(void* context, int level, const char* message) {

  if (message == nullptr) return SASL_BADPARAM;

  switch (level) {
    case SASL_LOG_NONE:
      break;

    case SASL_LOG_ERR:
    case SASL_LOG_FAIL:
      LOG(ERROR) << "SASL: " << message;
      break;

    case SASL_LOG_WARN:
      LOG(WARNING) << "SASL: " << message;
      break;

    case SASL_LOG_NOTE:
      LOG(INFO) << "SASL: " << message;
      break;

    case SASL_LOG_DEBUG:
      VLOG(1) << "SASL: " << message;
      break;

    case SASL_LOG_TRACE:
    case SASL_LOG_PASS:
      VLOG(3) << "SASL: " << message;
      break;
  }

  return SASL_OK;
}

// Get Sasl option.
// context: not used
// plugin_name: name of plugin for which an option is being requested.
// option: option requested
// result: set to result which persists until next getopt in same thread,
//         unchanged if option not found
// len: length of the result
// Return SASL_FAIL if the option is not handled, this does not fail the handshake.
static int SaslGetOption(void* context, const char* plugin_name, const char* option,
                         const char** result, unsigned* len) {
  // Handle Sasl Library options
  if (plugin_name == nullptr) {
    // Return the logging level that we want the sasl library to use.
    if (strcmp("log_level", option) == 0) {
      int level = SASL_LOG_NOTE;
      if (VLOG_IS_ON(1)) {
        level = SASL_LOG_DEBUG;
      } else if (VLOG_IS_ON(3)) {
        level = SASL_LOG_TRACE;
      }
      // The library's contract for this method is that the caller gets to keep
      // the returned buffer until the next call by the same thread, so we use a
      // threadlocal for the buffer.
      static __thread char buf[4];
      snprintf(buf, arraysize(buf), "%d", level);
      *result = buf;
      if (len != nullptr) *len = strlen(buf);
      return SASL_OK;
    }
    // Options can default so don't complain.
    VLOG(4) << "SaslGetOption: Unknown library option: " << option;
    return SASL_FAIL;
  }
  VLOG(4) << "SaslGetOption: Unknown plugin: " << plugin_name;
  return SASL_FAIL;
}

// Array of callbacks for the sasl library.
static sasl_callback_t callbacks[] = {
  { SASL_CB_LOG, reinterpret_cast<int (*)()>(&SaslLogCallback), nullptr },
  { SASL_CB_GETOPT, reinterpret_cast<int (*)()>(&SaslGetOption), nullptr },
  { SASL_CB_LIST_END, nullptr, nullptr }
};

// Determine whether initialization was ever called
struct InitializationData {
  Status status;
  string app_name;
};
static struct InitializationData* sasl_init_data;

// Actually perform the initialization for the SASL subsystem.
// Meant to be called via GoogleOnceInitArg().
static void DoSaslInit(void* app_name_char_array) {
  // Explicitly cast from void* here so GoogleOnce doesn't have to deal with it.
  // We were getting Clang 3.4 UBSAN errors when letting GoogleOnce cast.
  const char* const app_name = reinterpret_cast<const char* const>(app_name_char_array);
  VLOG(3) << "Initializing SASL library";

  sasl_init_data = new InitializationData();
  sasl_init_data->app_name = app_name;

  int result = sasl_client_init(&callbacks[0]);
  if (result != SASL_OK) {
    sasl_init_data->status = Status::RuntimeError("Could not initialize SASL client",
        sasl_errstring(result, nullptr, nullptr));
    return;
  }

  result = sasl_server_init(&callbacks[0], sasl_init_data->app_name.c_str());
  if (result != SASL_OK) {
    sasl_init_data->status = Status::RuntimeError("Could not initialize SASL server",
        sasl_errstring(result, nullptr, nullptr));
    return;
  }

  sasl_init_data->status = Status::OK();
}

// Only execute SASL initialization once
static GoogleOnceType once = GOOGLE_ONCE_INIT;

Status SaslInit(const char* const app_name) {
  GoogleOnceInitArg(&once,
                    &DoSaslInit,
                    // This is a bit ugly, but Clang 3.4 UBSAN complains otherwise.
                    reinterpret_cast<void*>(const_cast<char*>(app_name)));
  if (PREDICT_FALSE(sasl_init_data->app_name != app_name)) {
    return Status::InvalidArgument("SaslInit called successively with different arguments",
        StringPrintf("Previous: %s, current: %s", sasl_init_data->app_name.c_str(), app_name));
  }
  return sasl_init_data->status;
}

string SaslErrDesc(int status, sasl_conn_t* conn) {
  if (conn != nullptr) {
    return StringPrintf("SASL result code: %s, error: %s",
        sasl_errstring(status, nullptr, nullptr),
        sasl_errdetail(conn));
  }
  return StringPrintf("SASL result code: %s", sasl_errstring(status, nullptr, nullptr));
}

string SaslIpPortString(const Sockaddr& addr) {
  string addr_str = addr.ToString();
  size_t colon_pos = addr_str.find(':');
  if (colon_pos != string::npos) {
    addr_str[colon_pos] = ';';
  }
  return addr_str;
}

set<string> SaslListAvailableMechs() {
  set<string> mechs;

  // Array of NULL-terminated strings. Array terminated with NULL.
  const char** mech_strings = sasl_global_listmech();
  while (mech_strings != nullptr && *mech_strings != nullptr) {
    mechs.insert(*mech_strings);
    mech_strings++;
  }
  return mechs;
}

sasl_callback_t SaslBuildCallback(int id, int (*proc)(void), void* context) {
  sasl_callback_t callback;
  callback.id = id;
  callback.proc = proc;
  callback.context = context;
  return callback;
}

SaslMechanism::Type SaslMechanism::value_of(const string& mech) {
  if (boost::iequals(mech, "ANONYMOUS")) {
    return ANONYMOUS;
  } else if (boost::iequals(mech, "PLAIN")) {
    return PLAIN;
  }
  return INVALID;
}

} // namespace rpc
} // namespace kudu

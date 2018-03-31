//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ybpostgres/pg_port.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"

namespace yb {
namespace pgapi {

using strings::Substitute;

void PGPort::Initialize(const StringInfo& startup_packet, const ProtocolVersion proto) {
  // Save protocol version.
  proto_ = proto;

  // Process buffer.
  const char *buf = startup_packet->data.c_str();
  const size_t len = startup_packet->data.size();
  int32_t offset = sizeof(ProtocolVersion);

  if (PG_PROTOCOL_MAJOR(proto) >= 3) {
    /*
     * Scan packet body for name/option pairs.  We can assume any string
     * beginning within the packet body is null-terminated, thanks to
     * zeroing extra byte above.
     */
    while (offset < len) {
      // Get the name of a given option.
      const char *nameptr = buf + offset;
      if (*nameptr == '\0') {
        /* found packet terminator */
        break;
      }

      // Get the value that is associated with the name.
      const int32_t valoffset = offset + strlen(nameptr) + 1;
      if (valoffset >= len) {
        /* missing value, will complain below */
        break;
      }
      const char *valptr = buf + valoffset;

      if (strcmp(nameptr, "database") == 0) {
        database_name_ = valptr;
      } else if (strcmp(nameptr, "user") == 0) {
        user_name_ = valptr;
      } else if (strcmp(nameptr, "options") == 0) {
        cmdline_options_ = valptr;
      } else if (strcmp(nameptr, "replication") == 0) {
        /*
         * Due to backward compatibility concerns the replication
         * parameter is a hybrid beast which allows the value to be
         * either boolean or the string 'database'. The latter
         * connects to a specific database which is e.g. required for
         * logical decoding while.
         */
        LOG(FATAL) << "We don't have background task yet";
      } else {
        /* Assume it's a generic GUC option */
        guc_options_.push_back(nameptr);
        guc_options_.push_back(valptr);
      }

      // Update offset.
      offset = valoffset + strlen(valptr) + 1;
    }

    /*
     * If we didn't find a packet terminator exactly at the end of the
     * given packet length, complain.
     */
    // TODO(neil) Report error to client instead of crashing.
    CHECK_EQ(offset, len - 1) << "Invalid startup packet layout: expected terminator as last byte";

  } else {
    /*
     * Get the parameters from the old-style, fixed-width-fields startup
     * packet as C strings.  The packet destination was cleared first so a
     * short packet has zeros silently added.  We have to be prepared to
     * truncate the pstrdup result for oversize fields, though.
     */
    const StartupPacket *packet = reinterpret_cast<const StartupPacket *>(buf);

    database_name_ = packet->database;
    if (database_name_.length() > sizeof(packet->database)) {
      database_name_.resize(sizeof(packet->database));
    }

    user_name_ = packet->user;
    if (user_name_.length() > sizeof(packet->user)) {
      user_name_.resize(sizeof(packet->user));
    }

    cmdline_options_ = packet->options;
    if (cmdline_options_.length() > sizeof(packet->options)) {
      cmdline_options_.resize(sizeof(packet->options));
    }
  }

#ifdef POSTGRESQL_NOT_USED
  // TODO(neil) Postgresql requires user name, but we might not.  Need to define a clear spec.
  // Check a user name was given.
  if (user_name_.empty()) {
    STATUS(NetworkError, "no PostgreSQL user name specified in startup packet");
  }
#endif

  /*
   * Truncate given database and user names to length of a Postgres name.
   * This avoids lookup failures when overlength names are given.
   */
  if (database_name_.length() >= NAMEDATALEN) {
    database_name_.resize(NAMEDATALEN - 1);
  }
  if (user_name_.length() >= NAMEDATALEN) {
    user_name_.resize(NAMEDATALEN - 1);
  }
}

}  // namespace pgapi
}  // namespace yb

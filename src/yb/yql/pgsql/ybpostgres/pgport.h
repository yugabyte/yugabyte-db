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
#ifndef YB_YQL_PGSQL_YBPOSTGRES_PGPORT_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PGPORT_H_

#include <list>

#include "yb/yql/pgsql/ybpostgres/pgdefs.h"
#include "yb/yql/pgsql/ybpostgres/pg_stringinfo.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqformat.h"

namespace yb {
namespace pgapi {

// This class collect the specifications for a given database server in the startup packet that was
// sent from Postgresql client to proxy server.
//
// NOTE:
// - This class replaces the struct "Port" in Postgresql code. The following is a description
//   in Postgresql code about Port.
// - Class PGPort has a lot less info and is a lot less complicated because most of the work is
//   handled by different modules.
// - We might consider to rename this to "PGDbSpec".
/*
 * This is used by the postmaster in its communication with frontends.  It
 * contains all state information needed during this communication before the
 * backend is run.  The Port structure is kept in malloc'd memory and is
 * still available when a backend is running (see MyProcPort).  The data
 * it points to must also be malloc'd, or else palloc'd in TopMemoryContext,
 * so that it survives into PostgresMain execution!
 *
 * remote_hostname is set if we did a successful reverse lookup of the
 * client's IP address during connection setup.
 * remote_hostname_resolv tracks the state of hostname verification:
 *      +1 = remote_hostname is known to resolve to client's IP address
 *      -1 = remote_hostname is known NOT to resolve to client's IP address
 *       0 = we have not done the forward DNS lookup yet
 *      -2 = there was an error in name resolution
 * If reverse lookup of the client IP address fails, remote_hostname will be
 * left NULL while remote_hostname_resolv is set to -2.  If reverse lookup
 * succeeds but forward lookup fails, remote_hostname_resolv is also set to -2
 * (the case is distinguishable because remote_hostname isn't NULL).  In
 * either of the -2 cases, remote_hostname_errcode saves the lookup return
 * code for possible later use with gai_strerror.
 */
class PGPort {
 public:
  typedef std::shared_ptr<PGPort> SharedPtr;
  PGPort() { }
  virtual ~PGPort() { }

  void Initialize(const StringInfo& startup_packet, const ProtocolVersion proto);

 private:
  ProtocolVersion proto_ = 0;

  /*
   * Information that needs to be saved from the startup packet and passed
   * into backend execution.  "char *" fields are NULL if not set.
   * guc_options points to a List of alternating option names and values.
   */
  string database_name_;
  string user_name_;
  string cmdline_options_;
  std::list<string> guc_options_;
};

/*
 * Old-style startup packet layout with fixed-width fields.  This is used in
 * protocol 1.0 and 2.0, but not in later versions.  Note that the fields
 * in this layout are '\0' terminated only if there is room.
 */

#define SM_DATABASE             64
#define SM_USER                 32
/* We append database name if db_user_namespace true. */
#define SM_DATABASE_USER (SM_DATABASE+SM_USER+1)        /* +1 for @ */
#define SM_OPTIONS              64
#define SM_UNUSED               64
#define SM_TTY                  64

typedef struct StartupPacket {
  ProtocolVersion protoVersion;   /* Protocol version */
  char            database[SM_DATABASE];  /* Database name */
  /* Db_user_namespace appends dbname */
  char            user[SM_USER];  /* User name */
  char            options[SM_OPTIONS];    /* Optional additional args */
  char            unused[SM_UNUSED];      /* Unused */
  char            tty[SM_TTY];    /* Tty for debug output */
} StartupPacket;

}  // namespace pgapi
}  // namespace yb

#endif  // YB_YQL_PGSQL_YBPOSTGRES_PGPORT_H_

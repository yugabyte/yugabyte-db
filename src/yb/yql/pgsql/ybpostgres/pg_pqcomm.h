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
//
// All files that are prefixed with "pg_" redefine Postgresql interface. We use the same file and
// function names as Postgresql's code to ease future effort in updating our behavior to match with
// changes in Postgresql, but the contents of these functions are generally redefined to work with
// the rest of Yugabyte code.
//--------------------------------------------------------------------------------------------------

/*-------------------------------------------------------------------------
 *
 * pqcomm.h
 *              Definitions common to frontends and backends.
 *
 * NOTE: for historical reasons, this does not correspond to pqcomm.c.
 * pqcomm.c's routines are declared in libpq.h.
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/libpq/pqcomm.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef YB_YQL_PGSQL_YBPOSTGRES_PG_PQCOMM_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PG_PQCOMM_H_

#include <arpa/inet.h>
#include <cstdint>

#include "yb/yql/pgsql/ybpostgres/pgdefs.h"
#include "yb/yql/pgsql/ybpostgres/pg_stringinfo.h"
#include "yb/util/net/socket.h"

namespace yb {
namespace pgapi {

/*
 * These manipulate the frontend/backend protocol version number.
 *
 * The major number should be incremented for incompatible changes.  The minor
 * number should be incremented for compatible changes (eg. additional
 * functionality).
 *
 * If a backend supports version m.n of the protocol it must actually support
 * versions m.[0..n].  Backend support for version m-1 can be dropped after a
 * `reasonable' length of time.
 *
 * A frontend isn't required to support anything other than the current
 * version.
 */

#define PG_PROTOCOL_MAJOR(v)    ((v) >> 16)
#define PG_PROTOCOL_MINOR(v)    ((v) & 0x0000ffff)
#define PG_PROTOCOL(m, n)       (((m) << 16) | (n))

//--------------------------------------------------------------------------------------------------
// Postgres Startup instructions.
// The first 4 bytes represent the opcode. Currently, there are 3 different opcodes.
// 1- Protocol.
// 2- CANCEL_REQUEST_CODE
// 3- NEGOTIATE_SSL_CODE
//--------------------------------------------------------------------------------------------------
/*
 * In protocol 3.0 and later, the startup packet length is not fixed, but
 * we set an arbitrary limit on it anyway.  This is just to prevent simple
 * denial-of-service attacks via sending enough data to run the server
 * out of memory.
 */
#define MAX_STARTUP_PACKET_LENGTH 10000

/*
 * Protocol.
 * The earliest and latest frontend/backend protocol version supported.
 */
#define PG_PROTOCOL_EARLIEST PG_PROTOCOL(2, 0)
#define PG_PROTOCOL_LATEST   PG_PROTOCOL(3, 0)
typedef uint32_t ProtocolVersion; /* FE/BE protocol version number */

/*
 * A client can also send a cancel-current-operation request to the postmaster.
 * This is uglier than sending it directly to the client's backend, but it
 * avoids depending on out-of-band communication facilities.
 *
 * The cancel request code must not match any protocol version number
 * we're ever likely to use.  This random choice should do.
 */
#define CANCEL_REQUEST_CODE PG_PROTOCOL(1234, 5678)
typedef ProtocolVersion MsgType;
struct CancelRequestPacket {
  /* Note that each field is stored in network byte order! */
  MsgType cancelRequestCode;    /* code to identify a cancel request */
  uint32 backendPID;            /* PID of client's backend */
  uint32 cancelAuthCode;        /* secret key to authorize cancel */
};

/*
 * A client can also start by sending a SSL negotiation request, to get a
 * secure channel.
 */
#define NEGOTIATE_SSL_CODE PG_PROTOCOL(1234, 5679)

//--------------------------------------------------------------------------------------------------
// Postgres authentication method.
//--------------------------------------------------------------------------------------------------
/* These are the authentication request codes sent by the backend. */
enum class AuthRequest : uint32_t {
  AUTH_REQ_OK = 0,                                                        // User is authenticated.
  AUTH_REQ_KRB4 = 1,                                        // Kerberos V4. Not supported any more.
  AUTH_REQ_KRB5 = 2,                                        // Kerberos V5. Not supported any more.
  AUTH_REQ_PASSWORD = 3,                                                               // Password.
  AUTH_REQ_CRYPT = 4,                                    // crypt password. Not supported any more.
  AUTH_REQ_MD5 = 5,                                                                // md5 password.
  AUTH_REQ_SCM_CREDS = 6,                                              // transfer SCM credentials.
  AUTH_REQ_GSS = 7,                                                       // GSSAPI without wrap().
  AUTH_REQ_GSS_CONT = 8,                                                 // Continue GSS exchanges.
  AUTH_REQ_SSPI = 9,                                              // SSPI negotiate without wrap().
  AUTH_REQ_SASL = 10,                                                 // Begin SASL authentication.
  AUTH_REQ_SASL_CONT = 11,                                         // Continue SASL authentication.
  AUTH_REQ_SASL_FIN = 12,                                                    // Final SASL message.
};

//--------------------------------------------------------------------------------------------------
// This class reads rpc message and return the command contents.
class PGPqCommandReader {
 public:
  typedef std::shared_ptr<PGPqCommandReader> SharedPtr;

  explicit PGPqCommandReader(const IoVecs& source, size_t start_offset);
  virtual ~PGPqCommandReader() {
  }

  int scan_length() const {
    return offset_;
  }

  CHECKED_STATUS pq_getmessage(const StringInfo& s, int maxlen);

  int pq_recvbuf();
  int pq_getbyte();
  int pq_peekbyte();
  int pq_getbyte_if_available(unsigned char *c);
  int pq_getbytes(char *s, size_t len);
  int pq_getbytes(const StringInfo& s, size_t len);
  int pq_discardbytes(size_t len);
  int pq_getstring(const StringInfo& s);
  int pq_getint32(int32_t *val) {
    char *bytes = reinterpret_cast<char*>(val);
    if (pq_getbytes(bytes, 4) == EOF) {
      return EOF;
    }
    *val = ntohl(*val);
    return 0;
  }

  void pq_startmsgread();
  void pq_endmsgread();
  bool pq_is_reading_msg();

 private:
  // Replace variable "PqRecvBuffer" in Postgresql.
  const IoVecs *source_;
  size_t total_bytes_;

  // Next index to read a byte from PqRecvBuffer.
  // Replace variable "PqRecvPointer" in Postgresql.
  size_t offset_;
};

}  // namespace pgapi
}  // namespace yb

#endif  // YB_YQL_PGSQL_YBPOSTGRES_PG_PQCOMM_H_

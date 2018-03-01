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
 * pqformat.c
 *              Routines for formatting and parsing frontend/backend messages
 *
 * Outgoing messages are built up in a StringInfo buffer (which is expansible)
 * and then sent in a single call to pq_putmessage.  This module provides data
 * formatting/conversion routines that are needed to produce valid messages.
 * Note in particular the distinction between "raw data" and "text"; raw data
 * is message protocol characters and binary values that are not subject to
 * character set conversion, while text is converted by character encoding
 * rules.
 *
 * Incoming messages are similarly read into a StringInfo buffer, via
 * pq_getmessage, and then parsed and converted from that using the routines
 * in this module.
 *
 * These same routines support reading and writing of external binary formats
 * (typsend/typreceive routines).  The conversion routines for individual
 * data types are exactly the same, only initialization and completion
 * are different.
 *
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *      src/backend/libpq/pqformat.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 * Message assembly and output:
 *              pq_beginmessage - initialize StringInfo buffer
 *              pq_sendbyte             - append a raw byte to a StringInfo buffer
 *              pq_sendint              - append a binary integer to a StringInfo buffer
 *              pq_sendint64    - append a binary 8-byte int to a StringInfo buffer
 *              pq_sendfloat4   - append a float4 to a StringInfo buffer
 *              pq_sendfloat8   - append a float8 to a StringInfo buffer
 *              pq_sendbytes    - append raw data to a StringInfo buffer
 *              pq_sendcountedtext - append a counted text string (with character set conversion)
 *              pq_sendtext             - append a text string (with conversion)
 *              pq_sendstring   - append a null-terminated text string (with conversion)
 *              pq_send_ascii_string - append a null-terminated text string (without conversion)
 *              pq_endmessage   - send the completed message to the frontend
 * Note: it is also possible to append data to the StringInfo buffer using
 * the regular StringInfo routines, but this is discouraged since required
 * character set conversion may not occur.
 *
 * typsend support (construct a bytea value containing external binary data):
 *              pq_begintypsend - initialize StringInfo buffer
 *              pq_endtypsend   - return the completed string as a "bytea*"
 *
 * Special-case message output:
 *              pq_puttextmessage - generate a character set-converted message in one step
 *              pq_putemptymessage - convenience routine for message with empty body
 *
 * Message parsing after input:
 *              pq_getmsgbyte   - get a raw byte from a message buffer
 *              pq_getmsgint    - get a binary integer from a message buffer
 *              pq_getmsgint64  - get a binary 8-byte int from a message buffer
 *              pq_getmsgfloat4 - get a float4 from a message buffer
 *              pq_getmsgfloat8 - get a float8 from a message buffer
 *              pq_getmsgbytes  - get raw data from a message buffer
 *              pq_copymsgbytes - copy raw data from a message buffer
 *              pq_getmsgtext   - get a counted text string (with conversion)
 *              pq_getmsgstring - get a null-terminated text string (with conversion)
 *              pq_getmsgrawstring - get a null-terminated text string - NO conversion
 *              pq_getmsgend    - verify message fully consumed
 */

#include "yb/yql/pgsql/ybpostgres/pg_pqformat.h"

using strings::Substitute;

namespace yb {
namespace pgapi {

// Because POSTGRES uses different utility functions than us, include these inside the namespace.
#include <arpa/inet.h>

//--------------------------------------------------------------------------------------------------
// API for writing messages.
//--------------------------------------------------------------------------------------------------

PGPqFormatter::PGPqFormatter(const StringInfo& read_buffer) : read_buf_(read_buffer) {
}

/* --------------------------------
 *              pq_beginmessage         - initialize for sending a message
 * --------------------------------
 */
void
PGPqFormatter::pq_beginmessage(char msgtype) {
  // Clear the previous message and start a new one.
  initStringInfo(&send_buf_, msgtype);

  // This seems like a hack in Postgresql's code. We might not need this any more.
  /*
   * We stash the message type into the buffer's cursor_ field, expecting
   * that the pq_sendXXX routines won't touch it.  We could alternatively
   * make it the first byte of the buffer contents, but this seems easier.
   */
  cursor_ = msgtype;
}

/* --------------------------------
 *              pq_sendbyte             - append a raw byte to a StringInfo buffer
 * --------------------------------
 */
void
PGPqFormatter::pq_sendbyte(int byt) {
  appendStringInfoChar(send_buf_, byt);
}

/* --------------------------------
 *              pq_sendbytes    - append raw data to a StringInfo buffer
 * --------------------------------
 */
void
PGPqFormatter::pq_sendbytes(const char *data, int datalen) {
  appendBinaryStringInfo(send_buf_, reinterpret_cast<const uint8_t*>(data), datalen);
}

/* --------------------------------
 *              pq_sendcountedtext - append a counted text string (with character set conversion)
 *
 * The data sent to the frontend by this routine is a 4-byte count field
 * followed by the string.  The count includes itself or not, as per the
 * countincludesself flag (pre-3.0 protocol requires it to include itself).
 * The passed text string need not be null-terminated, and the data sent
 * to the frontend isn't either.
 * --------------------------------
 */
void
PGPqFormatter::pq_sendcountedtext(const char *str, int slen, bool countincludesself) {
  int extra = countincludesself ? 4 : 0;
  const char *p = pg_server_to_client(str, slen);

  if (p != str) {                           /* actual conversion has been done? */
    slen = strlen(p);
    pq_sendint(slen + extra, 4);
    appendBinaryStringInfo(send_buf_, reinterpret_cast<const uint8_t*>(p), slen);
    pfree(p);

  } else {
    pq_sendint(slen + extra, 4);
    appendBinaryStringInfo(send_buf_, reinterpret_cast<const uint8_t*>(str), slen);
  }
}

/* --------------------------------
 *              pq_sendtext             - append a text string (with conversion)
 *
 * The passed text string need not be null-terminated, and the data sent
 * to the frontend isn't either.  Note that this is not actually useful
 * for direct frontend transmissions, since there'd be no way for the
 * frontend to determine the string length.  But it is useful for binary
 * format conversions.
 * --------------------------------
 */
void
PGPqFormatter::pq_sendtext(const char *str, int slen) {
  const char *p = pg_server_to_client(str, slen);
  if (p != str) {                           /* actual conversion has been done? */
    slen = strlen(p);
    appendBinaryStringInfo(send_buf_, p, slen);
    pfree(p);

  } else {
    appendBinaryStringInfo(send_buf_, str, slen);
  }
}

/* --------------------------------
 *              pq_sendstring   - append a null-terminated text string (with conversion)
 *
 * NB: passed text string must be null-terminated, and so is the data
 * sent to the frontend.
 * --------------------------------
 */
void
PGPqFormatter::pq_sendstring(const char *str) {
  int slen = strlen(str);
  const char *p = pg_server_to_client(str, slen);

  if (p != str) {                           /* actual conversion has been done? */
    slen = strlen(p);
    appendBinaryStringInfo(send_buf_, p, slen + 1);
    pfree(p);

  } else {
    appendBinaryStringInfo(send_buf_, str, slen + 1);
  }
}

/* --------------------------------
 *              pq_send_ascii_string    - append a null-terminated text string (without conversion)
 *
 * This function intentionally bypasses encoding conversion, instead just
 * silently replacing any non-7-bit-ASCII characters with question marks.
 * It is used only when we are having trouble sending an error message to
 * the client with normal localization and encoding conversion.  The caller
 * should already have taken measures to ensure the string is just ASCII;
 * the extra work here is just to make certain we don't send a badly encoded
 * string to the client (which might or might not be robust about that).
 *
 * NB: passed text string must be null-terminated, and so is the data
 * sent to the frontend.
 * --------------------------------
 */
void
PGPqFormatter::pq_send_ascii_string(const char *str) {
  while (*str) {
    char ch = *str++;
    if (IS_HIGHBIT_SET(ch)) {
      ch = '?';
    }
    appendStringInfoChar(send_buf_, ch);
  }
  appendStringInfoChar(send_buf_, '\0');
}

/* --------------------------------
 *              pq_sendint              - append a binary integer to a StringInfo buffer
 * --------------------------------
 */
void
PGPqFormatter::pq_sendint(int i, int bytes) {
  switch (bytes) {
    case 1: {
      unsigned char n8 = static_cast<unsigned char>(i);
      appendBinaryStringInfo(send_buf_, reinterpret_cast<char *>(&n8), 1);
      break;
    }
    case 2: {
      uint16_t n16 = htons(static_cast<uint16_t>(i));
      appendBinaryStringInfo(send_buf_, reinterpret_cast<char *>(&n16), 2);
      break;
    }
    case 4: {
      uint32_t n32 = htonl(static_cast<uint32_t>(i));
      appendBinaryStringInfo(send_buf_, reinterpret_cast<char *>(&n32), 4);
      break;
    }
    default:
      LOG(FATAL) << Substitute("unsupported integer size $0", bytes);
      break;
  }
}

/* --------------------------------
 *              pq_sendint64    - append a binary 8-byte int to a StringInfo buffer
 *
 * It is tempting to merge this with pq_sendint, but we'd have to make the
 * argument int64 for all data widths --- that could be a big performance
 * hit on machines where int64 isn't efficient.
 * --------------------------------
 */
void
PGPqFormatter::pq_sendint64(int64_t i) {
  uint32_t n32;

  /* High order half first, since we're doing MSB-first */
  n32 = static_cast<uint32>(i >> 32);
  n32 = htonl(n32);
  appendBinaryStringInfo(send_buf_, reinterpret_cast<char *>(&n32), 4);

  /* Now the low order half */
  n32 = static_cast<uint32>(i);
  n32 = htonl(n32);
  appendBinaryStringInfo(send_buf_, reinterpret_cast<char *>(&n32), 4);
}

/* --------------------------------
 *              pq_sendfloat4   - append a float4 to a StringInfo buffer
 *
 * The point of this routine is to localize knowledge of the external binary
 * representation of float4, which is a component of several datatypes.
 *
 * We currently assume that float4 should be byte-swapped in the same way
 * as int4.  This rule is not perfect but it gives us portability across
 * most IEEE-float-using architectures.
 * --------------------------------
 */
void
PGPqFormatter::pq_sendfloat4(float4 f) {
  union {
    float4          f;
    uint32          i;
  } swap;

  swap.f = f;
  swap.i = htonl(swap.i);
  appendBinaryStringInfo(send_buf_, reinterpret_cast<char *>(&swap.i), 4);
}

/* --------------------------------
 *              pq_sendfloat8   - append a float8 to a StringInfo buffer
 *
 * The point of this routine is to localize knowledge of the external binary
 * representation of float8, which is a component of several datatypes.
 *
 * We currently assume that float8 should be byte-swapped in the same way
 * as int8.  This rule is not perfect but it gives us portability across
 * most IEEE-float-using architectures.
 * --------------------------------
 */
void
PGPqFormatter::pq_sendfloat8(float8 f) {
  union {
    float8          f;
    int64           i;
  } swap;

  swap.f = f;
  pq_sendint64(swap.i);
}

//--------------------------------------------------------------------------------------------------
// API for reading messages.
//--------------------------------------------------------------------------------------------------

/* --------------------------------
 *              pq_getmsgbyte   - get a raw byte from a message buffer
 * --------------------------------
 */
int
PGPqFormatter::pq_getmsgbyte() {
  CHECK_LE(cursor_, read_buf_->size()) << "no data left in message";
  return static_cast<unsigned char>(*read_buf_->buffer(cursor_++));
}

/* --------------------------------
 *              pq_getmsgint    - get a binary integer from a message buffer
 *
 *              Values are treated as unsigned.
 * --------------------------------
 */
unsigned int
PGPqFormatter::pq_getmsgint(int b) {
  unsigned int result;

  switch (b) {
    case 1: {
      unsigned char n8;
      pq_copymsgbytes(reinterpret_cast<char *>(&n8), 1);
      result = n8;
      break;
    }
    case 2: {
      uint16 n16;
      pq_copymsgbytes(reinterpret_cast<char *>(&n16), 2);
      result = ntohs(n16);
      break;
    }
    case 4: {
      uint32 n32;
      pq_copymsgbytes(reinterpret_cast<char *>(&n32), 4);
      result = ntohl(n32);
      break;
    }
    default:
      LOG(FATAL) << Substitute("unsupported integer size $0", b);
      result = 0;                     /* keep compiler quiet */
      break;
  }
  return result;
}

/* --------------------------------
 *              pq_getmsgint64  - get a binary 8-byte int from a message buffer
 *
 * It is tempting to merge this with pq_getmsgint, but we'd have to make the
 * result int64 for all data widths --- that could be a big performance
 * hit on machines where int64 isn't efficient.
 * --------------------------------
 */
int64
PGPqFormatter::pq_getmsgint64() {
  int64 result;
  uint32 h32;
  uint32 l32;

  pq_copymsgbytes(reinterpret_cast<char *>(&h32), 4);
  pq_copymsgbytes(reinterpret_cast<char *>(&l32), 4);
  h32 = ntohl(h32);
  l32 = ntohl(l32);

  result = h32;
  result <<= 32;
  result |= l32;

  return result;
}

/* --------------------------------
 *              pq_getmsgfloat4 - get a float4 from a message buffer
 *
 * See notes for pq_sendfloat4.
 * --------------------------------
 */
float4
PGPqFormatter::pq_getmsgfloat4() {
  union {
    float4 f;
    uint32 i;
  } swap;

  swap.i = pq_getmsgint(4);
  return swap.f;
}

/* --------------------------------
 *              pq_getmsgfloat8 - get a float8 from a message buffer
 *
 * See notes for pq_sendfloat8.
 * --------------------------------
 */
float8
PGPqFormatter::pq_getmsgfloat8() {
  union {
    float8 f;
    int64 i;
  } swap;

  swap.i = pq_getmsgint64();
  return swap.f;
}

/* --------------------------------
 *              pq_getmsgbytes  - get raw data from a message buffer
 *
 *              Returns a pointer directly into the message buffer; note this
 *              may not have any particular alignment.
 * --------------------------------
 */
const char*
PGPqFormatter::pq_getmsgbytes(int datalen) {
  if (datalen < 0 || datalen > (read_buf_->size() - cursor_)) {
    // TODO(neil) Should report error to client.
    LOG(FATAL) << "insufficient data left in message";
  }

  const char *result = read_buf_->buffer(cursor_);
  cursor_ += datalen;
  return result;
}

/* --------------------------------
 *              pq_copymsgbytes - copy raw data from a message buffer
 *
 *              Same as above, except data is copied to caller's buffer.
 * --------------------------------
 */
void
PGPqFormatter::pq_copymsgbytes(char *buf, int datalen) {
  if (datalen < 0 || datalen > (read_buf_->size() - cursor_)) {
    // TODO(neil) Should report error to client.
    LOG(FATAL) << "insufficient data left in message";
  }

  memcpy(buf, read_buf_->buffer(cursor_), datalen);
  cursor_ += datalen;
}

/* --------------------------------
 *              pq_getmsgtext   - get a counted text string (with conversion)
 *
 *              Always returns a pointer to a freshly palloc'd result.
 *              The result has a trailing null, *and* we return its strlen in *nbytes.
 * --------------------------------
 */
string
PGPqFormatter::pq_getmsgtext(int rawbytes, int *nbytes) {
  if (rawbytes < 0 || rawbytes > (read_buf_->size() - cursor_)) {
    // TODO(neil) Should report error to client.
    LOG(FATAL) << "insufficient data left in message";
  }

  const char *str = read_buf_->buffer(cursor_);
  cursor_ += rawbytes;

  string result;
  const char *p = pg_client_to_server(str, rawbytes);
  if (p != str) {                           /* actual conversion has been done? */
    *nbytes = strlen(p);
    pfree(p);
    result = p;
  } else {
    result = p;
    *nbytes = rawbytes;
  }
  return p;
}

/* --------------------------------
 *              pq_getmsgstring - get a null-terminated text string (with conversion)
 *
 *              May return a pointer directly into the message buffer, or a pointer
 *              to a palloc'd conversion result.
 * --------------------------------
 */
const char *PGPqFormatter::pq_getmsgstring() {
  const char *str = read_buf_->buffer(cursor_);
  const int read_len = strlen(str);
  cursor_ += read_len + 1;
  DCHECK_LE(cursor_, read_buf_->size());

  return pg_client_to_server(str, read_len);
}

/* --------------------------------
 *              pq_getmsgrawstring - get a null-terminated text string - NO conversion
 *
 *              Returns a pointer directly into the message buffer.
 * --------------------------------
 */
const char *
PGPqFormatter::pq_getmsgrawstring() {
  const char *str = read_buf_->buffer(cursor_);
  const int read_len = strlen(str);
  cursor_ += read_len + 1;
  DCHECK_LE(cursor_, read_buf_->size());

  return str;
}

/* --------------------------------
 *              pq_getmsgend    - verify message fully consumed
 * --------------------------------
 */
CHECKED_STATUS PGPqFormatter::pq_getmsgend() {
  if (cursor_ != read_buf_->size()) {
    return STATUS(NetworkError, "Invalid message format");
  }
  return Status::OK();
}

}  // namespace pgapi
}  // namespace yb

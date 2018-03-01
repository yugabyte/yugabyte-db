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
 * pqcomm.c
 *        Communication functions between the Frontend and the Backend
 *
 * These routines handle the low-level details of communication between
 * frontend and backend.  They just shove data across the communication
 * channel, and are ignorant of the semantics of the data --- or would be,
 * except for major brain damage in the design of the old COPY OUT protocol.
 * Unfortunately, COPY OUT was designed to commandeer the communication
 * channel (it just transfers data without wrapping it into messages).
 * No other messages can be sent while COPY OUT is in progress; and if the
 * copy is aborted by an ereport(ERROR), we need to close out the copy so that
 * the frontend gets back into sync.  Therefore, these routines have to be
 * aware of COPY OUT state.  (New COPY-OUT is message-based and does *not*
 * set the DoingCopyOut flag.)
 *
 * NOTE: generally, it's a bad idea to emit outgoing messages directly with
 * pq_putbytes(), especially if the message would require multiple calls
 * to send.  Instead, use the routines in pqformat.c to construct the message
 * in a buffer and then emit it in one call to pq_putmessage.  This ensures
 * that the channel will not be clogged by an incomplete message if execution
 * is aborted by ereport(ERROR) partway through the message.  The only
 * non-libpq code that should call pq_putbytes directly is old-style COPY OUT.
 *
 * At one time, libpq was shared between frontend and backend, but now
 * the backend's "backend/libpq" is quite separate from "interfaces/libpq".
 * All that remains is similarities of names to trap the unwary...
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *      src/backend/libpq/pqcomm.c
 *
 *-------------------------------------------------------------------------
 */

#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"

namespace yb {
namespace pgapi {

using strings::Substitute;

//--------------------------------------------------------------------------------------------------
// Constructor.
PGPqCommandReader::PGPqCommandReader(const IoVecs& source, size_t start_offset)
    : source_(&source),
      total_bytes_(IoVecsFullSize(source)),
      offset_(start_offset) {
}

int PGPqCommandReader::pq_recvbuf() {
  // We don't read message the way Postgres does, so this function is only called when we reach
  // end of message.
  LOG(INFO) << "Reach end of message unexpectedly. Wait for more data from the rpc layer";
  return 1;
}

//--------------------------------------------------------------------------------------------------
// Get a single byte from connection, or return EOF.
int PGPqCommandReader::pq_getbyte() {
  while (offset_ >= total_bytes_) {
    if (pq_recvbuf())
      return EOF;
  }

  std::vector<char> buffer(1);
  IoVecsToBuffer(*source_, offset_, offset_ + 1, &buffer);
  ++offset_;
  return (unsigned char)buffer[0];
}

/* --------------------------------
 *              pq_peekbyte             - peek at next byte from connection
 *
 *       Same as pq_getbyte() except we don't advance the pointer.
 * --------------------------------
 */
int PGPqCommandReader::pq_peekbyte() {
  while (offset_ >= total_bytes_) {
    if (pq_recvbuf())
      return EOF;
  }

  std::vector<char> buffer(1);
  IoVecsToBuffer(*source_, offset_, offset_ + 1, &buffer);
  return (unsigned char)buffer[0];
}

/* --------------------------------
 *              pq_getbyte_if_available - get a single byte from connection,
 *                      if available
 *
 * The received byte is stored in *c. Returns 1 if a byte was read,
 * 0 if no data was available, or EOF if trouble.
 * --------------------------------
 */
int PGPqCommandReader::pq_getbyte_if_available(unsigned char *c) {
  if (offset_ < total_bytes_) {
    std::vector<char> buffer(1);
    IoVecsToBuffer(*source_, offset_, offset_ + 1, &buffer);
    ++offset_;
    *c = (unsigned char)buffer[0];
    return 1;
  }
  return 0;
}

/* --------------------------------
 *              pq_getbytes             - get a known number of bytes from connection
 *
 *              returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int PGPqCommandReader::pq_getbytes(char *s, size_t len) {
  if (offset_ + len > total_bytes_) {
    if (pq_recvbuf())       /* If nothing in buffer, then recv some */
      return EOF;           /* Failed to recv data */
  }

  // Read data.
  std::vector<char> buffer(len);
  IoVecsToBuffer(*source_, offset_, offset_ + len, &buffer);
  memcpy(s, buffer.data(), len);

  // Advance offset_ to move the read-cursor and return.
  offset_ += len;
  return 0;
}

int PGPqCommandReader::pq_getbytes(const StringInfo& s, size_t len) {
  if (offset_ + len > total_bytes_) {
    if (pq_recvbuf())         /* If nothing in buffer, then recv some */
      return EOF;             /* Failed to recv data */
  }

  // Read data.
  std::vector<char> buffer(len);
  IoVecsToBuffer(*source_, offset_, offset_ + len, &buffer);
  appendBinaryStringInfo(s, buffer.data(), len);

  // Advance offset_ to move the read-cursor and return.
  offset_ += len;
  return 0;
}

/* --------------------------------
 *              pq_discardbytes         - throw away a known number of bytes
 *
 *              same as pq_getbytes except we do not copy the data to anyplace.
 *              this is used for resynchronizing after read errors.
 *
 *              returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int PGPqCommandReader::pq_discardbytes(size_t len) {
  if (offset_ + len > total_bytes_) {
    if (pq_recvbuf())         /* If nothing in buffer, then recv some */
      return EOF;             /* Failed to recv data */
  }

  // Advance offset_ to move the read-cursor and return.
  offset_ += len;
  return 0;
}

/* --------------------------------
 *              pq_getstring    - get a null terminated string from connection
 *
 *              The return value is placed in an expansible StringInfo, which has
 *              already been initialized by the caller.
 *
 *              This is used only for dealing with old-protocol clients.  The idea
 *              is to produce a StringInfo that looks the same as we would get from
 *              pq_getmessage() with a newer client; we will then process it with
 *              pq_getmsgstring.  Therefore, no character set conversion is done here,
 *              even though this is presumably useful only for text.
 *
 *              returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int PGPqCommandReader::pq_getstring(const StringInfo& s) {
  resetStringInfo(s);

  if (offset_ >= total_bytes_) {
    if (pq_recvbuf())       /* If nothing in buffer, then recv some */
      return EOF;             /* Failed to recv data */
  }

  // TODO(neil) We should read at most 4K at a time.  For now, just read the whole buffer.
  const size_t len = total_bytes_ - offset_;
  std::vector<char> buffer(len + 1);
  IoVecsToBuffer(*source_, offset_, offset_ + len, &buffer);
  buffer[len] = 0;

  /* Read until we get the terminating '\0' */
  const size_t text_len = strlen(buffer.data());
  appendBinaryStringInfo(s, buffer.data(), text_len);

  // Advance offset_ to move the read-cursor and return.
  offset_ += len;
  return 0;
}

/* --------------------------------
 *              pq_getmessage   - get a message with length word from connection
 *
 *              The return value is placed in an expansible StringInfo, which has
 *              already been initialized by the caller.
 *              Only the message body is placed in the StringInfo; the length word
 *              is removed.  Also, s->cursor is initialized to zero for convenience
 *              in scanning the message contents.
 *
 *              If maxlen is not zero, it is an upper limit on the length of the
 *              message we are willing to accept.  We abort the connection (by
 *              returning EOF) if client tries to send more than that.
 *
 *              returns 0 if OK, EOF if trouble
 * --------------------------------
 */
CHECKED_STATUS PGPqCommandReader::pq_getmessage(const StringInfo& s, int maxlen) {
  resetStringInfo(s);

  /* Read message length word */
  int64_t len;
  if (pq_getbytes(reinterpret_cast<char *>(&len), 4) == EOF) {
    return STATUS(NetworkError, "Unexpected EOF within message length word");
  }

  len = ntohl(len);
  if (len < 4 || (maxlen > 0 && len > maxlen)) {
    return STATUS(NetworkError, "Invalid message length");
  }

  len -= 4;                                       /* discount length itself */
  if (len > 0) {
    /*
     * Allocate space for message.  If we run out of room (ridiculously
     * large message), we will elog(ERROR), but we want to discard the
     * message body so as not to lose communication sync.
     */
    enlargeStringInfo(s, len);

    /* And grab the message */
    if (pq_getbytes(s, len) == EOF) {
      // Need to wait for more data.
      return Status::OK();
    }
  }

  return Status::OK();
}

}  // namespace pgapi
}  // namespace yb

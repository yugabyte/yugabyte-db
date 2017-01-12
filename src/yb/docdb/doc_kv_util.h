// Copyright (c) YugaByte, Inc.

// Utilities for encoding and decoding key/value pairs that are used in the document DB code.

#ifndef YB_DOCDB_DOC_KV_UTIL_H_
#define YB_DOCDB_DOC_KV_UTIL_H_

#include <string>
#include "yb/util/monotime.h"

#include "rocksdb/slice.h"

#include "yb/common/timestamp.h"
#include "yb/gutil/endian.h"
#include "yb/util/status.h"

namespace yb {
namespace docdb {

constexpr int kEncodedKeyStrTerminatorSize = 2;

constexpr int kBytesPerTimestamp = sizeof(yb::Timestamp);

// Timestamps are assumed to be represented as 64-bit integers.
static_assert(kBytesPerTimestamp == 8, "Expected timestamp size to be 8 bytes");

// This is used to invert all bits in a 64-bit timestamp so that higher timestamps appear first in
// the sorted order.
constexpr uint64_t kTimestampInversionMask = 0xffffffffffffffffL;

// We are flipping the sign bit of 64-bit integers appearing as object keys in a document so that
// negative numbers sort earlier.
constexpr uint64_t kInt64SignBitFlipMask = 0x8000000000000000L;

// Checks whether the given RocksDB key belongs to a document identified by the given encoded
// document key (a key that has already had zero characters escaped). This is done simply by
// checking that the key starts with the encoded document key followed by two zero characters.
// This is only used in unit tests as of 08/02/2016.
bool KeyBelongsToDocKeyInTest(const rocksdb::Slice &key, const std::string &encoded_doc_key);

// Decode a timestamp stored at the given position in the given slice. Timestamps are stored
// inside keys as big-endian 64-bit integers with all bits inverted for reverse sorting.
yb::Timestamp DecodeTimestampFromKey(const rocksdb::Slice& key, int pos);

// Consumes timestamp from the given slice, decreasing the slice size by the timestamp size.
// Timestamp is stored in a "key-appropriate" format (bits inverted for reverse sorting).
// @param slice The slice holding RocksDB key bytes.
// @param timestamp Where to store the timestamp. Undefined in case of failure.
yb::Status ConsumeTimestampFromKey(rocksdb::Slice* slice, Timestamp* timestamp);

inline void AppendBigEndianUInt64(uint64_t u, std::string* dest) {
  char buf[sizeof(uint64_t)];
  BigEndian::Store64(buf, u);
  dest->append(buf, sizeof(buf));
}

// Encodes and appends timestamp to the given string representing a RocksDB key. Timestamps are
// encoded as big-endian 64-bit integers with all bits inverted for reverse sorting.
inline void AppendEncodedTimestampToKey(yb::Timestamp timestamp, std::string *dest) {
  AppendBigEndianUInt64(timestamp.value() ^ kTimestampInversionMask, dest);
}

// Encode and append the given signed 64-bit integer to the destination string holding a RocksDB
// key being constructed. We are flipping the sign bit so that negative numbers sort before positive
// ones.
inline void AppendInt64ToKey(int64_t val, std::string* dest) {
  char buf[sizeof(uint64_t)];
  // Flip the sign bit so that negative values sort before positive ones when compared as
  // big-endian byte sequences.
  BigEndian::Store64(buf, val ^ kInt64SignBitFlipMask);
  dest->append(buf, sizeof(buf));
}

inline void AppendUInt32ToKey(uint32_t val, std::string* dest) {
  char buf[sizeof(uint32_t)];
  BigEndian::Store32(buf, val);
  dest->append(buf, sizeof(buf));
}

// Encodes the given string by replacing '\x00' with "\x00\x01" and appends it to the given
// destination string.
void AppendZeroEncodedStrToKey(const std::string &s, std::string *dest);

// Appends two zero characters to the given string.
inline void TerminateZeroEncodedKeyStr(std::string *dest) {
  dest->push_back('\0');
  dest->push_back('\0');
}

inline void ZeroEncodeAndAppendStrToKey(const std::string &s, std::string *dest) {
  AppendZeroEncodedStrToKey(s, dest);
  TerminateZeroEncodedKeyStr(dest);
}

inline std::string ZeroEncodeStr(std::string s) {
  std::string result;
  ZeroEncodeAndAppendStrToKey(s, &result);
  return result;
}

// Reverses the encoding we use for string fields in a RocksDB key where a zero is represented as
// \0x00\0x01 and the string is terminated with \x00\x00.
// Input/output:
//   slice - a slice containing an encoded string, optionally terminated by \x00\x00. A prefix of
//           this slice is consumed.
// Output (undefined in case of an error):
//   result - the resulting decoded string
yb::Status DecodeZeroEncodedStr(rocksdb::Slice* slice, std::string* result);

// A version of the above function that ensures the encoding is correct and all characters are
// consumed.
std::string DecodeZeroEncodedStr(std::string encoded_str);

// We try to use up to this number of characters when converting raw bytes to strings for debug
// purposes.
constexpr int kShortDebugStringLength = 40;

// Produces a debug-friendly representation of a sequence of bytes that may contain non-printable
// characters.
// @return A human-readable representation of the given slice, capped at a fixed short length.
std::string ToShortDebugStr(rocksdb::Slice slice);

inline std::string ToShortDebugStr(const std::string& raw_str) {
  return ToShortDebugStr(rocksdb::Slice(raw_str));
}

CHECKED_STATUS hasExpiredTTL(const rocksdb::Slice &key, const MonoDelta &ttl,
    const Timestamp &timestamp, bool *hasExpired);

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_KV_UTIL_H_

// Copyright (c) YugaByte, Inc.

// Utilities for encoding and decoding key/value pairs that are used in the document DB code.

#ifndef YB_DOCDB_DOC_KV_UTIL_H_
#define YB_DOCDB_DOC_KV_UTIL_H_

#include <string>

#include "rocksdb/slice.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/gutil/endian.h"
#include "yb/util/decimal.h"
#include "yb/util/memcmpable_varint.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/fast_varint.h"

namespace yb {
namespace docdb {

constexpr int kEncodedKeyStrTerminatorSize = 2;

// We are flipping the sign bit of 64-bit integers appearing as object keys in a document so that
// negative numbers sort earlier.
constexpr uint64_t kInt64SignBitFlipMask = 0x8000000000000000L;
constexpr uint32_t kInt32SignBitFlipMask = 0x80000000;

// Checks whether the given RocksDB key belongs to a document identified by the given encoded
// document key (a key that has already had zero characters escaped). This is done simply by
// checking that the key starts with the encoded document key followed by two zero characters.
// This is only used in unit tests as of 08/02/2016.
bool KeyBelongsToDocKeyInTest(const rocksdb::Slice &key, const std::string &encoded_doc_key);

// Decode a DocHybridTime stored in the end of the given slice.
CHECKED_STATUS DecodeHybridTimeFromEndOfKey(const rocksdb::Slice &key, DocHybridTime *dest);

// Given a DocDB key stored in RocksDB, validate the DocHybridTime size stored as the
// last few bits of the final byte of the key, and ensure that the ValueType byte preceding that
// encoded DocHybridTime is ValueType::kHybridTime.
CHECKED_STATUS CheckHybridTimeSizeAndValueType(
    const rocksdb::Slice& key,
    int* ht_byte_size_dest);

// Consumes hybrid time from the given slice, decreasing the slice size by the hybrid time size.
// Hybrid time is stored in a "key-appropriate" format (bits inverted for reverse sorting).
// @param slice The slice holding RocksDB key bytes.
// @param hybrid_time Where to store the hybrid time. Undefined in case of failure.
yb::Status ConsumeHybridTimeFromKey(rocksdb::Slice* slice, DocHybridTime* hybrid_time);

inline void AppendBigEndianUInt64(uint64_t u, std::string* dest) {
  char buf[sizeof(uint64_t)];
  BigEndian::Store64(buf, u);
  dest->append(buf, sizeof(buf));
}

inline void AppendBigEndianUInt32(uint32_t u, std::string* dest) {
  char buf[sizeof(uint32_t)];
  BigEndian::Store32(buf, u);
  dest->append(buf, sizeof(buf));
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

inline int64_t DecodeInt64FromKey(const rocksdb::Slice& slice) {
  uint64_t v = BigEndian::Load64(slice.data());
  return v ^ kInt64SignBitFlipMask;
}

inline void AppendInt32ToKey(int32_t val, std::string* dest) {
  char buf[sizeof(int32_t)];
  BigEndian::Store32(buf, val ^ kInt32SignBitFlipMask);
  dest->append(buf, sizeof(buf));
}

inline void AppendUInt16ToKey(uint16_t val, std::string* dest) {
  char buf[sizeof(uint16_t)];
  BigEndian::Store16(buf, val);
  dest->append(buf, sizeof(buf));
}

inline void AppendFloatToKey(float val, std::string* dest) {
  char buf[sizeof(uint32_t)];
  uint32_t v = *(reinterpret_cast<uint32_t*>(&val));
  LOG(INFO) << "here " << val << ", " << v;
  if (v >> 31) { // This is the sign bit: better than using val >= 0 (because -0, nulls denormals).
    v = ~v;
  } else {
    v ^= kInt32SignBitFlipMask;
  }
  BigEndian::Store32(buf, v);
  dest->append(buf, sizeof(buf));
}

inline float DecodeFloatFromKey(const rocksdb::Slice& slice) {
  uint32_t v = BigEndian::Load32(slice.data());
  if (v >> 31) { // This is the sign bit: better than using val >= 0 (because -0, nulls denormals).
    v ^= kInt32SignBitFlipMask;
  } else {
    v = ~v;
  }
  return *(reinterpret_cast<float*>(&v));
}

inline void AppendDoubleToKey(double val, std::string* dest) {
  char buf[sizeof(uint64_t)];
  uint64_t v = *(reinterpret_cast<uint64_t*>(&val));
  if (v >> 63) { // This is the sign bit: better than using val >= 0 (because -0, nulls denormals).
    v = ~v;
  } else {
    v ^= kInt64SignBitFlipMask;
  }
  BigEndian::Store64(buf, v);
  dest->append(buf, sizeof(buf));
}

inline double DecodeDoubleFromKey(const rocksdb::Slice& slice) {
  uint64_t v = BigEndian::Load64(slice.data());
  if (v >> 63) { // This is the sign bit: better than using val >= 0 (because -0, nulls denormals).
    v ^= kInt64SignBitFlipMask;
  } else {
    v = ~v;
  }
  return *(reinterpret_cast<double*>(&v));
}

inline void AppendColumnIdToKey(ColumnId val, std::string* dest) {
  yb::util::FastAppendSignedVarIntToStr(val.rep(), dest);
}

// Encodes the given string by replacing '\x00' with "\x00\x01" and appends it to the given
// destination string.
void AppendZeroEncodedStrToKey(const std::string &s, std::string *dest);

// Encodes the given string by replacing '\xff' with "\xff\xfe" and appends it to the given
// destination string.
void AppendComplementZeroEncodedStrToKey(const string &s, string *dest);

// Appends two zero characters to the given string. We don't add final end-of-string characters in
// this function.
void TerminateZeroEncodedKeyStr(std::string *dest);

// Appends two '\0xff' characters to the given string. We don't add final end-of-string characters
// in this function.
void TerminateComplementZeroEncodedKeyStr(std::string *dest);

inline void ZeroEncodeAndAppendStrToKey(const std::string &s, std::string *dest) {
  AppendZeroEncodedStrToKey(s, dest);
  TerminateZeroEncodedKeyStr(dest);
}

inline void ComplementZeroEncodeAndAppendStrToKey(const std::string &s, std::string *dest) {
  AppendComplementZeroEncodedStrToKey(s, dest);
  TerminateComplementZeroEncodedKeyStr(dest);
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

// Reverses the encoding for a string that was encoded with ComplementZeroEncodeAndAppendStrToKey.
// In this representation the string termination changes from \x00\x00 to
// \xFF\xFF.
// Input/output:
//   slice - a slice containing an encoded string, optionally terminated by \xFF\xFF. A prefix of
//           this slice is consumed.
// Output (undefined in case of an error):
//   result - the resulting decoded string
yb::Status DecodeComplementZeroEncodedStr(rocksdb::Slice* slice, std::string* result);


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

// Determines whether or not the TTL for a key has expired, given the ttl for the key, its hybrid
// time and the hybrid_time we're reading at. The result is stored in has_expired.
CHECKED_STATUS HasExpiredTTL(const HybridTime& key_hybrid_time, const MonoDelta& ttl,
                             const HybridTime& read_hybrid_time, bool* has_expired);

// Computes the table level TTL, given a schema.
const MonoDelta TableTTL(const Schema& schema);

// Computes the effective TTL by combining the column level TTL with the table level TTL.
const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const MonoDelta& table_ttl);

// Utility function that computes the effective TTL directly given a schema
const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const Schema& schema);

// Cassandra considers a TTL of zero as resetting the TTL.
static const uint64_t kResetTTL = 0;

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_KV_UTIL_H_

// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_kv_util.h"

#include <string>

#include "rocksdb/slice.h"

#include "yb/common/timestamp.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/status.h"

using std::string;

using strings::Substitute;
using yb::Timestamp;
using yb::util::FormatBytesAsStr;

namespace yb {
namespace docdb {

bool KeyBelongsToDocKey(const rocksdb::Slice &key, const string &encoded_doc_key) {
  if (key.starts_with(encoded_doc_key)) {
    const int encoded_doc_key_size = encoded_doc_key.size();
    const char* key_data = key.data();
    return key.size() >= encoded_doc_key_size + 2 &&
           key_data[encoded_doc_key_size] == '\0' &&
           key_data[encoded_doc_key_size + 1] == '\0';
  } else {
    return false;
  }
}

Timestamp DecodeTimestampFromKey(const rocksdb::Slice& key, const int pos) {
  assert(key.size() >= pos + sizeof(int64_t));
  // We invert all bits of the 64-bit timestamp (which is equivalent to subtracting the timestamp
  // from the maximum unsigned 64-bit integer) so that newer timestamps are sorted first.

  return Timestamp(BigEndian::Load64(key.data() + pos) ^ kTimestampInversionMask);
}

Status ConsumeTimestampFromKey(rocksdb::Slice* slice, Timestamp* timestamp) {
  if (slice->size() < kBytesPerTimestamp) {
    return STATUS(Corruption,
        Substitute("$0 bytes is not enough to decode a timestamp, need $1: $2",
            slice->size(),
            kBytesPerTimestamp,
            FormatRocksDBSliceAsStr(*slice)));
  }
  *timestamp = Timestamp(BigEndian::Load64(slice->data()) ^ kTimestampInversionMask);
  slice->remove_prefix(kBytesPerTimestamp);
  return Status::OK();
}

void AppendZeroEncodedStrToKey(const string &s, string *dest) {
  if (s.find('\0') == string::npos) {
    // Fast path: no zero characters, nothing to encode.
    dest->append(s);
  } else {
    for (char c : s) {
      if (c == '\0') {
        dest->push_back('\0');
        dest->push_back('\x01');
      } else {
        dest->push_back(c);
      }
    }
  }
}

Status DecodeZeroEncodedStr(rocksdb::Slice* slice, string* result) {
  const char* p = slice->data();
  const char* end = p + slice->size();

  while (p != end) {
    if (*p == '\0') {
      ++p;
      if (p == end) {
        return STATUS(Corruption, "Zero-encoded string ends with only one zero");
      }
      if (*p == '\0') {
        // Found two zero characters, this is the end of the encoded string.
        ++p;
        break;
      }
      if (*p == '\x01') {
        // Zero character is encoded as \x00\x01.
        result->push_back('\0');
        ++p;
      } else {
        return STATUS(Corruption, StringPrintf(
            "Invalid sequence in a zero-encoded string: \\0x00\\0x%02x "
            "(must be either \\0x00\\x00 or \\0x00\\0x01)", *p));
      }
    } else {
      result->push_back(*p);
      ++p;
    }
  }
  result->shrink_to_fit();
  slice->remove_prefix(p - slice->data());
  return Status::OK();
}

string DecodeZeroEncodedStr(string encoded_str) {
  string result;
  rocksdb::Slice slice(encoded_str);
  Status status = DecodeZeroEncodedStr(&slice, &result);
  if (!status.ok()) {
    LOG(FATAL) << "Failed to decode zero-encoded string " << FormatBytesAsStr(encoded_str) << ": "
               << status.ToString();
  }
  if (!slice.empty()) {
    LOG(FATAL) << "Did not consume all characters from a zero-encoded string "
               << FormatBytesAsStr(encoded_str) << ": "
               << "bytes left: " << slice.size() << ", "
               << "encoded_str.size(): " << encoded_str.size();
  }
  return result;
}

}
}

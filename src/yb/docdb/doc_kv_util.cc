// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/server/hybrid_clock.h"

#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"
#include "yb/docdb/value.h"

using std::string;

using strings::Substitute;
using yb::HybridTime;
using yb::util::FormatBytesAsStr;

namespace yb {
namespace docdb {

bool KeyBelongsToDocKeyInTest(const rocksdb::Slice &key, const string &encoded_doc_key) {
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

HybridTime DecodeHybridTimeFromKey(const rocksdb::Slice& key) {
  const int pos = key.size() - kBytesPerHybridTime;
  return DecodeHybridTimeFromKey(key, pos);
}

HybridTime DecodeHybridTimeFromKey(const rocksdb::Slice& key, const int pos) {
  CHECK_GE(key.size(), pos + sizeof(int64_t));
  // We invert all bits of the 64-bit hybrid_time (which is equivalent to subtracting the
  // hybrid_time from the maximum unsigned 64-bit integer) so that newer hybrid_times are sorted
  // first.

  return HybridTime(BigEndian::Load64(key.data() + pos) ^ kHybridTimeInversionMask);
}

Status ConsumeHybridTimeFromKey(rocksdb::Slice* slice, HybridTime* hybrid_time) {
  if (slice->size() < kBytesPerHybridTime) {
    return STATUS(Corruption,
        Substitute("$0 bytes is not enough to decode a hybrid_time, need $1: $2",
            slice->size(),
            kBytesPerHybridTime,
            FormatRocksDBSliceAsStr(*slice)));
  }
  *hybrid_time = HybridTime(BigEndian::Load64(slice->data()) ^ kHybridTimeInversionMask);
  slice->remove_prefix(kBytesPerHybridTime);
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

std::string ToShortDebugStr(rocksdb::Slice slice) {
  return yb::FormatRocksDBSliceAsStr(slice, kShortDebugStringLength);
}

CHECKED_STATUS HasExpiredTTL(const rocksdb::Slice& key, const MonoDelta& ttl,
                             const HybridTime& read_hybrid_time, bool* has_expired) {
  *has_expired = false;
  if (!ttl.Equals(Value::kMaxTtl)) {
    RETURN_NOT_OK(HasExpiredTTL(DecodeHybridTimeFromKey(key), ttl, read_hybrid_time, has_expired));
  }
  return Status::OK();
}

CHECKED_STATUS HasExpiredTTL(const HybridTime& key_hybrid_time, const MonoDelta& ttl,
                             const HybridTime& read_hybrid_time, bool* has_expired) {
  *has_expired = false;
  if (!ttl.Equals(Value::kMaxTtl)) {
    // We avoid using AddPhysicalTimeToHybridTime, since there might be overflows after addition.
    *has_expired = server::HybridClock::CompareHybridClocksToDelta(key_hybrid_time,
                                                                   read_hybrid_time, ttl) > 0;
  }
  return Status::OK();
}

const MonoDelta TableTTL(const Schema& schema) {
  MonoDelta ttl = Value::kMaxTtl;
  if (schema.table_properties().HasDefaultTimeToLive()) {
    uint64_t table_ttl = schema.table_properties().DefaultTimeToLive();
    return table_ttl == kResetTTL ? Value::kMaxTtl : MonoDelta::FromMilliseconds(table_ttl);
  }
  return ttl;
}

const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const Schema& schema) {
  MonoDelta ttl;
  if (!value_ttl.Equals(Value::kMaxTtl)) {
    ttl = value_ttl.ToMilliseconds() == kResetTTL ? Value::kMaxTtl : value_ttl;
  } else {
    // This is the default.
    ttl = TableTTL(schema);
  }
  return ttl;
}

}  // namespace docdb
}  // namespace yb

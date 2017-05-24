// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_kv_util.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/value.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/server/hybrid_clock.h"

using std::string;

using strings::Substitute;
using yb::HybridTime;
using yb::util::FormatBytesAsStr;

namespace yb {
namespace docdb {

bool KeyBelongsToDocKeyInTest(const rocksdb::Slice &key, const string &encoded_doc_key) {
  if (key.starts_with(encoded_doc_key)) {
    const int encoded_doc_key_size = encoded_doc_key.size();
    const char* key_data = key.cdata();
    return key.size() >= encoded_doc_key_size + 2 &&
           key_data[encoded_doc_key_size] == '\0' &&
           key_data[encoded_doc_key_size + 1] == '\0';
  } else {
    return false;
  }
}

Status ConsumeHybridTimeFromKey(rocksdb::Slice* slice, DocHybridTime* hybrid_time)  {
  return hybrid_time->DecodeFrom(slice);
}

Status DecodeHybridTimeFromEndOfKey(
    const rocksdb::Slice &key,
    DocHybridTime *dest) {
  return dest->DecodeFromEnd(key);
}

// Given a DocDB key stored in RocksDB, validate the DocHybridTime size stored as the
// last few bits of the final byte of the key, and ensure that the ValueType byte preceding that
// encoded DocHybridTime is ValueType::kHybridTime.
Status CheckHybridTimeSizeAndValueType(
    const rocksdb::Slice& key,
    int* ht_byte_size_dest) {
  RETURN_NOT_OK(
      DocHybridTime::CheckAndGetEncodedSize(key, ht_byte_size_dest));
  const size_t hybrid_time_value_type_offset = key.size() - *ht_byte_size_dest - 1;
  const ValueType value_type = DecodeValueType(key[hybrid_time_value_type_offset]);
  if (value_type != ValueType::kHybridTime) {
    return STATUS_SUBSTITUTE(
        Corruption,
        "Expected to find value type kHybridTime preceding the HybridTime component of the "
            "encoded key, found $0. DocHybridTime bytes: $1",
        ValueTypeToStr(value_type),
        ToShortDebugStr(rocksdb::Slice(key.data() + hybrid_time_value_type_offset,
                                       key.size() - hybrid_time_value_type_offset)));
  }

  return Status::OK();
}

template <char END_OF_STRING>
void AppendEncodedStrToKey(const string &s, string *dest) {
  static_assert(END_OF_STRING == '\0' || END_OF_STRING == '\xff',
                "Only characters '\0' and '\xff' allowed as a template parameter");
  if (END_OF_STRING == '\0' && s.find('\0') == string::npos) {
    // Fast path: no zero characters, nothing to encode.
    dest->append(s);
  } else {
    for (char c : s) {
      if (c == '\0') {
        dest->push_back(END_OF_STRING);
        dest->push_back(END_OF_STRING ^ 1);
      } else {
        dest->push_back(END_OF_STRING ^ c);
      }
    }
  }
}

void AppendZeroEncodedStrToKey(const string &s, string *dest) {
  AppendEncodedStrToKey<'\0'>(s, dest);
}

void AppendComplementZeroEncodedStrToKey(const string &s, string *dest) {
  AppendEncodedStrToKey<'\xff'>(s, dest);
}

template <char A>
inline void TerminateEncodedKeyStr(string *dest) {
  dest->push_back(A);
  dest->push_back(A);
}

void TerminateZeroEncodedKeyStr(string *dest) {
  TerminateEncodedKeyStr<'\0'>(dest);
}

void TerminateComplementZeroEncodedKeyStr(string *dest) {
  TerminateEncodedKeyStr<'\xff'>(dest);
}

template<char END_OF_STRING>
Status DecodeEncodedStr(rocksdb::Slice* slice, string* result) {
  static_assert(END_OF_STRING == '\0' || END_OF_STRING == '\xff',
                "Invalid END_OF_STRING character. Only '\0' and '\xff' accepted");
  constexpr char END_OF_STRING_ESCAPE = END_OF_STRING ^ 1;
  const char* p = slice->cdata();
  const char* end = p + slice->size();

  while (p != end) {
    if (*p == END_OF_STRING) {
      ++p;
      if (p == end) {
        return STATUS(Corruption, StringPrintf("Encoded string ends with only one \\0x%02x ",
                                               END_OF_STRING));
      }
      if (*p == END_OF_STRING) {
        // Found two END_OF_STRING characters, this is the end of the encoded string.
        ++p;
        break;
      }
      if (*p == END_OF_STRING_ESCAPE) {
        // Character END_OF_STRING is encoded as AB.
        if (result != nullptr) {
          result->push_back(END_OF_STRING ^ END_OF_STRING);
        }
        ++p;
      } else {
        return STATUS(Corruption, StringPrintf(
            "Invalid sequence in encoded string: "
            R"#(\0x%02x\0x%02x (must be either \0x%02x\0x%02x or \0x%02x\0x%02x))#",
            END_OF_STRING, *p, END_OF_STRING, END_OF_STRING, END_OF_STRING, END_OF_STRING_ESCAPE));
      }
    } else {
      if (result != nullptr) {
        result->push_back((*p) ^ END_OF_STRING);
      }
      ++p;
    }
  }
  if (result != nullptr) {
    result->shrink_to_fit();
  }
  slice->remove_prefix(p - slice->cdata());
  return Status::OK();
}

Status DecodeComplementZeroEncodedStr(rocksdb::Slice* slice, std::string* result) {
  return DecodeEncodedStr<'\xff'>(slice, result);
}

Status DecodeZeroEncodedStr(rocksdb::Slice* slice, string* result) {
  return DecodeEncodedStr<'\0'>(slice, result);
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

const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const MonoDelta& table_ttl) {
  MonoDelta ttl;
  if (!value_ttl.Equals(Value::kMaxTtl)) {
    ttl = value_ttl.ToMilliseconds() == kResetTTL ? Value::kMaxTtl : value_ttl;
  } else {
    // This is the default.
    ttl = table_ttl;
  }
  return ttl;
}

const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const Schema& schema) {
  return ComputeTTL(value_ttl, TableTTL(schema));
}

}  // namespace docdb
}  // namespace yb

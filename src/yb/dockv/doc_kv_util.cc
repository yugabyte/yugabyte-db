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

#include "yb/dockv/doc_kv_util.h"

#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/value_type.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

using std::string;

namespace yb::dockv {

bool KeyBelongsToDocKeyInTest(const Slice &key, const string &encoded_doc_key) {
  if (key.starts_with(encoded_doc_key)) {
    const auto encoded_doc_key_size = encoded_doc_key.size();
    const char* key_data = key.cdata();
    return key.size() >= encoded_doc_key_size + 2 &&
           key_data[encoded_doc_key_size] == '\0' &&
           key_data[encoded_doc_key_size + 1] == '\0';
  } else {
    return false;
  }
}

// Given a DocDB key stored in RocksDB, validate the DocHybridTime size stored as the
// last few bits of the final byte of the key, and ensure that the ValueType byte preceding that
// encoded DocHybridTime is ValueType::kHybridTime.
Result<size_t> CheckHybridTimeSizeAndValueType(const Slice& key) {
  auto ht_byte_size = VERIFY_RESULT(DocHybridTime::GetEncodedSize(key));
  const size_t hybrid_time_value_type_offset = key.size() - ht_byte_size - 1;
  const auto key_entry_type = DecodeKeyEntryType(key[hybrid_time_value_type_offset]);
  if (key_entry_type != KeyEntryType::kHybridTime) {
    return STATUS_FORMAT(
        Corruption,
        "Expected to find value type kHybridTime preceding the HybridTime component of the "
            "encoded key, found $0. DocHybridTime bytes: $1",
        key_entry_type,
        key.WithoutPrefix(hybrid_time_value_type_offset).ToDebugString());
  }

  return ht_byte_size;
}

namespace {

// Finds a compile-time constant character in string.
// Returns end if the character is not found.
template<char kChar>
const char* Find(const char* begin, const char* end) {
  if (kChar == 0) {
    return begin + strnlen(begin, end - begin);
  }
  auto result = static_cast<const char*>(memchr(begin, kChar, end - begin));
  return result ? result : end;
}

template<char kChar, class Ch>
void Xor(Ch* begin, Ch* end) {
  if (kChar == 0) {
    return;
  }
  for (; begin != end; ++begin) {
    *begin ^= kChar;
  }
}

template<char kChar, class Out>
void Xor(Out* out, size_t old_size) {
  Xor<kChar>(out->data() + old_size, out->data() + out->size());
}

template<char kChar>
void Xor(ValueBuffer* out, size_t old_size) {
  Xor<kChar>(out->mutable_data() + old_size, out->mutable_data() + out->size());
}

template <bool desc>
void AppendEncodedStrToKey(const Slice& s, KeyBuffer *dest) {
  const auto* p = s.cdata();
  const auto* end = s.cend();
  size_t old_size = dest->size();
  for (;;) {
    const auto* stop = Find<'\0'>(p, end);
    if (stop == end) {
      dest->append(p, end);
      break;
    }
    dest->append(p, stop + 1);
    dest->push_back(1);
    p = stop + 1;
  }
  if (desc) {
    Xor<'\xff'>(dest->mutable_data() + old_size, dest->mutable_data() + dest->size());
  }
}

template <char A>
inline void TerminateEncodedKeyStr(KeyBuffer *dest) {
  char buf[2] = {A, A};
  dest->append(buf, sizeof(buf));
}

template<char kEndOfString, class Out>
Result<const char*> DecodeEncodedStr(const char* p, const char* end, Out* result) {
  static_assert(kEndOfString == '\0' || kEndOfString == '\xff',
                "Invalid kEndOfString character. Only '\0' and '\xff' accepted");
  constexpr char kEndOfStringEscape = kEndOfString ^ 1;
  auto old_size = result->size();

  // Loop invariant: remaining bytes to be processed are [p, end)
  do {
    auto stop = Find<kEndOfString>(p, end);
    if (PREDICT_FALSE(stop >= end - 1)) {
      if (stop == end) {
        if (p == end) {
          return STATUS(Corruption, "Encoded string is empty");
        }
        return STATUS(
            Corruption, StringPrintf(
                            "Encoded string is not terminated with \\0x%02x\\0x%02x", kEndOfString,
                            kEndOfString));
      }
      DCHECK_EQ(stop, end - 1);
      return STATUS(
          Corruption, StringPrintf("Encoded string ends with only one \\0x%02x ", kEndOfString));
    }
    if (PREDICT_TRUE(stop[1] == kEndOfString)) {
      result->append(p, stop);
      p = stop + 2;
      break;
    }
    if (PREDICT_FALSE(stop[1] != kEndOfStringEscape)) {
      return STATUS(
          Corruption,
          StringPrintf(
              "Invalid sequence in encoded string: "
              R"#(\0x%02x\0x%02x (must be either \0x%02x\0x%02x or \0x%02x\0x%02x))#",
              kEndOfString, stop[1], kEndOfString, kEndOfString, kEndOfString, kEndOfStringEscape));
    }
    result->append(p, stop + 1);
    p = stop + 2;
  } while (p != end);
  Xor<kEndOfString>(result, old_size);
  return p;
}

template<char kEndOfString, class Out>
Status DecodeEncodedStr(Slice* slice, Out* result) {
  const auto* begin = VERIFY_RESULT(DecodeEncodedStr<kEndOfString>(
     slice->cdata(), slice->cend(), result));
  *slice = Slice(begin, slice->cend());
  return Status::OK();
}

struct DevNull {
  void append(const char* begin, const char* end) {
  }

  constexpr size_t size() const {
    return 0;
  }

  char* data() const {
    return nullptr;
  }
};

template <class Decoder>
Result<std::string> FullyDecodeString(const Slice& encoded_str, const Decoder& decoder) {
  std::string result;
  result.reserve(encoded_str.size());
  Slice slice(encoded_str);
  RETURN_NOT_OK(decoder(&slice, &result));
  if (!slice.empty()) {
    return STATUS_FORMAT(
        Corruption,
        "Did not consume all characters from a zero-encoded string $0: bytes left: $1",
        FormatBytesAsStr(encoded_str), slice.size());
  }
  return result;
}

} // namespace

void AppendZeroEncodedStrToKey(const Slice& s, KeyBuffer *dest) {
  AppendEncodedStrToKey<false>(s, dest);
}

void AppendComplementZeroEncodedStrToKey(const Slice& s, KeyBuffer *dest) {
  AppendEncodedStrToKey<true>(s, dest);
}

void TerminateZeroEncodedKeyStr(KeyBuffer *dest) {
  TerminateEncodedKeyStr<'\0'>(dest);
}

void TerminateComplementZeroEncodedKeyStr(KeyBuffer *dest) {
  TerminateEncodedKeyStr<'\xff'>(dest);
}

Status DecodeComplementZeroEncodedStr(Slice* slice, std::string* result) {
  if (result == nullptr) {
    DevNull dev_null;
    return DecodeEncodedStr<'\xff'>(slice, &dev_null);
  }
  return DecodeEncodedStr<'\xff'>(slice, result);
}

Result<std::string> DecodeComplementZeroEncodedStr(const Slice& encoded_str) {
  return FullyDecodeString(encoded_str, [](Slice* slice, std::string* result) {
    return DecodeComplementZeroEncodedStr(slice, result);
  });
}

Status DecodeZeroEncodedStr(Slice* slice, std::string* result) {
  if (result == nullptr) {
    DevNull dev_null;
    return DecodeEncodedStr<'\0'>(slice, &dev_null);
  }
  return DecodeEncodedStr<'\0'>(slice, result);
}

Result<const char*> DecodeZeroEncodedStr(const char* begin, const char* end, ValueBuffer* out) {
  return DecodeEncodedStr<'\0'>(begin, end, out);
}

Result<const char*> SkipZeroEncodedStr(const char* begin, const char* end) {
  DevNull dev_null;
  return DecodeEncodedStr<'\0'>(begin, end, &dev_null);
}

Result<const char*> DecodeComplementZeroEncodedStr(
    const char* begin, const char* end, ValueBuffer* out) {
  return DecodeEncodedStr<'\xff'>(begin, end, out);
}

Result<const char*> SkipComplementZeroEncodedStr(const char* begin, const char* end) {
  DevNull dev_null;
  return DecodeEncodedStr<'\xff'>(begin, end, &dev_null);
}

Result<std::string> DecodeZeroEncodedStr(const Slice& encoded_str) {
  return FullyDecodeString(encoded_str, [](Slice* slice, std::string* result) {
    return DecodeZeroEncodedStr(slice, result);
  });
}

std::string ToShortDebugStr(Slice slice) {
  return FormatSliceAsStr(slice, QuotesType::kDoubleQuotes, kShortDebugStringLength);
}

Result<DocHybridTime> DecodeInvertedDocHt(Slice key_slice) {
  if (key_slice.empty() || key_slice.size() > kMaxBytesPerEncodedHybridTime + 1) {
    return STATUS_FORMAT(
        Corruption,
        "Invalid doc hybrid time in reverse intent record suffix: $0",
        key_slice.ToDebugHexString());
  }

  DocHybridTimeWordBuffer doc_ht_buffer;
  key_slice = InvertEncodedDocHT(key_slice, &doc_ht_buffer);

  if (!key_slice.TryConsumeByte(KeyEntryTypeAsChar::kHybridTime)) {
    return STATUS_FORMAT(
        Corruption,
        "Invalid prefix of doc hybrid time in reverse intent record decoded suffix: $0",
        key_slice.ToDebugHexString());
  }
  return DocHybridTime::DecodeFrom(&key_slice);
}

Slice InvertEncodedDocHT(const Slice& input, DocHybridTimeWordBuffer* buffer) {
  memcpy(buffer->data(), input.data(), input.size());
  for (size_t i = 0; i != kMaxWordsPerEncodedHybridTimeWithValueType; ++i) {
    (*buffer)[i] = ~(*buffer)[i];
  }
  return {pointer_cast<char*>(buffer->data()), input.size()};
}

}  // namespace yb::dockv

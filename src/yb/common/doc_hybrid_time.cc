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

#include "yb/common/doc_hybrid_time.h"

#include "yb/gutil/casts.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/cast.h"
#include "yb/util/checked_narrow_cast.h"
#include "yb/util/debug-util.h"
#include "yb/util/fast_varint.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/varint.h"

using std::string;

using yb::FastEncodeDescendingSignedVarInt;
using yb::FastDecodeDescendingSignedVarIntUnsafe;

using strings::SubstituteAndAppend;

namespace yb {

// It does not really matter what write id we use here. We determine DocHybridTime validity based
// on its HybridTime component's validity. However, given that HybridTime::kInvalid is close to the
// highest possible value of the underlying in-memory representation of HybridTime, we use
// kMaxWriteId for the write id portion of this constant for consistency.
const DocHybridTime DocHybridTime::kInvalid = DocHybridTime(HybridTime::kInvalid, kMaxWriteId);

const DocHybridTime DocHybridTime::kMin = DocHybridTime(HybridTime::kMin, 0);
const DocHybridTime DocHybridTime::kMax = DocHybridTime(HybridTime::kMax, kMaxWriteId);

const EncodedDocHybridTime kEncodedDocHybridTimeMin{DocHybridTime::kMin};

const Slice EncodedDocHybridTime::kMin = kEncodedDocHybridTimeMin.AsSlice();

constexpr int kNumBitsForHybridTimeSize = 5;
constexpr int kHybridTimeSizeMask = (1 << kNumBitsForHybridTimeSize) - 1;

char* DocHybridTime::EncodedInDocDbFormat(char* dest) const {
  // We compute the difference between the physical time as microseconds since the UNIX epoch and
  // the "YugaByte epoch" as a signed operation, so that we can still represent hybrid times earlier
  // than the YugaByte epoch.
  char* out = dest;

  // Hybrid time generation number. This is currently always 0. In the future this can be used to
  // reset hybrid time throughout the entire cluster back to a lower value if it gets stuck at some
  // far-in-the-future point due to a temporary clock issue.
  out = FastEncodeDescendingSignedVarInt(0, out);

  out = FastEncodeDescendingSignedVarInt(
      static_cast<int64_t>(hybrid_time_.GetPhysicalValueMicros() - kYugaByteMicrosecondEpoch),
      out);
  out = FastEncodeDescendingSignedVarInt(hybrid_time_.GetLogicalValue(), out);

  // We add one to write_id to ensure the negated value used in the encoding is always negative
  // (i.e. is never zero).  Then we shift it left by kNumBitsForHybridTimeSize bits so that we
  // always have kNumBitsForHybridTimeSize lowest bits to store the encoded size. This way we can
  // also decode the VarInt, negate it, obtain an always-positive value, and look at the lowest
  // kNumBitsForHybridTimeSize bits to get the encoded size of the entire DocHybridTime.
  //
  // It is important that we cast to int64_t before adding 1, otherwise WriteId might overflow.
  // (As of 04/17/2017 we're using a 32-bit unsigned int for WriteId).
  out = FastEncodeDescendingSignedVarInt(
      (static_cast<int64_t>(write_id_) + 1) << kNumBitsForHybridTimeSize, out);

  // Store the encoded DocHybridTime size in the last kNumBitsForHybridTimeSize bits so we
  // can decode the hybrid time from the end of an encoded DocKey efficiently.
  const uint8_t last_byte = static_cast<uint8_t>(out[-1]);

  const uint8_t encoded_size = static_cast<uint8_t>(out - dest);
  DCHECK_LE(1, encoded_size);
  DCHECK_LE(encoded_size, kMaxBytesPerEncodedHybridTime);
  out[-1] = static_cast<char>((last_byte & ~kHybridTimeSizeMask) | encoded_size);
  return out;
}

Result<Slice> DocHybridTime::EncodedFromStart(Slice* slice) {
  const auto* begin = slice->cdata();
  const char* mid = VERIFY_RESULT(EncodedFromStart(begin, slice->cend()));
  *slice = Slice(mid, slice->cend());
  return Slice(begin, mid);
}

Result<const char*> DocHybridTime::EncodedFromStart(const char* begin, const char* end) {
  const char* start = begin;
  // There are following components:
  // 1) Generation number - not used always 0.
  // 2) Physical part of hybrid time.
  // 3) Logical part of hybrid time.
  // 4) Write id.
  for (size_t i = 0; i != 4; ++i) {
    auto size = FastDecodeDescendingSignedVarIntSize(Slice(begin, end));
    if (size == 0 || begin + size > end) {
      return STATUS_FORMAT(
          Corruption, "Bad doc hybrid time: $0, step: $1",
          Slice(start, end).ToDebugHexString(), i);
    }
    begin += size;
  }
  return begin;
}

Result<DocHybridTime> DocHybridTime::DecodeFrom(Slice *slice) {
  DocHybridTime result;
  const size_t previous_size = slice->size();
  {
    // Currently we just ignore the generation number as it should always be 0.
    RETURN_NOT_OK(FastDecodeDescendingSignedVarIntUnsafe(slice));
    int64_t decoded_micros =
        kYugaByteMicrosecondEpoch + VERIFY_RESULT(FastDecodeDescendingSignedVarIntUnsafe(slice));

    auto decoded_logical = VERIFY_RESULT(checked_narrow_cast<LogicalTimeComponent>(
        VERIFY_RESULT(FastDecodeDescendingSignedVarIntUnsafe(slice))));

    result.hybrid_time_ = HybridTime::FromMicrosecondsAndLogicalValue(
        decoded_micros, decoded_logical);
  }

  const auto ptr_before_decoding_write_id = slice->data();
  int64_t decoded_shifted_write_id = VERIFY_RESULT(FastDecodeDescendingSignedVarIntUnsafe(slice));

  if (decoded_shifted_write_id < 0) {
    return STATUS_SUBSTITUTE(
        Corruption,
        "Negative decoded_shifted_write_id: $0. Was trying to decode from: $1",
        decoded_shifted_write_id,
        Slice(ptr_before_decoding_write_id,
              slice->data() + slice->size() - ptr_before_decoding_write_id).ToDebugHexString());
  }
  result.write_id_ = VERIFY_RESULT(checked_narrow_cast<IntraTxnWriteId>(
      (decoded_shifted_write_id >> kNumBitsForHybridTimeSize) - 1));

  const size_t bytes_decoded = previous_size - slice->size();
  const size_t size_at_the_end = (*(slice->data() - 1)) & kHybridTimeSizeMask;
  if (size_at_the_end != bytes_decoded) {
    return STATUS_SUBSTITUTE(
        Corruption,
        "Wrong encoded DocHybridTime size at the end: $0. Expected: $1. "
            "Encoded timestamp: $2.",
        size_at_the_end,
        bytes_decoded,
        Slice(to_char_ptr(slice->data() - bytes_decoded), bytes_decoded).ToDebugHexString());
  }

  return result;
}

Result<DocHybridTime> DocHybridTime::FullyDecodeFrom(const Slice& encoded) {
  Slice s = encoded;
  auto result = DecodeFrom(&s);
  if (result.ok() && !s.empty()) {
    return STATUS_SUBSTITUTE(
        Corruption,
        "$0 extra bytes left when decoding a DocHybridTime $1",
        s.size(), FormatSliceAsStr(encoded, QuotesType::kDoubleQuotes, /* max_length = */ 32));
  }
  return result;
}

Result<DocHybridTime> DocHybridTime::DecodeFromEnd(Slice* encoded_key_with_ht_at_end) {
  size_t encoded_size = VERIFY_RESULT(GetEncodedSize(*encoded_key_with_ht_at_end));
  Slice s = encoded_key_with_ht_at_end->Suffix(encoded_size);
  DocHybridTime result = VERIFY_RESULT(FullyDecodeFrom(s));
  encoded_key_with_ht_at_end->remove_suffix(encoded_size);
  return result;
}

Result<DocHybridTime> DocHybridTime::DecodeFromEnd(Slice encoded_key_with_ht_at_end) {
  return DecodeFromEnd(&encoded_key_with_ht_at_end);
}

string DocHybridTime::ToString() const {
  if (write_id_ == 0) {
    return hybrid_time_.ToDebugString();
  }

  string s = hybrid_time_.ToDebugString();
  if (s[s.length() - 1] == '}') {
    s.resize(s.length() - 2);
  } else {
    s.insert(2, "{ ");
  }
  if (write_id_ == kMaxWriteId) {
    s += " w: Max }";
  } else {
    SubstituteAndAppend(&s, " w: $0 }", write_id_);
  }
  return s;
}

// Decode and verify that the decoded DocHybridTime (encoded_ht_size) is at least
// 1 and is strictly less than the size of the whole key (encoded_key_size). The latter strict
// inequality is because we must also leave room for a ValueType::kHybridTime. In practice,
// the preceding DocKey will also take a non-zero number of bytes.
Result<size_t> DocHybridTime::GetEncodedSize(const Slice& encoded_key) {
  auto encoded_key_size = encoded_key.size();
  if (encoded_key_size == 0) {
    return STATUS(RuntimeError,
                  "Got an empty encoded key when looking for a DocHybridTime at the end.");
  }

  size_t result = static_cast<uint8_t>(encoded_key.end()[-1]) & kHybridTimeSizeMask;

  SCHECK_GE(result,
            1U,
            Corruption,
            Format("Encoded HybridTime must be at least one byte, found $0", result));

  SCHECK_LE(result,
            kMaxBytesPerEncodedHybridTime,
            Corruption,
            Format("Encoded HybridTime can't be more than $0 bytes, found $1",
                   kMaxBytesPerEncodedHybridTime, result));


  SCHECK_LT(result,
            encoded_key_size,
            Corruption,
            Format(
                "Trying to extract an encoded HybridTime with a size of $0 bytes from "
                    "an encoded key of length $1 bytes (must be strictly less -- one byte is "
                    "used for value type)",
                result, encoded_key_size));

  return result;
}

Status DocHybridTime::EncodedFromEnd(const Slice& slice, EncodedDocHybridTime* out) {
  auto size = VERIFY_RESULT(GetEncodedSize(slice));
  out->Assign(slice.Suffix(size));
  return Status::OK();
}

std::string DocHybridTime::DebugSliceToString(Slice input) {
  auto temp = FullyDecodeFrom(input);
  if (!temp.ok()) {
    LOG(WARNING) << "Failed to decode DocHybridTime: " << temp.status();
    return input.ToDebugHexString();
  }
  return temp->ToString();
}

const EncodedDocHybridTime& DocHybridTime::EncodedMin() {
  static const EncodedDocHybridTime result(kMin);
  return result;
}

EncodedDocHybridTime::EncodedDocHybridTime(const DocHybridTime& input)
    : size_(input.EncodedInDocDbFormat(buffer_.data()) - buffer_.data()) {
}

EncodedDocHybridTime::EncodedDocHybridTime(HybridTime ht, IntraTxnWriteId write_id)
    : EncodedDocHybridTime(DocHybridTime(ht, write_id)) {}

EncodedDocHybridTime::EncodedDocHybridTime(const Slice& src)
    : size_(src.size()) {
  memcpy(buffer_.data(), src.data(), src.size());
}

void EncodedDocHybridTime::Assign(const Slice& input) {
  size_ = input.size();
  memcpy(buffer_.data(), input.data(), size_);
}

void EncodedDocHybridTime::Assign(const DocHybridTime& doc_ht) {
  size_ = doc_ht.EncodedInDocDbFormat(buffer_.data()) - buffer_.data();
}

void EncodedDocHybridTime::Reset() {
  size_ = 0;
}

std::string EncodedDocHybridTime::ToString() const {
  return DocHybridTime::FullyDecodeFrom(AsSlice()).ToString();
}

Result<DocHybridTime> EncodedDocHybridTime::Decode() const {
  return DocHybridTime::FullyDecodeFrom(AsSlice());
}

void EncodedDocHybridTime::MakeAtLeast(const EncodedDocHybridTime& rhs) {
  if (*this >= rhs) {
    return;
  }
  Assign(rhs.AsSlice());
}

}  // namespace yb

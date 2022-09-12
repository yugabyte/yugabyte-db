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

using yb::util::VarInt;
using yb::util::FastEncodeDescendingSignedVarInt;
using yb::util::FastDecodeDescendingSignedVarIntUnsafe;
using yb::FormatBytesAsStr;
using yb::FormatSliceAsStr;
using yb::QuotesType;

using strings::Substitute;
using strings::SubstituteAndAppend;

namespace yb {

// It does not really matter what write id we use here. We determine DocHybridTime validity based
// on its HybridTime component's validity. However, given that HybridTime::kInvalid is close to the
// highest possible value of the underlying in-memory representation of HybridTime, we use
// kMaxWriteId for the write id portion of this constant for consistency.
const DocHybridTime DocHybridTime::kInvalid = DocHybridTime(HybridTime::kInvalid, kMaxWriteId);

const DocHybridTime DocHybridTime::kMin = DocHybridTime(HybridTime::kMin, 0);
const DocHybridTime DocHybridTime::kMax = DocHybridTime(HybridTime::kMax, kMaxWriteId);

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

    result.hybrid_time_ =
        HybridTime::FromMicrosecondsAndLogicalValue(decoded_micros, decoded_logical);
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
  size_t encoded_size = 0;
  RETURN_NOT_OK(CheckAndGetEncodedSize(*encoded_key_with_ht_at_end, &encoded_size));
  Slice s(encoded_key_with_ht_at_end->end() - encoded_size, encoded_size);
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

Status DocHybridTime::CheckEncodedSize(size_t encoded_ht_size, size_t encoded_key_size) {
  if (encoded_key_size == 0) {
    return STATUS(RuntimeError,
                  "Got an empty encoded key when looking for a DocHybridTime at the end.");
  }

  SCHECK_GE(encoded_ht_size,
            1U,
            Corruption,
            Substitute("Encoded HybridTime must be at least one byte, found $0.", encoded_ht_size));

  SCHECK_LE(encoded_ht_size,
            kMaxBytesPerEncodedHybridTime,
            Corruption,
            Substitute("Encoded HybridTime can't be more than $0 bytes, found $1.",
                       kMaxBytesPerEncodedHybridTime, encoded_ht_size));


  SCHECK_LT(encoded_ht_size,
            encoded_key_size,
            Corruption,
            Substitute(
                "Trying to extract an encoded HybridTime with a size of $0 bytes from "
                    "an encoded key of length $1 bytes (must be strictly less -- one byte is "
                    "used for value type).",
                encoded_ht_size, encoded_key_size));

  return Status::OK();
}

int DocHybridTime::GetEncodedSize(const Slice& encoded_key) {
  // We are not checking for errors here -- see CheckEncodedSize for that. We return something
  // even for a zero-size slice.
  return encoded_key.empty() ? 0
      : static_cast<uint8_t>(encoded_key.end()[-1]) & kHybridTimeSizeMask;
}

Status DocHybridTime::CheckAndGetEncodedSize(
    const Slice& encoded_key, size_t* encoded_ht_size) {
  *encoded_ht_size = GetEncodedSize(encoded_key);
  return CheckEncodedSize(*encoded_ht_size, encoded_key.size());
}

std::string DocHybridTime::DebugSliceToString(Slice input) {
  auto temp = FullyDecodeFrom(input);
  if (!temp.ok()) {
    LOG(WARNING) << "Failed to decode DocHybridTime: " << temp.status();
    return input.ToDebugHexString();
  }
  return temp->ToString();
}

}  // namespace yb

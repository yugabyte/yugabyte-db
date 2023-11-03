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

#include "yb/util/varint.h"

#include <openssl/bn.h>

#include "yb/gutil/casts.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

using std::ostream;

namespace yb {

void BigNumDeleter::operator()(BIGNUM* bn) const {
  BN_free(bn);
}

ATTRIBUTE_NO_SANITIZE_UNDEFINED VarInt::VarInt(int64_t int64_val) : impl_(BN_new()) {
  if (int64_val >= 0) {
    BN_set_word(impl_.get(), int64_val);
  } else {
    BN_set_word(impl_.get(), -int64_val);
    BN_set_negative(impl_.get(), 1);
  }
}

VarInt::VarInt() : impl_(BN_new()) {}

VarInt::VarInt(const VarInt& var_int) : impl_(BN_dup(var_int.impl_.get())) {}

VarInt& VarInt::operator=(const VarInt& rhs) {
  impl_.reset(BN_dup(rhs.impl_.get()));
  return *this;
}

std::string VarInt::ToString() const {
  char* temp = BN_bn2dec(impl_.get());
  std::string result(temp);
  OPENSSL_free(temp);
  return result;
}

Result<int64_t> VarInt::ToInt64() const {
  BN_ULONG value = BN_get_word(impl_.get());
  bool negative = BN_is_negative(impl_.get());
  // Casting minimal signed value to unsigned type of the same size returns its absolute value.
  BN_ULONG bound = negative ? std::numeric_limits<int64_t>::min()
                            : std::numeric_limits<int64_t>::max();

  if (value > bound) {
    return STATUS_FORMAT(
        InvalidArgument, "VarInt $0 cannot be converted to int64 due to overflow", *this);
  }
  return negative ? -value : value;
}

Status VarInt::FromString(const std::string& str) {
  return FromString(str.c_str());
}

Status VarInt::FromString(const char* cstr) {
  if (*cstr == '+') {
    ++cstr;
  }
  BIGNUM* temp = nullptr;
  size_t parsed = BN_dec2bn(&temp, cstr);
  impl_.reset(temp);
  if (parsed == 0 || parsed != strlen(cstr)) {
    return STATUS_FORMAT(InvalidArgument, "Cannot parse varint: $0", cstr);
  }
  return Status::OK();
}

int VarInt::CompareTo(const VarInt& other) const {
  return BN_cmp(impl_.get(), other.impl_.get());
}

template <class T>
void FlipBits(T* data, size_t count) {
  for (auto end = data + count; data != end; ++data) {
    *data = ~*data;
  }
}

constexpr auto kWordSize = sizeof(uint8_t);
constexpr auto kBitsPerWord = 8 * kWordSize;

std::string VarInt::EncodeToComparable(size_t num_reserved_bits) const {
  // Actually we use EncodeToComparable with num_reserved_bits equals to 0 or 2, so don't have
  // to handle complex case when it wraps byte.
  DCHECK_LT(num_reserved_bits, 8);

  if (BN_is_zero(impl_.get())) {
    // Zero is encoded as positive value of length 0.
    return std::string(1, 0x80 >> num_reserved_bits);
  }

  auto num_bits = BN_num_bits(impl_.get());
  // The minimal number of bits that is required to encode this number:
  // sign bit, bits in value representation and reserved bits.
  size_t total_num_bits = num_bits + 1 + num_reserved_bits;

  // Number of groups of 7 becomes number of bytes, because of the unary size prefix.
  size_t num_bytes = (total_num_bits + 6) / 7;
  // If total_num_bits is not a multiple of seven, then the numerical part is prefixed with zeros.
  size_t rounding_padding = num_bytes * 7 - total_num_bits;
  // Header consists of sign bit, then number of ones that equals to num_bytes and padding zeroes.
  size_t header_length = 1 + num_bytes + rounding_padding;

  // Number of words required to encode this, i.e. header and bits in value rounded up to word size.
  auto words_count = (num_bits + header_length + kBitsPerWord - 1) / kBitsPerWord;
  // Offset of the first byte that would contain number representation.
  auto offset = words_count - (num_bits + kBitsPerWord - 1) / kBitsPerWord;
  std::string result(words_count, 0);
  if (offset > 0) {
    // This value could be merged with ones, so should cleanup it.
    result[offset - 1] = 0;
  }
  auto data = pointer_cast<unsigned char*>(const_cast<char*>(result.data()));
  BN_bn2bin(impl_.get(), data + offset);
  size_t idx = 0;
  // Fill header with ones. We also fill reserved bits with ones for simplicity, then it will be
  // reset to zero.
  size_t num_ones = num_bytes + num_reserved_bits;
  // Fill complete bytes with ones.
  while (num_ones > 8) {
    data[idx] = 0xff;
    num_ones -= 8;
    ++idx;
  }
  // Merge last byte of header with possible first byte of number body.
  data[idx] |= 0xff ^ ((1 << (8 - num_ones)) - 1);
  // Negative number is inverted.
  if (BN_is_negative(impl_.get())) {
    FlipBits(data, result.size());
  }
  // Set reserved bits to 0.
  if (num_reserved_bits) {
    data[0] &= (1 << (8 - num_reserved_bits)) - 1;
  }

  return result;
}

Status VarInt::DecodeFromComparable(const Slice &slice, size_t *num_decoded_bytes,
                                    size_t num_reserved_bits) {
  DCHECK_LT(num_reserved_bits, 8);

  size_t len = slice.size();
  if (len == 0) {
    return STATUS(Corruption, "Cannot decode varint from empty slice");
  }
  bool negative = (slice[0] & (0x80 >> num_reserved_bits)) == 0;
  std::vector<uint8_t> buffer(slice.data(), slice.end());
  if (negative) {
    FlipBits(buffer.data(), buffer.size());
  }
  if (num_reserved_bits) {
    buffer[0] |= ~((1 << (8 - num_reserved_bits)) - 1);
  }
  size_t idx = 0;
  size_t num_ones = 0;
  while (buffer[idx] == 0xff) {
    ++idx;
    if (idx >= len) {
      return STATUS_FORMAT(
          Corruption, "Encoded varint failure, no prefix termination: $0",
          slice.ToDebugHexString());
    }
    num_ones += 8;
  }
  uint8_t temp = 0x80;
  while (buffer[idx] & temp) {
    buffer[idx] ^= temp;
    ++num_ones;
    temp >>= 1;
  }
  num_ones -= num_reserved_bits;
  if (num_ones > len) {
    return STATUS_FORMAT(
        Corruption, "Not enough data in encoded varint: $0, $1",
        slice.ToDebugHexString(), num_ones);
  }
  *num_decoded_bytes = num_ones;
  impl_.reset(BN_bin2bn(buffer.data() + idx, narrow_cast<int>(num_ones - idx), nullptr /* ret */));
  if (negative) {
    BN_set_negative(impl_.get(), 1);
  }

  return Status::OK();
}

Status VarInt::DecodeFromComparable(const Slice& slice) {
  size_t num_decoded_bytes;
  return DecodeFromComparable(slice, &num_decoded_bytes);
}

std::string VarInt::EncodeToTwosComplement() const {
  if (BN_is_zero(impl_.get())) {
    return std::string(1, 0);
  }
  size_t num_bits = BN_num_bits(impl_.get());
  size_t offset = num_bits % kBitsPerWord == 0 ? 1 : 0;
  size_t count = (num_bits + kBitsPerWord - 1) / kBitsPerWord + offset;
  std::string result(count, 0);
  auto data = pointer_cast<unsigned char*>(const_cast<char*>(result.data()));
  if (offset) {
    *data = 0;
  }
  BN_bn2bin(impl_.get(), data + offset);
  if (BN_is_negative(impl_.get())) {
    FlipBits(data, result.size());
    unsigned char* back = data + result.size();
    while (--back >= data && *back == 0xff) {
      *back = 0;
    }
    ++*back;
    // The case when number has form of 10000000 00000000 ...
    // It is easier to detect it at this point, instead of doing preallocation check.
    if (offset == 1 && back > data && (data[1] & 0x80)) {
      result.erase(0, 1);
    }
  }
  return result;
}

Status VarInt::DecodeFromTwosComplement(const std::string& input) {
  if (input.size() == 0) {
    return STATUS(Corruption, "Cannot decode varint from empty slice");
  }
  bool negative = (input[0] & 0x80) != 0;
  if (!negative) {
    impl_.reset(BN_bin2bn(
        pointer_cast<const unsigned char*>(input.data()), narrow_cast<int>(input.size()),
        nullptr /* ret */));
    return Status::OK();
  }
  std::string copy(input);
  auto data = pointer_cast<unsigned char*>(const_cast<char*>(copy.data()));
  unsigned char* back = data + copy.size();
  while (--back >= data && *back == 0x00) {
    *back = 0xff;
  }
  --*back;
  FlipBits(data, copy.size());
  impl_.reset(BN_bin2bn(data, narrow_cast<int>(input.size()), nullptr /* ret */));
  BN_set_negative(impl_.get(), 1);

  return Status::OK();
}

const VarInt& VarInt::Negate() {
  BN_set_negative(impl_.get(), 1 - BN_is_negative(impl_.get()));
  return *this;
}

int VarInt::Sign() const {
  if (BN_is_zero(impl_.get())) {
    return 0;
  }
  return BN_is_negative(impl_.get()) ? -1 : 1;
}

Result<VarInt> VarInt::CreateFromString(const std::string& input) {
  VarInt result;
  RETURN_NOT_OK(result.FromString(input));
  return result;
}

Result<VarInt> VarInt::CreateFromString(const char* input) {
  VarInt result;
  RETURN_NOT_OK(result.FromString(input));
  return result;
}

std::ostream& operator<<(ostream& os, const VarInt& v) {
  os << v.ToString();
  return os;
}

VarInt operator+(const VarInt& lhs, const VarInt& rhs) {
  BigNumPtr temp(BN_new());
  CHECK(BN_add(temp.get(), lhs.impl_.get(), rhs.impl_.get()));
  return VarInt(std::move(temp));
}

VarInt operator-(const VarInt& lhs, const VarInt& rhs) {
  BigNumPtr temp(BN_new());
  CHECK(BN_sub(temp.get(), lhs.impl_.get(), rhs.impl_.get()));
  return VarInt(std::move(temp));
}

} // namespace yb

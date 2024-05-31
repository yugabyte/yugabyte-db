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

#pragma once

#include <openssl/ossl_typ.h>

#include "yb/util/slice.h"

namespace yb {

class BigNumDeleter {
 public:
  void operator()(BIGNUM* bn) const;
};

typedef std::unique_ptr<BIGNUM, BigNumDeleter> BigNumPtr;

// VarInt holds a sequence of digits d1, d2 ... and a radix r.
// The digits should always to be in range 0 <= d < r, and satisfy d_n-1 > 0, 0 < r < 2^15
// (So that int multiplication never overflows).
// The representation is little endian: The underlying number is d1 + d2 * r + d3 * r^2 + ...
//
// The provided EncodeToComparable() function gives a byte sequence from which the size can be
// decoded, and it is guaranteed that a < b if and only if Enc(a) < Enc(b) in lexicographical byte
// order.
//
// Two other ways to encode VarInt are also provided, which are used for Decimal encodings.

// For signed int this metafunction returns int64_t, for unsigned - uint64_t.
template<class T>
class CoveringInt {
 public:
  typedef typename std::decay<T>::type CleanedT;
  typedef typename std::conditional<std::is_signed<CleanedT>::value, int64_t, uint64_t>::type type;
};

class VarInt {
 public:
  VarInt();
  VarInt(const VarInt& var_int);
  VarInt(VarInt&& rhs) = default;
  explicit VarInt(int64_t int64_val);

  VarInt& operator=(const VarInt& rhs);
  VarInt& operator=(VarInt&& rhs) = default;

  std::string ToString() const;

  Result<int64_t> ToInt64() const;

  // The input is expected to be of the form (-)?[0-9]+, whitespace is not allowed. Use this
  // after removing whitespace.
  Status FromString(const std::string& input);
  Status FromString(const char* cstr);

  static Result<VarInt> CreateFromString(const std::string& input);

  static Result<VarInt> CreateFromString(const char* input);

  // <0, =0, >0 if this <,=,> other numerically.
  int CompareTo(const VarInt& other) const;

  // Note: operator== is numerical comparison, and can yield equality between different bases.
  bool operator==(const VarInt& other) const { return CompareTo(other) == 0; }
  bool operator!=(const VarInt& other) const { return CompareTo(other) != 0; }
  bool operator<(const VarInt& other) const { return CompareTo(other) < 0; }
  bool operator<=(const VarInt& other) const { return CompareTo(other) <= 0; }
  bool operator>(const VarInt& other) const { return CompareTo(other) > 0; }
  bool operator>=(const VarInt& other) const { return CompareTo(other) >= 0; }

  /**
   * (1) Encoding algorithm for unsigned varint (with no reserved bits):
   * ---------------------------------------------------------------------------
   *
   * - Convert the number to base 128 (Or look at binary representation in groups of 7)
   * - Prefix the representation by number of digits in base 128
   * (Or number of binary digits / 7) - 1 in unary format followed by a zero as delimiter
   * unary format is a bunch of 1's (eg 5 = 11111)
   *
   * Bytes  Max Value            Representation
   * -----  ---------            ---------
   * 1      2^7-1 (127)          0[v]
   * 2      2^14-1 (16383)       10[v]
   * 3      2^21-1               110[v]
   * ...
   * 8      2^56-1               11111110[v]
   * 9      2^63-1               111111110[v]
   * ...
   *
   * [v] denotes the binary representation of v.
   *
   * (2) Encoding of Signed VarInt:
   * ---------------------------------------------------------------------------
   *
   * - First bit is sign bit (0 for negative, 1 for positive)
   * - The rest is the unsigned representation of the absolute value, except for 2 differences:
   *    > The size prefix is (|v|+1)/7 instead of |v|/7 (Where |v| is the bit length of v)
   *    > For negative numbers, we have to complement everything (Note that we don't add 1, this
   *        is one's complement, not two's complement. So comp(v) = 2^|v|-1-v, not 2^|v|-v)
   *
   *    Bytes  Max Magnitude        Positives       Negatives
   *    -----  ---------            ---------        --------
   *    1      2^6-1 (63)           10[v]            01{-v}
   *    2      2^13-1 (8191)        110[v]           001{-v}
   *    3      2^20-1               1110[v]          0001{-v}
   *    ...
   *
   * {v} denotes the bitwise negation of the binary representation of v.
   *
   * - Note: Negative zero technically has a different encoding than positive zero:
   *         01111111 vs 10000000, But we forcibly canonicalize it to 10000000.
   *
   * (3) Encoding with reserved bits:
   * ---------------------------------------------------------------------------
   *
   * Above specs assume that the encoded form is integer number of bytes long, but for
   * decimal implementation, we only have 7+8k bits for the exponent varint since the first bit
   * is used up by the global sign bit. Hence we need a way to reserve some bits in the beginning
   * of the encoding,
   *
   * This is done by computing the size prefix as (|v|+1 + reserved_bits)/7. This way, we would have
   * some guaranteed bits in the beginning of [v], we can move those to the beginning of the
   * encoding without harming comparison.
   *
   * (*) Anatomy of the final encoding (before complement)
   *
   * [reserved bits] [sign bit] [unary size prefix (bunch of ones)] [delimeter (one zero)] [binary]
   *
   * Example -1000 with 3 reserved bits:
   * Before complement:
   * [000][0][11][0][00000001111101000]
   * After complement:
   * [000][0][00][1][11111110000010111]
   *
   * In practice, we first encode as [111][0][11][0][00000001111101000]
   */


  // is_signed and reserved_bits are parameters, Decode and Encode are inverse operations
  // if and only if parameters are the same.
  // num_reserved_bits = 5 ensures that first 5 bits of the encoded form are zero.
  // This is a comparable encoding.
  //
  // In the above description of the encode algorithms,
  // section 1 corresponds to is_signed = false, num_reserved_bits = 0, section 2 corresponds to
  // is_Signed = true, num_reserved_bits = 0, and section 3 addresses the general case with
  // num_reserved_bits > 0.
  // Note that the first <num_reserved_bits> bits of the encoding is guaranteed to be zero.
  std::string EncodeToComparable(size_t num_reserved_bits = 0) const;

  // Convert the number to base 256 and encode each digit as a byte from high order to low order.
  // If negative x, encode 2^(8t) + x for the smallest value of t that ensures first bit is one.
  // If num_bytes is -1 then choose it based on number of digits in base 256
  Status DecodeFromComparable(
      const Slice& slice, size_t *num_decoded_bytes, size_t num_reserved_bits = 0);

  Status DecodeFromComparable(const Slice& string);

  // Each byte in the encoding encodes two digits, and a continuation bit in the beginning.
  // The continuation bit is zero if and only if this is the last byte of the encoding.
  std::string EncodeToTwosComplement() const;

  Status DecodeFromTwosComplement(const std::string& string);

  const VarInt& Negate();

  int Sign() const;

 private:
  explicit VarInt(BigNumPtr&& rhs) : impl_(std::move(rhs)) {}

  BigNumPtr impl_;

  friend VarInt operator+(const VarInt& lhs, const VarInt& rhs);
  friend VarInt operator-(const VarInt& lhs, const VarInt& rhs);
};

std::ostream& operator<<(std::ostream& os, const VarInt& v);

} // namespace yb

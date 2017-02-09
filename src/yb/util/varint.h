// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_VARINT_H
#define YB_UTIL_VARINT_H

#include <vector>

#include "yb/util/status.h"
#include "yb/util/slice.h"

namespace yb {
namespace util {

// VarInt holds a sequence of digits d1, d2 ... and a radix r.
// The digits should always to be in range 0 <= d < r, and satisfy d_n-1 > 0, 0 < r < 2^15
// (So that int multiplication never overflows).
// The representation is little endian: The underlying number is d1 + d2 * r + d3 * r^2 + ...
// The provided Encode() function gives a comparable byte sequence from which the size can be
// decoded as well.

class VarInt {

  friend class VarIntTest;

 public:
  VarInt() {}
  // Precondition: 0 < d < r and d_{n-1} > 0, 0 < r < 2^15
  // Note that the digit array must be Little Endian, hence VarInt("243") = VarInt({3, 4, 2}, 10)
  VarInt(const std::vector<int>& digits, int radix, bool is_positive = true)
      : digits_(digits), radix_(radix), is_positive_(is_positive) {}
  VarInt(const VarInt& var_int) : VarInt(var_int.digits_, var_int.radix_, var_int.is_positive_) {}
  // Ensure the string is parsable if you use this constructor.
  // Parse errors are ignored and set to zero.
  explicit VarInt(const std::string& string_val) {
    if (!FromString(string_val).ok()) {
      digits_.clear();
    }
  }
  explicit VarInt(int64 int64_val) { CHECK_OK(FromInt64(int64_val)); }

  const std::vector<int>& digits() const { return digits_; }
  int digit(std::size_t index) const { return digits_.size() > index ? digits_[index] : 0; }
  int radix() const { return radix_; }

  std::string ToDebugString() const;
  std::string ToString() const;
  // The returned value is zero in case of overflow, in the following two functions

  CHECKED_STATUS ToInt64(int64_t* int64_value) const;

  // The input is expected to be of the form (-)?[0-9]+, whitespace is not allowed. Use this
  // after removing whitespace.
  CHECKED_STATUS FromSlice(const Slice& slice);
  CHECKED_STATUS FromString(const std::string& string) { return FromSlice(Slice(string)); }
  CHECKED_STATUS FromInt64(std::int64_t int64_val, int radix = 256);

  // Arithmetic functions will probaly not be needed. Here for testing big numbers for consistency
  // or just in case we need them in future.
  //
  // Preconditions:
  //  inputs.size() > 0, all positive, radix of the inputs are the same. radix * num_inputs <= 2^31.
  static VarInt add(const std::vector<VarInt>& inputs);
  // Not yet implemented
  static VarInt multiply(const VarInt& input1, const VarInt& input2);

  VarInt convert_to_base(int radix) const;

  // Checks the representation by components, For testing purposes.
  bool is_identical_to(const VarInt &other) const;
  // <0, =0, >1 if this <,=,> other numerically.
  int CompareTo(const VarInt& other) const;

  // Note: operator== is numerical comparison, and can yield equality between different bases.
  bool operator==(const VarInt&  other) { return CompareTo(other) == 0; }
  bool operator<(const VarInt&  other) { return CompareTo(other) < 0; }
  bool operator<=(const VarInt&  other) { return CompareTo(other) <= 0; }
  bool operator>(const VarInt&  other) { return CompareTo(other) > 0; }
  bool operator>=(const VarInt&  other) { return CompareTo(other) >= 0; }
  VarInt operator-() { return VarInt(digits_, radix_, !is_positive_); }
  VarInt operator+() { return VarInt(digits_, radix_, is_positive_); }

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
  //
  // In the above description of the encode algorithms,
  // section 1 corresponds to is_signed = false, num_reserved_bits = 0, section 2 corresponds to
  // is_Signed = true, num_reserved_bits = 0, and section 3 addresses the general case with
  // num_reserved_bits > 0.
  std::string Encode(bool is_signed = true, size_t num_reserved_bits = 0);
  // Decodes a Slice from a given slice with parameters is_signed, num_reserved_bits, which are
  // defined above.
  CHECKED_STATUS DecodeFrom(
      Slice slice, size_t* num_decoded_bytes, bool is_signed = true, size_t num_reserved_bits = 0);
  CHECKED_STATUS DecodeFrom(
      const std::string& string, size_t* num_decoded_bytes,
      bool is_signed = true, size_t reserved_bits = 0) {
    return DecodeFrom(Slice(string), num_decoded_bytes, is_signed, reserved_bits);
  }

 private:
  // A readable way to examine bits in a byte stream grouped by 8.
  // For testing purposes only, current VarInt is assumed to be base 256, may or may not be trimmed.
  std::string ToBinaryDebugString() const;

  // Remove all trailing zeros from the digits vector (most significant end).
  // VarInt should be trimmed by default, most of the input VarInts in the API are
  // assumed to be trimmed already.
  void trim();
  // Precondition: 0 < factor < 2^15, 0 < carry < 2^30
  VarInt multiply_and_add(int factor, int carry);
  void append_digits(size_t n, int digit);

  // Since encode always should provide a number of bits multiple of 8, we first express it
  // in base 256, then treat each digit as characters.
  VarInt EncodeToBase256(bool is_signed = true, size_t num_reserved_bits = 0) const;

  std::vector<int> digits_;
  int radix_;
  bool is_positive_;

};

} // namespace util
} // namespace yb


#endif // YB_UTIL_VARINT_H

// Copyright (c) YugaByte, Inc.

#include <vector>
#include <cstdlib>
#include <limits>
#include <glog/logging.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/util/varint.h"

using std::vector;

namespace yb {
namespace util {

string VarInt::ToDebugString() const {
  string output = "[ ";
  output += is_positive_ ? "+" : "-";
  output += " radix " + std::to_string(radix_) + " digits ";
  for (int digit : digits_) {
    output += std::to_string(digit) + " ";
  }
  output += "]";
  return output;
}

string VarInt::ToBinaryDebugString() const {
  DCHECK_EQ(256, radix_) << "Binary DebugString is only supported for base 256 VarInts";
  size_t num_bytes = digits_.size();
  VarInt converted = radix_ == 2 ? *this : convert_to_base(2);
  string output = "[ ";
  for (size_t i = 0; i < num_bytes; i++) {
    for (size_t j = 0; j < 8; j++) {
      output += '0' + converted.digit(8 * num_bytes - i * 8 - j - 1);
    }
    output += ' ';
  }
  output += ']';
  return output;
}

string VarInt::ToString() const {
  if (digits_.empty()) {
    return "0";
  }
  const VarInt& converted = radix_ == 10 ? *this : convert_to_base(10);
  string output;
  if (!is_positive_) {
    output.push_back('-');
  }
  for (auto itr = converted.digits_.rbegin(); itr != converted.digits_.rend(); ++itr) {
    output.push_back('0' + static_cast<char> (*itr));
  }
  return output;
}

const VarInt k_int_64_max = VarInt({255, 255, 255, 255, 255, 255, 255, 127}, 256, true);
const VarInt k_int_64_min = VarInt({0, 0, 0, 0, 0, 0, 0, 128}, 256, false);

Status VarInt::ToInt64(int64_t* int64_value) const {
  const int comparison = CompareTo(is_positive_ ? k_int_64_max : k_int_64_min);
  if (PREDICT_FALSE(!is_positive_ && comparison == 0)) {
    *int64_value = numeric_limits<int64_t>::min();
    return Status::OK();
  }
  if (is_positive_ ? comparison > 0 : comparison < 0) {
    *int64_value = 0;
    return STATUS(InvalidArgument, "VarInt cannot be converted to int64 due to overflow");
  }
  int64_t output = 0;
  for (auto itr = digits_.rbegin(); itr != digits_.rend(); ++itr) {
    output *= static_cast<int64_t> (radix_);
    output += *itr;
  }
  *int64_value = is_positive_ ? output : -output;
  return Status::OK();
}

Status VarInt::FromSlice(const Slice& slice) {
  radix_ = 10;
  is_positive_ = true;
  for (int i = 0; i < slice.size(); i++) {
    if (i == 0 && slice[i] == '+') {
      continue;
    } else if (i == 0 && slice[i] == '-') {
      is_positive_ = false;
      continue;
    }
    if (slice[i] < '0' || slice[i] > '9') {
      return STATUS_SUBSTITUTE(InvalidArgument, "All characters should be 0-9, found $0", slice[i]);
    }
    digits_.push_back(static_cast<int>(slice[i] - '0'));
  }
  std::reverse(digits_.begin(), digits_.end());
  trim();
  return Status::OK();
}

Status VarInt::FromInt64(std::int64_t int64_val, int radix) {
  DCHECK(radix > 0) << "Radix of VarInt found to be non-positive";
  radix_ = radix;
  is_positive_ = int64_val >= 0;
  uint64_t val = 0;
  if (PREDICT_FALSE(int64_val == std::numeric_limits<int64_t>::min())) {
    val = static_cast<uint64_t> (std::numeric_limits<int64_t>::max()) + 1;
  } else {
    val = is_positive_ ? static_cast<uint64_t> (int64_val) : static_cast<uint64_t> (-int64_val);
  }
  while (val > 0) {
    digits_.push_back(static_cast<int> (val % radix));
    val /= radix;
  }
  return Status::OK();
}

VarInt VarInt::add(const vector<VarInt> &inputs) {
  int radix = inputs[0].radix_;
  DCHECK(radix > 0) << "Radix of VarInt found to be non-positive";
  VarInt output = VarInt(vector<int>(), radix);
  size_t max_size = 0;
  for (const VarInt& v : inputs) {
    if (v.digits_.size() > max_size) {
      max_size = v.digits_.size();
    }
  }
  int carry = 0;
  for (size_t i = 0; i < max_size || carry != 0; i++) {
    for (const VarInt &v : inputs) {
      carry += v.digit(i);
    }
    output.digits_.push_back(carry % radix);
    carry /= radix;
  }
  return output;
}

VarInt VarInt::convert_to_base(int radix) const {
  DCHECK(radix > 1) << "Cannot convert to radix <= 1";
  DCHECK(radix_ > 1) << "Cannot convert from radix <= 1";
  VarInt output = VarInt(vector<int>(), radix);
  for (auto itr = digits_.rbegin(); itr != digits_.rend(); ++itr) {
    output = output.multiply_and_add(radix_, *itr);
  }
  output.is_positive_ = is_positive_;
  return output;
}

bool VarInt::is_identical_to(const VarInt &other) const {
  return digits_ == other.digits_ && radix_ == other.radix_ && is_positive_ == other.is_positive_;
}

int VarInt::CompareTo(const VarInt& other) const {
  // If the number is zero, i.e. digits_ is empty, sign doesn't matter.
  if (digits_.empty() && other.digits_.empty()) return 0;
  // Otherwise sign gets priority.
  int sign_comp = static_cast<int> (is_positive_) - static_cast<int> (other.is_positive_);
  if (sign_comp != 0) return sign_comp;
  // Need to get on the same base for comparison.
  const VarInt& converted = other.radix() == radix_ ? other : other.convert_to_base(radix_);
  if (digits_.size() != converted.digits().size()) {
    int comp = static_cast<int> (digits_.size()) - static_cast<int> (converted.digits().size());
    return is_positive_ ? comp : -comp;
  }
  for (size_t i = digits_.size() - 1; i < digits_.size() && i >= 0; i--) {
    int comp = digits_[i] - converted.digits()[i];
    if (comp != 0) {
      return is_positive_ ? comp : -comp;
    }
  }
  return 0;
}

string VarInt::Encode(bool is_signed, size_t num_reserved_bits) {
  VarInt base_256 = EncodeToBase256(is_signed, num_reserved_bits);
  string output;
  for (auto itr = base_256.digits_.rbegin(); itr != base_256.digits_.rend(); ++itr) {
    output += static_cast<char>(*itr);
  }
  return output;
}

// Treats a byte sequence as a bit sequence, finds the i'th bit. xors the result with the input
// 'complement'. Returns corruption if idx is larger than slice.
static Status get_bit(size_t idx, const Slice& slice, bool complement, bool* val) {
  if (PREDICT_FALSE(idx/8 >= slice.size())) {
    return STATUS_SUBSTITUTE(Corruption,
        "Slice has size $0, to decode, expecting at least $1", slice.size(), idx/8+1);
  }
  const uint8_t bit_pos = static_cast<uint8_t> (7 - idx%8);
  *val = ((slice[idx/8] >> bit_pos) % 2 == 1) != complement;
  return Status::OK();
}

Status VarInt::DecodeFrom(
    Slice slice, size_t* num_decoded_bytes, bool is_signed, size_t num_reserved_bits) {
  digits_ = {};
  radix_ = 2;
  // i is the current index. We go from left to right parsing parts of the encoding,
  // First thing to do is skip the reserved bits.
  size_t i = num_reserved_bits;
  size_t j = 0;
  if (is_signed) {
    // Parse the sign bit.
    RETURN_NOT_OK(get_bit(num_reserved_bits, slice, false, &is_positive_));
    // All future bits will be interpreted as complement if is_positive_ is false.
    i++;
  }
  // Find the end of the unary size prefix by searching for the first zero.
  bool val = true;
  for (j = i; val; j++) {
    RETURN_NOT_OK(get_bit(j, slice, !is_positive_, &val));
  }
  // Now we know the total size of what we are decoding.
  *num_decoded_bytes = j - i;
  i = j;
  // Construct a binary number with the rest of the bits.
  for (j = i; j < *num_decoded_bytes * 8; j++) {
    bool val;
    RETURN_NOT_OK(get_bit(j, slice, !is_positive_, &val));
    digits_.push_back(val ? 1 : 0);
  }
  // Go from big endian to little endian.
  std::reverse(digits_.begin(), digits_.end());
  // Remove any training zero digits.
  trim();
  return Status::OK();
}

VarInt VarInt::EncodeToBase256(bool is_signed, size_t num_reserved_bits) const {
  DCHECK(radix_ > 0) << "Radix of VarInt found to be non-positive";
  VarInt binary = convert_to_base(2);
  size_t num_bits = binary.digits_.size();
  if (PREDICT_FALSE(num_bits == 0)) {
    num_bits = 1;
    binary.append_digits(1, 0);
    binary.is_positive_ = true;
  }
  size_t total_num_bits = num_bits + num_reserved_bits;
  if (is_signed) {
    total_num_bits += 1;
  }
  // Number of groups of 7 becomes number of bytes, because of the unary size prefix.
  size_t num_bytes = (total_num_bits + 6) / 7;
  // If total_num_bits is not a multiple of seven, then the numerical part is prefixed with zeros.
  size_t rounding_padding = num_bytes * 7 - total_num_bits;
  binary.append_digits(rounding_padding, 0);
  // This is the delimeter between unary size prefix and the digits part.
  binary.append_digits(1, 0);
  // This is the unary size prefix
  binary.append_digits(num_bytes-1, 1);
  // This is the sign bit
  if (is_signed) {
    // Everything will be complemented for negative numbers, including the sign bit.
    // So we always set the sign bit = 1 here, regardless of the actual sign.
    binary.append_digits(1, 1);
  }
  // The reserved bits go to the very beginning of the digit array.
  // Since they need to be zero after complement, they start being is_signed && !is_positive_
  binary.append_digits(num_reserved_bits, is_signed && !binary.is_positive_);
  // group everything by 8 before returning.
  DCHECK_EQ(binary.digits_.size() % 8, 0);
  VarInt base_256 = binary.convert_to_base(256);
  base_256.append_digits(num_bytes - base_256.digits_.size(), 0);
  if (is_signed && !binary.is_positive_) {
    // For negatives, complement everything
    for (size_t i = 0; i < base_256.digits_.size(); i++) {
      base_256.digits_[i] = 255 - base_256.digits_[i];
    }
  }
  return base_256;
}

void VarInt::trim() {
  while (digits_.size() > 0 && digits_[digits_.size() - 1] == 0) {
    digits_.pop_back();
  }
}

VarInt VarInt::multiply_and_add(int factor, int carry) {
  int radix = radix_;
  VarInt output = VarInt(vector<int>(), radix);
  for (size_t i = 0;; i++) {
    if (PREDICT_FALSE(carry == 0 && i >= digits_.size())) {
      break;
    }
    carry += digit(i) * factor;
    output.digits_.push_back(carry % radix);
    carry /= radix;
  }
  return output;
}

void VarInt::append_digits(size_t n, int digit) {
  for (size_t i = 0; i < n; i++) {
    digits_.push_back(digit);
  }
}

} // namespace util
} // namespace yb

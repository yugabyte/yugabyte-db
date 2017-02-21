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

VarInt::VarInt(const std::string& string_val) {
  if (!FromString(string_val).ok()) {
    clear();
  }
}

VarInt::VarInt(int64_t int64_val) {
  CHECK_OK(FromInt64(int64_val));
}

void VarInt::clear() {
  digits_ = {};
  radix_ = 2;
  is_positive_ = true;
}

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

string VarInt::ToString() const {
  if (digits_.empty()) {
    return "0";
  }
  const VarInt& converted = ConvertToBase(10);
  string output;
  if (!is_positive_) {
    output.push_back('-');
  }
  for (auto itr = converted.digits_.rbegin(); itr != converted.digits_.rend(); ++itr) {
    output.push_back('0' + *itr);
  }
  return output;
}

const VarInt k_uint_64_max = VarInt({255, 255, 255, 255, 255, 255, 255, 255}, 256, true);
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

Status VarInt::FromString(const Slice &slice) {
  if (slice.empty()) {
    return STATUS(InvalidArgument, "Cannot parse empty slice to varint");
  }
  radix_ = 10;
  is_positive_ = true;
  digits_.clear();
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
    digits_.push_back(slice[i] - '0');
  }
  std::reverse(digits_.begin(), digits_.end());
  trim();
  return Status::OK();
}

Status VarInt::ExtractDigits(uint64_t val, int radix) {
  while (val > 0) {
    digits_.push_back(static_cast<uint8_t> (val % radix));
    val /= radix;
  }
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
  return ExtractDigits(val, radix);
}

VarInt VarInt::add(const vector<VarInt> &inputs) {
  int radix = inputs[0].radix_;
  DCHECK(radix > 0) << "Radix of VarInt found to be non-positive";
  vector<int> output_digits;
  size_t max_size = 0;
  for (const VarInt& v : inputs) {
    if (v.digits_.size() > max_size) {
      max_size = v.digits_.size();
    }
  }
  int carry = 0;
  bool is_positive = true;
  for (size_t i = 0; i < max_size || carry != 0; i++) {
    for (const VarInt &v : inputs) {
      carry += v.is_positive_ ? v.digit(i) : -v.digit(i);
    }
    is_positive = carry == 0 ? is_positive : carry > 0;
    output_digits.push_back(carry % radix);
    carry /= radix;
  }
  carry = 0;
  for (auto itr = output_digits.begin(); itr != output_digits.end(); itr++) {
    *itr = carry + (is_positive ? *itr : -*itr);
    if (*itr < 0) {
      carry = -1;
      *itr += radix;
    } else {
      carry = 0;
    }
  }
  vector<uint8_t> uint8digits(output_digits.begin(), output_digits.end());
  VarInt output(uint8digits, radix, is_positive);
  output.trim();
  return output;
}

VarInt VarInt::ConvertToBase(int radix) const {
  DCHECK(radix > 1) << "Cannot convert to radix <= 1";
  DCHECK(radix_ > 1) << "Cannot convert from radix <= 1";
  if (radix == radix_) {
    return VarInt(*this);
  }
  VarInt output = VarInt(vector<uint8_t >(), radix);
  for (auto itr = digits_.rbegin(); itr != digits_.rend(); ++itr) {
    output = output.MultiplyAndAdd(radix_, *itr);
  }
  output.is_positive_ = is_positive_;
  return output;
}

bool VarInt::IsIdenticalTo(const VarInt &other) const {
  return digits_ == other.digits_ && radix_ == other.radix_ && is_positive_ == other.is_positive_;
}

int VarInt::CompareTo(const VarInt& other) const {
  // If the number is zero, i.e. digits_ is empty, sign doesn't matter.
  if (digits_.empty() && other.digits_.empty()) return 0;
  // Otherwise sign gets priority.
  int sign_comp = static_cast<int> (is_positive_) - static_cast<int> (other.is_positive_);
  if (sign_comp != 0) return sign_comp;
  // Need to get on the same base for comparison.
  const VarInt& converted = other.radix() == radix_ ? other : other.ConvertToBase(radix_);
  if (digits_.size() != converted.digits().size()) {
    int comp = static_cast<int> (digits_.size()) - static_cast<int> (converted.digits().size());
    return is_positive_ ? comp : -comp;
  }
  for (size_t i = digits_.size() - 1; i < digits_.size(); i--) {
    int comp = static_cast<int> (digits_[i]) - static_cast<int> (converted.digits()[i]);
    if (comp != 0) {
      return is_positive_ ? comp : -comp;
    }
  }
  return 0;
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

VarInt VarInt::EncodeToComparableBytes(bool is_signed, size_t num_reserved_bits) const {
  DCHECK(radix_ > 0) << "Radix of VarInt found to be non-positive";
  VarInt binary = ConvertToBase(2);
  size_t num_bits = binary.digits_.size();
  if (PREDICT_FALSE(num_bits == 0)) {
    num_bits = 1;
    binary.AppendDigits(1, 0);
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
  binary.AppendDigits(rounding_padding, 0);
  // This is the delimeter between unary size prefix and the digits part.
  binary.AppendDigits(1, 0);
  // This is the unary size prefix
  binary.AppendDigits(num_bytes - 1, 1);
  // This is the sign bit
  if (is_signed) {
    // Everything will be complemented for negative numbers, including the sign bit.
    // So we always set the sign bit = 1 here, regardless of the actual sign.
    binary.AppendDigits(1, 1);
  }
  // The reserved bits go to the very beginning of the digit array.
  // Since they need to be zero after complement, they start being is_signed && !is_positive_
  binary.AppendDigits(num_reserved_bits, is_signed && !binary.is_positive_);
  // group everything by 8 before returning.
  DCHECK_EQ(binary.digits_.size() % 8, 0);
  VarInt base_256 = binary.ConvertToBase(256);
  base_256.AppendDigits(num_bytes - base_256.digits_.size(), 0);
  if (is_signed && !binary.is_positive_) {
    // For negatives, complement everything
    for (size_t i = 0; i < base_256.digits_.size(); i++) {
      base_256.digits_[i] = ~base_256.digits_[i];
    }
  }
  std::reverse(base_256.digits_.begin(), base_256.digits_.end());
  return base_256;
}

Status VarInt::DecodeFromComparable(
    const Slice &slice, size_t *num_decoded_bytes, bool is_signed, size_t num_reserved_bits) {
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

VarInt VarInt::EncodeToTwosComplementBytes(bool* is_out_of_range, size_t num_bytes) const {
  VarInt base256 = ConvertToBase(256);
  if (base256.digits_.empty()) {
    base256.is_positive_ = true;
    base256.digits_.push_back(0);
  }
  if (base256.digits_.back() >= 128 && (num_bytes == 0 || num_bytes > base256.digits_.size())) {
    base256.digits_.push_back(0);
  }
  while (num_bytes > base256.digits_.size()) {
    base256.digits_.push_back(0);
  }

  vector<uint8_t> ref(num_bytes > 0 ? num_bytes : base256.digits_.size(), 0);
  ref.push_back(1);
  VarInt two_power(ref, 256, true);

  if (num_bytes > 0 && base256.digits_.size() != num_bytes) {
    *is_out_of_range = true;
    return base256;
  } else {
    *is_out_of_range = false;
  }
  if (!base256.is_positive_) {
    base256 = two_power + base256;
    if (base256.digits_.back() < 128) {
      *is_out_of_range = true;
      return base256;
    }
  } else if(base256.digits_.back() >= 128) {
    *is_out_of_range = true;
    return base256;
  }
  std::reverse(base256.digits_.begin(), base256.digits_.end());
  return base256;
}

Status VarInt::DecodeFromTwosComplement(const Slice& slice) {
  digits_.clear();
  radix_ = 256;
  is_positive_ = true;
  for (size_t i = 0; i < slice.size(); i++) {
    digits_.push_back(static_cast<int> (slice[i]));
  }
  std::reverse(digits_.begin(), digits_.end());
  if (digits_.back() >= 128) {
    vector<uint8_t> ref(digits_.size(), 0);
    ref.push_back(1);
    digits_ = (VarInt(ref, 256, true) - *this).digits_;
    is_positive_ = false;
  }
  trim();
  return Status::OK();
}



VarInt VarInt::EncodeToDigitPairsBytes() const {
  DCHECK_EQ(radix_, 10);
  if (digits_.empty()) {
    return VarInt({0}, 256, true);
  }
  VarInt base256({}, 256, true);
  size_t len = (digits_.size()+1)/2;
  for (size_t i = 0; i < len; i++) {
    int d = digit(2*i) * 10 + digit(2*i+1);
    if (i != len-1) {
      d += 128;
    }
    base256.digits_.push_back(d);
  }
  return base256;
}

Status VarInt::DecodeFromDigitPairs(const Slice& slice, size_t *num_decoded_bytes) {
  digits_.clear();
  radix_ = 10;
  is_positive_ = true;
  *num_decoded_bytes = 0;
  for (size_t i = 0; i < slice.size(); i++) {
    uint8_t byte = slice[i];
    if (byte >= 128) {
      byte -= 128;
    } else {
      *num_decoded_bytes = i + 1;
      i = slice.size();
    }
    digits_.push_back(byte / 10);
    digits_.push_back(byte % 10);
  }
  if (*num_decoded_bytes == 0) {
    return STATUS(Corruption, "Decoded the whole slice but didn't find the ending");
  }
  trim();
  return Status::OK();
}

void VarInt::trim() {
  while (digits_.size() > 0 && digits_.back() == 0) {
    digits_.pop_back();
  }
}

VarInt VarInt::MultiplyAndAdd(int factor, int carry) {
  int radix = radix_;
  VarInt output = VarInt(vector<uint8_t>(), radix);
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

void VarInt::AppendDigits(size_t n, uint8_t digit) {
  for (size_t i = 0; i < n; i++) {
    digits_.push_back(digit);
  }
}

string VarInt::ToStringFromBase256() const {
  DCHECK_EQ(radix_, 256);
  string output;
  for (auto itr = digits_.begin(); itr != digits_.end(); ++itr) {
    output += static_cast<uint8_t>(*itr);
  }
  return output;
}

string VarInt::ToDebugStringFromBase256() const {
  DCHECK_EQ(256, radix_) << "Binary DebugString is only supported for base 256 VarInts";
  size_t num_bytes = digits_.size();
  string output = "[ ";
  for (size_t i = 0; i < num_bytes; i++) {
    for (size_t j = 0; j < 8; j++) {
      output += '0' + static_cast<char> (digit(i)>>(7-j) & 1);
    }
    output += ' ';
  }
  output += ']';
  return output;
}

std::ostream& operator<<(ostream& os, const VarInt& v) {
  os << v.ToString();
  return os;
}

} // namespace util
} // namespace yb

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

#include <vector>
#include <limits>
#include <iomanip>
#include <glog/logging.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/util/decimal.h"
#include "yb/util/stol_utils.h"

using std::string;
using std::vector;

namespace yb {
namespace util {

void Decimal::clear() {
  digits_ = {};
  exponent_ = VarInt(0);
  is_positive_ = true;
}

string Decimal::ToDebugString() const {
  string output = "[ ";
  output += is_positive_ ? "+" : "-";
  output += " 10^";
  output += exponent_.is_positive_ ? "+" : "";
  output += exponent_.ToString() + " * 0.";
  for (int digit : digits_) {
    output += '0' + digit;
  }
  output += " ]";
  return output;
}

Status Decimal::ToPointString(string* string_val, const int max_length) const {
  if (digits_.empty()) {
    *string_val = "0";
    return Status::OK();
  }
  int64_t exponent = 0;
  RETURN_NOT_OK(exponent_.ToInt64(&exponent));
  if (exponent > max_length || exponent < -max_length) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Max length $0 too small to encode decimal with exponent $1", max_length, exponent);
  }
  string output;
  if (!is_positive_) {
    output = "-";
  }
  if (exponent <= 0) {
    output += "0.";
    for (int i = 0; i < -exponent; i++) {
      output.push_back('0');
    }
    for (size_t i = 0; i < digits_.size(); i++) {
      output.push_back('0' + digits_[i]);
      if (output.size() > max_length) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "Max length $0 too small to encode Decimal", max_length);
      }
    }
  } else {
    for (size_t i = 0; i < digits_.size(); i++) {
      if (exponent == i) {
        output.push_back('.');
      }
      output.push_back('0' + digits_[i]);
      if (output.size() > max_length) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "Max length $0 too small to encode Decimal", max_length);
      }
    }
    for (size_t i = digits_.size(); i < exponent; i++) {
      output.push_back('0');
      if (output.size() > max_length) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "Max length $0 too small to encode Decimal", max_length);
      }
    }
  }
  *string_val = std::move(output);
  return Status::OK();
}

string Decimal::ToScientificString() const {
  if (digits_.empty()) {
    return "0";
  }
  string output;
  if (!is_positive_) {
    output = "-";
  }
  output.push_back('0' + digits_[0]);
  output.push_back('.');
  for (size_t i = 1; i < digits_.size(); i++) {
    output.push_back('0' + digits_[i]);
  }
  output.push_back('e');
  VarInt exponent = exponent_ + VarInt(-1);
  if (exponent.is_positive_) {
    output += "+";
  }
  output += exponent.ToString();
  return output;
}

string Decimal::ToString() const {
  string output;
  if (Decimal::ToPointString(&output).ok()) {
    return output;
  } else {
    return ToScientificString();
  }
}

Status Decimal::ToDouble(long double* double_val) const {
  return CheckedStold(ToString(), double_val);
}

Status Decimal::ToVarInt(VarInt *varint_value, const int max_length) const {
  int64_t exponent = 0;
  RETURN_NOT_OK(exponent_.ToInt64(&exponent));
  if (exponent <= 0) {
    *varint_value = VarInt(0);
    return Status::OK();
  }
  if (exponent > max_length) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Max length $0 too small to convert Decimal to VarInt, need $1",
        max_length, exponent);
  }
  vector<uint8_t> digits(digits_.begin(),
      digits_.size() > exponent ? digits_.begin() + exponent : digits_.end());
  while (digits.size() < exponent) {
    digits.push_back(0);
  }
  std::reverse(digits.begin(), digits.end());
  *varint_value = VarInt(digits, 10, is_positive_);
  return Status::OK();
}

Status Decimal::FromString(const Slice &slice) {
  if (slice.empty()) {
    return STATUS(InvalidArgument, "Cannot decode empty slice to Decimal");
  }
  is_positive_ = slice[0] != '-';
  size_t i = 0;
  if (slice[0] == '+' || slice[0] == '-') {
    i++;
  }
  bool point_found = false;
  size_t point_position = 0;
  digits_.clear();
  for (; i < slice.size() && slice[i] != 'e' && slice[i] != 'E'; i++) {
    if (slice[i] == '.' && !point_found) {
      point_found = true;
      point_position = digits_.size();
      continue;
    }
    if (PREDICT_TRUE(slice[i] >= '0' && slice[i] <= '9')) {
      digits_.push_back(slice[i]-'0');
    } else {
      return STATUS_SUBSTITUTE(
          InvalidArgument,
          "Invalid character $0 found at position $1 when parsing Decimal $2",
          slice[i], i, slice.ToString());
    }
  }
  if (PREDICT_FALSE(digits_.empty())) {
    return STATUS_SUBSTITUTE(
        InvalidArgument,
        "There are no digits in the decimal $0 before the e / E",
        slice.ToString());
  }
  if (!point_found) {
    point_position = digits_.size();
  }
  if (i < slice.size()) {
    Slice exponent_slice(slice);
    exponent_slice.remove_prefix(i+1);
    RETURN_NOT_OK(exponent_.FromString(exponent_slice));
  } else {
    exponent_ = VarInt(0);
  }
  exponent_ = exponent_ + VarInt(static_cast<int64_t> (point_position));
  make_canonical();
  return Status::OK();
}

constexpr size_t kPrecisionLimit = 20;

namespace {

string StringFromDouble(double double_val, int precision_limit = kPrecisionLimit) {
  std::stringstream ss;
  ss << std::setprecision(precision_limit);
  ss << double_val;
  return ss.str();
}

}  // namespace

Status Decimal::FromDouble(double double_val) {
  string str = StringFromDouble(double_val);
  if (str == "nan") {
    return STATUS(Corruption, "Cannot convert nan to Decimal");
  } else if (str == "inf") {
    return STATUS(Corruption, "Cannot convert inf to Decimal");
  } else if (str == "-inf") {
    return STATUS(Corruption, "Cannot convert -inf to Decimal");
  }
  return FromString(str);
}

Status Decimal::FromVarInt(const VarInt &varint_val) {
  return FromString(varint_val.ToString());
}

bool Decimal::is_integer() const {
  return digits_.empty() || exponent_ >= VarInt(static_cast<int64_t>(digits_.size()));
}

int Decimal::CompareTo(const Decimal &other) const {
  if (is_positive_ != other.is_positive_) {
    return static_cast<int>(is_positive_) - static_cast<int>(other.is_positive_);
  }
  int comp;
  // We must compare zeros in a special way because the exponent logic doesn't work with special
  // canonicalization for zero.
  if (digits_.empty() || other.digits_.empty()) {
    comp = static_cast<int>(digits_.empty()) - static_cast<int>(other.digits_.empty());
    return is_positive_ ? -comp : comp;
  }
  comp = exponent_.CompareTo(other.exponent_);
  if (comp != 0) {
    return is_positive_ ? comp : -comp;
  }
  for (size_t i = 0; i < digits_.size() && i < other.digits_.size(); i++) {
    comp = static_cast<int>(digits_[i]) - static_cast<int>(other.digits_[i]);
    if (comp != 0) {
      return is_positive_ ? comp : -comp;
    }
  }
  comp = static_cast<int>(this->digits_.size()) - static_cast<int>(other.digits_.size());
  return is_positive_ ? comp : -comp;
}

string Decimal::EncodeToComparable() const {
  // Zero is encoded to the special value 128.
  if (digits_.empty()) {
    return string(1, 128);
  }
  // We reserve two bits for sign: -, zero, and +. Their sign portions are resp. '00', '10', '11'.
  string exponent = exponent_.EncodeToComparable(/* is_signed */ true, /* num_reserved_bits */ 2);
  const string mantissa = VarInt(digits_, 10).EncodeToDigitPairs();
  string output = exponent + mantissa;
  // The first two (reserved) bits are set to 1 here.
  output[0] += 192;
  // For negatives, everything is complemented (including the sign bits) which were set to 1 above.
  if (!is_positive_) {
    for (int i = 0; i < output.size(); i++) {
      output[i] = ~output[i]; // Bitwise not.
    }
  }
  return output;
}

Status Decimal::DecodeFromComparable(const Slice& slice, size_t *num_decoded_bytes) {
  if (slice.empty()) {
    return STATUS(Corruption, "Cannot decode Decimal from empty slice.");
  }
  // Zero is specially decoded from the value 128.
  if (slice[0] == 128) {
    clear();
    *num_decoded_bytes = 1;
    return Status::OK();
  }
  // The first bit is enough to decode the sign.
  is_positive_ = slice[0] >= 128;
  // We have to complement everything if negative, so we are making a copy.
  string encoded = slice.ToString();
  if (!is_positive_) {
    for (size_t i = 0; i < encoded.size(); i++) {
      encoded[i] = ~encoded[i];
    }
  }
  size_t num_exponent_bytes = 0;
  RETURN_NOT_OK(exponent_.DecodeFromComparable(
      encoded, &num_exponent_bytes, /* is signed */ true, /* num_reserved_bits */ 2));
  Slice remaining_slice(encoded);
  remaining_slice.remove_prefix(num_exponent_bytes);
  VarInt mantissa;
  size_t num_mantissa_bytes = 0;
  RETURN_NOT_OK(mantissa.DecodeFromDigitPairs(remaining_slice, &num_mantissa_bytes));
  digits_ = vector<uint8_t>(mantissa.digits_.begin(), mantissa.digits_.end());
  *num_decoded_bytes = num_exponent_bytes + num_mantissa_bytes;
  return Status::OK();
}

Status Decimal::DecodeFromComparable(const Slice& slice) {
  size_t num_decoded_bytes;
  return DecodeFromComparable(slice, &num_decoded_bytes);
}

Status Decimal::DecodeFromComparable(const string& str) {
  return DecodeFromComparable(Slice(str));
}

string Decimal::EncodeToSerializedBigDecimal(bool* is_out_of_range) const {
  // Note that BigDecimal's scale is not the same as our exponent, but related by the following:
  VarInt varint_scale = VarInt(static_cast<int64_t> (digits_.size())) - exponent_;
  // Must use 4 bytes for the two's complement encoding of the scale.
  bool is_overflow = false;
  string scale = varint_scale.EncodeToTwosComplement(&is_overflow, 4);
  *is_out_of_range = is_overflow;
  vector<uint8_t> digits(digits_);
  // VarInt representation needs reversed digits.
  std::reverse(digits.begin(), digits.end());
  // Note that the mantissa varint needs to have the same sign as the current decimal.
  // Get the digit array in int from int8_t in this class.
  string mantissa = VarInt(digits, 10, is_positive_).EncodeToTwosComplement(&is_overflow);
  return scale + mantissa;
}

Status Decimal::DecodeFromSerializedBigDecimal(const Slice &slice) {
  if (slice.size() < 5) {
    return STATUS_SUBSTITUTE(
        Corruption, "Serialized BigDecimal must have at least 5 bytes. Found $0", slice.size());
  }
  // Decode the scale from the first 4 bytes.
  VarInt scale;
  RETURN_NOT_OK(scale.DecodeFromTwosComplement(Slice(slice.data(), 4)));
  VarInt mantissa;
  RETURN_NOT_OK(mantissa.DecodeFromTwosComplement(Slice(slice.data() + 4, slice.size() - 4)));
  // Must convert to base 10 before we can interpret as digits in our Decimal representation
  mantissa = mantissa.ConvertToBase(10);
  // The sign of the BigDecimal is the sign of the mantissa
  is_positive_ = mantissa.is_positive_;
  digits_ = vector <uint8_t> (mantissa.digits_.begin(), mantissa.digits_.end());
  // The varint has the digits in reverse order
  std::reverse(digits_.begin(), digits_.end());
  exponent_ = VarInt(static_cast<int64_t> (digits_.size())) - scale;
  make_canonical();
  return Status::OK();
}

bool Decimal::IsIdenticalTo(const Decimal &other) const {
  return
      is_positive_ == other.is_positive_ &&
      exponent_ == other.exponent_ &&
      digits_ == other.digits_;
}

bool Decimal::is_canonical() const {
  if (digits_.empty()) {
    return is_positive_ && exponent_ == VarInt(0);
  }
  return digits_.front() != 0 && digits_.back() != 0;
}

void Decimal::make_canonical() {
  if (is_canonical()) {
    return;
  }
  size_t num_zeros = 0;
  while (num_zeros < digits_.size() && digits_[num_zeros] == 0) {
    num_zeros++;
  }
  exponent_ = exponent_ - VarInt(num_zeros);
  for (size_t i = num_zeros; i < digits_.size() + num_zeros; i++) {
    digits_[i-num_zeros] = i < digits_.size() ? digits_[i] : 0;
  }
  while (!digits_.empty() && digits_.back() == 0) {
    digits_.pop_back();
  }
  if (digits_.empty()) {
    clear();
  }
}

Decimal DecimalFromComparable(const Slice& slice) {
  Decimal decimal;
  CHECK_OK(decimal.DecodeFromComparable(slice));
  return decimal;
}

Decimal DecimalFromComparable(const std::string& str) {
  return DecimalFromComparable(Slice(str));
}

std::ostream& operator<<(ostream& os, const Decimal& d) {
  os << d.ToString();
  return os;
}

} // namespace util
} // namespace yb

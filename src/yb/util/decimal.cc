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
#include "yb/util/decimal.h"

#include <iomanip>
#include <limits>
#include <vector>

#include "yb/gutil/casts.h"

#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stol_utils.h"

using std::string;
using std::vector;
using std::ostream;

namespace yb {
namespace util {

Decimal::Decimal(const std::string& string_val) {
  CHECK_OK(FromString(string_val));
}

Decimal::Decimal(double double_val) {
  CHECK_OK(FromDouble(double_val));
}

Decimal::Decimal(const VarInt& varint_val) {
  CHECK_OK(FromVarInt(varint_val));
}

void Decimal::clear() {
  digits_ = {};
  exponent_ = VarInt(0);
  is_positive_ = true;
}

string Decimal::ToDebugString() const {
  string output = "[ ";
  output += is_positive_ ? "+" : "-";
  output += " 10^";
  output += exponent_.Sign() >= 0 ? "+" : "";
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
  int64_t exponent = VERIFY_RESULT(exponent_.ToInt64());
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
      if (implicit_cast<int64_t>(output.size()) > max_length) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "Max length $0 too small to encode Decimal", max_length);
      }
    }
  } else {
    for (size_t i = 0; i < digits_.size(); i++) {
      if (implicit_cast<size_t>(exponent) == i) {
        output.push_back('.');
      }
      output.push_back('0' + digits_[i]);
      if (implicit_cast<int64_t>(output.size()) > max_length) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "Max length $0 too small to encode Decimal", max_length);
      }
    }
    for (ssize_t i = digits_.size(); i < exponent; i++) {
      output.push_back('0');
      if (implicit_cast<int64_t>(output.size()) > max_length) {
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
  if (exponent.Sign() >= 0) {
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

Result<long double> Decimal::ToDouble() const {
  return CheckedStold(ToString());
}

Result<VarInt> Decimal::ToVarInt() const {
  string string_val;
  RETURN_NOT_OK(ToPointString(&string_val, kUnlimitedMaxLength));

  if (!is_integer()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Cannot convert non-integer Decimal into integer: $0", string_val);
  }

  return VarInt::CreateFromString(string_val);
}

Status Decimal::FromString(const Slice &slice) {
  if (slice.empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Cannot decode empty slice to Decimal: $0", slice.ToString());
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
        slice.ToBuffer());
  }
  if (!point_found) {
    point_position = digits_.size();
  }
  if (i < slice.size()) {
    Slice exponent_slice(slice);
    exponent_slice.remove_prefix(i+1);
    RETURN_NOT_OK(exponent_.FromString(exponent_slice.ToBuffer()));
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
  } else if (str == "-nan") {
    return STATUS(Corruption, "Cannot convert -nan to Decimal");
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

// Encodes pairs of digits into one byte each. The last bit in each byte is the
// "continuation bit" which is equal to 1 for all bytes except the last.
std::string EncodeToDigitPairs(const std::vector<uint8_t>& digits) {
  if (digits.empty()) {
    return std::string();
  }
  size_t len = (digits.size() + 1) / 2;
  std::string result(len, 0);
  for (size_t i = 0; i < len - 1; ++i) {
    result[i] = (digits[i * 2] * 10 + digits[i * 2 + 1]) * 2 + 1;
  }
  size_t i = len - 1;
  uint8_t last_byte = digits[i * 2] * 10;
  if (i * 2 + 1 < digits.size()) {
    last_byte += digits[i * 2 + 1];
  }
  result[i] = last_byte * 2;
  return result;
}

string Decimal::EncodeToComparable() const {
  // Zero is encoded to the special value 128.
  if (digits_.empty()) {
    return string(1, static_cast<char>(128));
  }
  // We reserve two bits for sign: -, zero, and +. Their sign portions are resp. '00', '10', '11'.
  string exponent = exponent_.EncodeToComparable(/* num_reserved_bits */ 2);
  const string mantissa = EncodeToDigitPairs(digits_);
  string output = exponent + mantissa;
  // The first two (reserved) bits are set to 1 here.
  output[0] |= 0xc0;
  // For negatives, everything is complemented (including the sign bits) which were set to 1 above.
  if (!is_positive_) {
    for (size_t i = 0; i < output.size(); i++) {
      output[i] = ~output[i]; // Bitwise not.
    }
  }
  return output;
}

Status DecodeFromDigitPairs(
    const Slice& slice, size_t *num_decoded_bytes, std::vector<uint8_t>* digits) {
  digits->clear();
  digits->reserve(slice.size() * 2);
  *num_decoded_bytes = 0;
  for (size_t i = 0; i < slice.size(); i++) {
    uint8_t byte = slice[i];
    if (!(byte & 1)) {
      *num_decoded_bytes = i + 1;
      i = slice.size();
    }
    byte /= 2;
    digits->push_back(byte / 10);
    digits->push_back(byte % 10);
  }
  if (*num_decoded_bytes == 0) {
    return STATUS(Corruption, "Decoded the whole slice but didn't find the ending");
  }
  while (!digits->empty() && !digits->back()) {
    digits->pop_back();
  }
  return Status::OK();
}

Status Decimal::DecodeFromComparable(Slice* slice) {
  size_t num_decoded_bytes;
  RETURN_NOT_OK(DecodeFromComparable(*slice, &num_decoded_bytes));
  slice->remove_prefix(num_decoded_bytes);
  return Status::OK();
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
  string encoded = slice.ToBuffer();
  if (!is_positive_) {
    for (size_t i = 0; i < encoded.size(); i++) {
      encoded[i] = ~encoded[i];
    }
  }
  size_t num_exponent_bytes = 0;
  RETURN_NOT_OK(exponent_.DecodeFromComparable(
      encoded, &num_exponent_bytes, /* num_reserved_bits */ 2));
  Slice remaining_slice(encoded);
  remaining_slice.remove_prefix(num_exponent_bytes);
  size_t num_mantissa_bytes = 0;
  RETURN_NOT_OK(DecodeFromDigitPairs(remaining_slice, &num_mantissa_bytes, &digits_));
  *num_decoded_bytes = num_exponent_bytes + num_mantissa_bytes;
  return Status::OK();
}

Status Decimal::DecodeFromComparable(const Slice& slice) {
  size_t num_decoded_bytes;
  return DecodeFromComparable(slice, &num_decoded_bytes);
}

string Decimal::EncodeToSerializedBigDecimal(bool* is_out_of_range) const {
  // Note that BigDecimal's scale is not the same as our exponent, but related by the following:
  VarInt varint_scale = VarInt(static_cast<int64_t>(digits_.size())) - exponent_;
  // Must use 4 bytes for the two's complement encoding of the scale.
  string scale = varint_scale.EncodeToTwosComplement();
  if (scale.length() > 4) {
    *is_out_of_range = true;
    return std::string();
  }
  *is_out_of_range = false;
  if (scale.length() < 4) {
    scale = std::string(4 - scale.length(), varint_scale.Sign() < 0 ? 0xff : 0x00) + scale;
  }

  std::vector<char> digits;
  digits.reserve(digits_.size() + 1);
  for (auto digit : digits_) {
    digits.push_back('0' + digit);
  }
  digits.push_back(0);

  // Note that the mantissa varint needs to have the same sign as the current decimal.
  // Get the digit array in int from int8_t in this class.
  auto temp = !digits_.empty() ? CHECK_RESULT(VarInt::CreateFromString(digits.data())) : VarInt(0);
  if (!is_positive_) {
    temp.Negate();
  }
  string mantissa = temp.EncodeToTwosComplement();
  return scale + mantissa;
}

Status Decimal::DecodeFromSerializedBigDecimal(Slice slice) {
  if (slice.size() < 5) {
    return STATUS_SUBSTITUTE(
        Corruption, "Serialized BigDecimal must have at least 5 bytes. Found $0", slice.size());
  }
  // Decode the scale from the first 4 bytes.
  VarInt scale;
  RETURN_NOT_OK(scale.DecodeFromTwosComplement(std::string(slice.cdata(), 4)));
  slice.remove_prefix(4);
  VarInt mantissa;
  RETURN_NOT_OK(mantissa.DecodeFromTwosComplement(slice.ToBuffer()));
  bool negative = mantissa.Sign() < 0;
  if (negative) {
    mantissa.Negate();
  }
  auto mantissa_str = mantissa.ToString();
  // The sign of the BigDecimal is the sign of the mantissa
  is_positive_ = !negative;
  digits_.resize(mantissa_str.size());
  for (size_t i = 0; i != mantissa_str.size(); ++i) {
    digits_[i] = mantissa_str[i] - '0';
  }
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

// Normalize so that both Decimals have same exponent and same number of digits
// Add digits considering sign
// Canonicalize result

Decimal Decimal::operator+(const Decimal& other) const {
  Decimal decimal(digits_, exponent_, is_positive_);
  Decimal other1(other.digits_, other.exponent_, other.is_positive_);

  // Normalize the exponents
  // eg. if we are adding 0.1E+3 and 0.5E+2, we first convert them to 0.1E+3 and 0.05E+3
  VarInt max_exponent = std::max(other1.exponent_, decimal.exponent_);
  VarInt var_int_one(1);
  if (decimal.exponent_ < max_exponent) {
    VarInt increase_varint = max_exponent - decimal.exponent_;
    int64_t increase = CHECK_RESULT(increase_varint.ToInt64());
    decimal.digits_.insert(decimal.digits_.begin(), increase, 0);
    decimal.exponent_ = max_exponent;
  }
  if (other1.exponent_ < max_exponent) {
    VarInt increase_varint = max_exponent - other1.exponent_;
    int64_t increase = CHECK_RESULT(increase_varint.ToInt64());
    other1.digits_.insert(other1.digits_.begin(), increase, 0);
    other1.exponent_ = max_exponent;
  }

  // Make length of digits the same.
  // If we need to add 0.1E+3 and 0.05E+3, we convert them to 0.10E+3 and 0.05E+3
  size_t max_digits = std::max(decimal.digits_.size(), other1.digits_.size());
  if (decimal.digits_.size() < max_digits) {
    auto increase = max_digits - decimal.digits_.size();
    for (size_t i = 0; i < increase; i = i + 1) {
      decimal.digits_.push_back(0);
    }
  }
  if (other1.digits_.size() < max_digits) {
    auto increase = max_digits - other1.digits_.size();
    for (size_t i = 0; i < increase; i++) {
      other1.digits_.push_back(0);
    }
  }

  // Add mantissa
  // For the case when the both numbers are positive (eg. 0.1E+3 and 0.05E+3)
  // or both negative (eg. -0.1E+3 and -0.05E+3), we just add the mantissas
  if (decimal.is_positive_ == other1.is_positive_) {
    for (int64_t i = max_digits - 1; i >= 0; i--) {
      decimal.digits_[i] += other1.digits_[i];
      if (decimal.digits_[i] > 9) {
        decimal.digits_[i] -= 10;
        if (i > 0) {
          decimal.digits_[i - 1]++;
        } else {
          decimal.digits_.insert(decimal.digits_.begin(), 1);
          decimal.exponent_ = decimal.exponent_ + var_int_one;
        }
      }
    }
  } else {
  // For the case when the two numbers have opposite sign (eg. 0.1E+3 and -0.05E+3)
  // or (-0.1E+3 and 0.05E+3), we subtract the smaller mantissa from the larger mantissa
  // and use the sign of the larger mantissa
    int comp = 0; // indicates whether mantissa of this is bigger
    for (size_t i = 0; i < decimal.digits_.size() && i < other1.digits_.size(); i++) {
      comp = static_cast<int>(decimal.digits_[i]) - static_cast<int>(other1.digits_[i]);
      if (comp != 0) {
        break;
      }
    }
    if (comp < 0) {
      decimal.digits_.swap(other1.digits_);
      decimal.is_positive_ = !decimal.is_positive_;
    }
    for (int64_t i = max_digits - 1; i >= 0; i--) {
      if (decimal.digits_[i] >= other1.digits_[i]) {
        decimal.digits_[i] -= other1.digits_[i];
      } else {
        other1.digits_[i-1]++;
        decimal.digits_[i] = decimal.digits_[i] + 10 - other1.digits_[i];
      }
    }
  }

  // Finally we normalize the number obtained to get rid of leading and trailing 0's
  // in the mantissa.
  decimal.make_canonical();
  return decimal;
}

std::ostream& operator<<(ostream& os, const Decimal& d) {
  os << d.ToString();
  return os;
}

} // namespace util
} // namespace yb

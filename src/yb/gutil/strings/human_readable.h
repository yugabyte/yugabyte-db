// Copyright 2007 Google Inc. All Rights Reserved.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
// A collection of methods to convert back and forth between a number
// and a human-readable string representing the number.

#pragma once

#include <functional>
#include <string>

#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"



//                                 WARNING
// HumanReadable{NumBytes, Int} don't give you the standard set of SI prefixes.
//
// HumanReadableNumBytes uses binary powers -- 1M = 1 << 20 -- but for numbers
// less than 1024, it adds the suffix "B" for "bytes." It is OK when you need
// to print a literal number of bytes, but can be awfully confusing for
// anything else.
//
// HumanReadableInt uses decimal powers -- 1M = 10^3 -- but prints
// 'B'-for-billion instead of 'G'-for-giga. It's good for representing
// true numbers, like how many documents are in a repository.
// HumanReadableNum is the same as HumanReadableInt but has additional
// support for DoubleToString(), where smaller numbers will print more
// (up to 3) decimal places.
//
// If you want SI prefixes, use the functions in si_prefix.h instead; for
// example, strings::si_prefix::ToDecimalString(1053.2) == "1.05k".

class HumanReadableNumBytes {
 public:
  // Converts between an int64 representing a number of bytes and a
  // human readable string representing the same number.
  // e.g. 1000000 -> "976.6K".
  //  Note that calling these two functions in succession isn't a
  //  noop, since ToString() may round.
  static bool ToInt64(const std::string &str, int64 *num_bytes);
  static std::string ToString(int64 num_bytes);
  // Like ToString but without rounding.  For example 1025 would return
  // "1025B" rather than "1.0K".  Uses the largest common denominator.
  static std::string ToStringWithoutRounding(int64 num_bytes);

  static bool ToDouble(const std::string &str, double *num_bytes);
  // Function overloading this with a function that takes an int64 is just
  // asking for trouble.
  static std::string DoubleToString(double num_bytes);

  // TODO(user): Maybe change this class to use SIPrefix?

  // ----------------------------------------------------------------------
  // LessThan
  // humanreadablebytes_less
  // humanreadablebytes_greater
  //    These numerically compare the values encoded in strings by
  //    ToString().  Strings which cannot be parsed are treated as
  //    if they represented the value 0.  The following byte sizes
  //    would be sorted as:
  //        3B
  //        .06K
  //        .03M
  //        10000G
  //        10T
  //        3.01P
  //        3.02P
  //        0.007E
  // ----------------------------------------------------------------------
  static bool LessThan(const std::string &a, const std::string &b);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(HumanReadableNumBytes);
};

class HumanReadableInt {
 public:
  // Similar to HumanReadableNumBytes::ToInt64(), but uses decimal
  // rather than binary expansions - so M = 1 million, B = 1 billion,
  // etc. Numbers beyond 1T are expressed as "3E14" etc.
  static std::string ToString(int64 value);

  // Reverses ToString(). Note that calling these two functions in
  // succession isn't a noop, since ToString() may round.
  static bool ToInt64(const std::string &str, int64 *value);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(HumanReadableInt);
};

class HumanReadableNum {
 public:
  // Same as HumanReadableInt::ToString().
  static std::string ToString(int64 value);

  // Similar to HumanReadableInt::ToString(), but prints 2 decimal
  // places for numbers with absolute value < 10.0 and 1 decimal place
  // for numbers >= 10.0 and < 100.0.
  static std::string DoubleToString(double value);

  // Reverses DoubleToString(). Note that calling these two functions in
  // succession isn't a noop, since there may be rounding errors.
  static bool ToDouble(const std::string &str, double *value);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(HumanReadableNum);
};

class HumanReadableElapsedTime {
 public:
  // Converts a time interval as double to a human readable
  // string. For example:
  //   0.001       -> "1 ms"
  //   10.0        -> "10 s"
  //   933120.0    -> "10.8 days"
  //   39420000.0  -> "1.25 years"
  //   -10         -> "-10 s"
  static std::string ToShortString(double seconds);

  // Reverses ToShortString(). Note that calling these two functions in
  // succession isn't a noop, since ToShortString() may round.
  // This accepts multiple forms of units, but the abbreviated forms are
  // us (microseconds), ms (milliseconds), s, m (minutes), h, d, w,
  // M (month = 30 days), y
  // This function is not particularly fast.  Use at performance peril.
  // Only leading negative signs are allowed.
  // Examples:
  //   "1ms"        -> 0.001
  //   "10 second"  -> 10
  //   "10.8 days"  -> 933120.0
  //   "1m 30s"     -> 90
  //   "-10 sec"    -> -10
  //   "18.3"       -> 18.3
  //   "1M"         -> 2592000 (1 month = 30 days)
  static bool ToDouble(const std::string& str, double* value);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(HumanReadableElapsedTime);
};

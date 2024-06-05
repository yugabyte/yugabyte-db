//--------------------------------------------------------------------------------------------------
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
//
// Utilities for DateTime parsing, processing and formatting
// TODO: some parsing and formatting settings (e.g. default timezone) should be configurable using a
// config file or QL functions
// currently hardcoding default_timezone(UTC), precision, output format and epoch
//--------------------------------------------------------------------------------------------------


#pragma once

#include <regex>

#include "yb/util/monotime.h"
#include "yb/util/timestamp.h"

namespace yb {

class DateTime {
 public:
  //----------------------------------------------------------------------------------------------
  // Timestamp input and output formats.
  struct InputFormat {
    std::vector<std::regex> regexes;
    int input_precision;
    // When use_utc is true, the UTC is used during conversion. Otherwise local TZ is used.
    bool use_utc;
  };

  struct OutputFormat {
    const std::locale output_locale;
    // See comment in InputFormat.
    bool use_utc;
  };

  // CQL timestamp formats.
  static const InputFormat CqlInputFormat;
  static const OutputFormat CqlOutputFormat;

  // Human readable format.
  static const InputFormat HumanReadableInputFormat;
  static const OutputFormat HumanReadableOutputFormat;

  //----------------------------------------------------------------------------------------------
  static Result<Timestamp> TimestampFromString(const std::string_view& str,
                                               const InputFormat& input_format = CqlInputFormat);
  static Timestamp TimestampFromInt(int64_t val, const InputFormat& input_format = CqlInputFormat);
  static std::string TimestampToString(Timestamp timestamp,
                                       const OutputFormat& output_format = CqlOutputFormat);
  static Timestamp TimestampNow();

  static std::string SystemTimezone();

  //----------------------------------------------------------------------------------------------
  // Date represented as the number of days in uint32_t with the epoch (1970-01-01) at the center of
  // the range (2^31). Min and max possible dates are "-5877641-06-23" and "5881580-07-11".
  static Result<uint32_t> DateFromString(const std::string_view& str);
  static Result<uint32_t> DateFromTimestamp(Timestamp timestamp);
  static Result<uint32_t> DateFromUnixTimestamp(int64_t unix_timestamp);
  static Result<std::string> DateToString(uint32_t date);
  static Timestamp DateToTimestamp(uint32_t date);
  static int64_t DateToUnixTimestamp(uint32_t date);
  static uint32_t DateNow();

  //----------------------------------------------------------------------------------------------
  // Min and max time of day since midnight in nano-seconds.
  static constexpr int64_t kMinTime = 0;
  static constexpr int64_t kMaxTime = 24 * 60 * 60 * 1000000000L - 1; // 23:59:59.999999999

  static Result<int64_t> TimeFromString(const std::string_view& str);
  static Result<std::string> TimeToString(int64_t time);
  static int64_t TimeNow();

  //----------------------------------------------------------------------------------------------
  // Interval represents a relative span of time, in microseconds.
  // This is normally utilized relative to the current HybridTime.

  static Result<MonoDelta> IntervalFromString(const std::string_view& str);

  //----------------------------------------------------------------------------------------------
  static int64_t AdjustPrecision(int64_t val, size_t input_precision, size_t output_precision);
  static constexpr int64_t kInternalPrecision = 6; // microseconds
  static constexpr int64_t kMillisecondPrecision = 3; // milliseconds
};

} // namespace yb

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


#ifndef YB_UTIL_DATE_TIME_H_
#define YB_UTIL_DATE_TIME_H_


#include <locale>
#include <regex>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

#include "yb/util/result.h"
#include "yb/util/timestamp.h"

namespace yb {

// type of time zone (shared) pointer
typedef boost::local_time::time_zone_ptr tz_ptr_type;
// type for point in time
typedef boost::posix_time::ptime time_type;

class DateTimeFormatBase {
 public:
  time_type epoch_start() const;
  tz_ptr_type default_tz() const;

 protected:
  DateTimeFormatBase(time_type epoch_start, tz_ptr_type default_tz);
  time_type epoch_start_;
  tz_ptr_type default_tz_;
};

class DateTimeInputFormat : public DateTimeFormatBase {
 public:
  DateTimeInputFormat(time_type epoch_start,
                      tz_ptr_type default_tz,
                      const std::vector<std::regex>& regexes,
                      int input_precision);
  int input_precision() const;
  std::vector<std::regex> regexes() const;

 private:
  std::vector<std::regex> regexes_;
  int input_precision_;
};

class DateTimeOutputFormat : public DateTimeFormatBase {
 public:
  DateTimeOutputFormat(time_type epoch_start, tz_ptr_type default_tz, std::locale output_locale);
  std::locale output_locale() const;

 private:
  std::locale output_locale_;

};

class DateTime {
 public:
  DateTime();
  ~DateTime();

  static DateTimeInputFormat CqlDateTimeInputFormat;
  static DateTimeOutputFormat CqlDateTimeOutputFormat;

  static Result<Timestamp> TimestampFromString(
      const string& str,
      const DateTimeInputFormat input_format = CqlDateTimeInputFormat);
  static Timestamp TimestampFromInt(int64_t val,
                                    DateTimeInputFormat input_format = CqlDateTimeInputFormat);
  static std::string TimestampToString(
      Timestamp timestamp, DateTimeOutputFormat output_format = CqlDateTimeOutputFormat);

  static tz_ptr_type GetSystemTimezone();
  static tz_ptr_type GetUtcTimezone();
  static int64_t AdjustPrecision(int64_t val, int input_precision, int output_precision);
  static constexpr int64_t kInternalPrecision = 6; // microseconds
  static constexpr int64_t kMillisecondPrecision = 3; // milliseconds

 private:
  // Utility constants to avoid overflow when increasing precision in AdjustPrecision().
  static constexpr int64_t kInt64MaxOverTen = INT64_MAX / 10;
  static constexpr int64_t kInt64MinOverTen = INT64_MIN / 10;
};

} // namespace yb

#endif // YB_UTIL_DATE_TIME_H_

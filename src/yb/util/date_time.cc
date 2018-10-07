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
// DateTime parser and serializer
//--------------------------------------------------------------------------------------------------

#include <regex>
#include <ctime>

#include "yb/util/date_time.h"
#include "yb/util/logging.h"
#include "boost/date_time/local_time_adjustor.hpp"
#include "boost/date_time/c_local_time_adjustor.hpp"

using std::locale;
using std::vector;
using std::string;
using std::regex;
using boost::gregorian::date;
using boost::gregorian::date_duration;
using boost::gregorian::day_clock;
using boost::local_time::local_date_time;
using boost::local_time::local_time_facet;
using boost::local_time::local_time_input_facet;
using boost::local_time::local_microsec_clock;
using boost::local_time::not_a_date_time;
using boost::local_time::posix_time_zone;
using boost::local_time::time_zone_ptr;
using boost::posix_time::ptime;
using boost::posix_time::microseconds;
using boost::posix_time::time_duration;
using boost::posix_time::microsec_clock;

namespace yb {

namespace {

// Unix epoch in date and Posix time format.
static const date kEpochDate(1970, 1, 1);
static const ptime kEpochTime(kEpochDate);

// Date offset of Unix epoch (2^31).
static constexpr uint32_t kEpochDateOffset = 1<<31;

// Day in milli- and micro-seconds.
static constexpr int64_t kDayInMilliSeconds = 24 * 60 * 60 * 1000L;
static constexpr int64_t kDayInMicroSeconds = kDayInMilliSeconds * 1000L;

// UTC and system (local) timezones.
static const time_zone_ptr kUtcTimezone(new posix_time_zone("UTC"));
static const time_zone_ptr kSystemTimezone(new posix_time_zone(
    []() -> string {
      // Get current timezone by getting current UTC time, converting to local time and computing
      // the offset.
      const ptime utc_time = microsec_clock::universal_time();
      const ptime local_time = boost::date_time::c_local_adjustor<ptime>::utc_to_local(utc_time);
      const time_duration offset = local_time - utc_time;
      const int hours = offset.hours();
      const int minutes = offset.minutes();
      char buffer[7]; // "+HH:MM" or "-HH:MM"
      const int result = snprintf(buffer, sizeof(buffer), "%+2.2d:%2.2d", hours, minutes);
      CHECK(result > 0 && result < sizeof(buffer)) << "Unexpected snprintf result: " << result;
      return buffer;
    }()));

} // namespace

Result<Timestamp> DateTime::TimestampFromString(const string& str,
                                                const InputFormat& input_format) {
  std::smatch m;
  // trying first regex to match from the format
  for (const auto& reg : input_format.regexes) {
    if (std::regex_match(str, m, reg)) {
      // setting default values where missing
      const int year = stoi(m.str(1));
      const int month = stoi(m.str(2));
      const int day = stoi(m.str(3));
      const int hours = m.str(4).empty() ? 0 : stoi(m.str(4));
      const int minutes = m.str(5).empty() ? 0 : stoi(m.str(5));
      const int seconds = m.str(6).empty() ? 0 : stoi(m.str(6));
      int64_t frac = m.str(7).empty() ? 0 : stoi(m.str(7));
      frac = AdjustPrecision(frac, m.str(7).size(), time_duration::num_fractional_digits());
      // constructing date_time and getting difference from epoch to set as Timestamp value
      try {
        const date d(year, month, day);
        const time_duration t(hours, minutes, seconds, frac);
        const time_zone_ptr tz = !m.str(8).empty() ? time_zone_ptr(new posix_time_zone(m.str(8)))
                                                   : kSystemTimezone;
        const local_date_time ldt(d, t, tz, local_date_time::NOT_DATE_TIME_ON_ERROR);
        const local_date_time epoch(kEpochTime, kUtcTimezone);
        return Timestamp((ldt - epoch).total_microseconds());
      } catch (std::exception& e) {
        return STATUS(InvalidArgument, "Invalid Timestamp: wrong format of input string", e.what());
      }
    }
  }
  return STATUS(InvalidArgument, "Invalid Timestamp: wrong format of input string");
}

Timestamp DateTime::TimestampFromInt(const int64_t val, const InputFormat& input_format) {
  return Timestamp(AdjustPrecision(val, input_format.input_precision, kInternalPrecision));
}

Timestamp DateTime::TimestampNow() {
  const local_date_time now = local_microsec_clock::local_time(kUtcTimezone);
  const local_date_time epoch(kEpochTime, kUtcTimezone);
  return Timestamp((now - epoch).total_microseconds());
}

Result<uint32_t> DateTime::DateFromString(const std::string& str) {
  // Regex for date format "yyyy-mm-dd"
  static const regex date_format("(-?\\d{1,7})-(\\d{1,2})-(\\d{1,2})");
  std::smatch m;
  if (!std::regex_match(str, m, date_format)) {
    return STATUS(InvalidArgument, "Invalid date format");
  }
  const int year = stoi(m.str(1));
  const int month = stoi(m.str(2));
  const int day = stoi(m.str(3));
  if (month < 1 || month > 12) {
    return STATUS(InvalidArgument, "Invalid month");
  }
  if (day < 1 || day > 31) {
    return STATUS(InvalidArgument, "Invalid day of month");
  }
  try {
    return (date(year, month, day) - kEpochDate).days() + kEpochDateOffset;
  } catch (std::exception& e) {
    return STATUS(InvalidArgument, "Invalid date", e.what());
  }
}

Result<uint32_t> DateTime::DateFromTimestamp(const Timestamp timestamp) {
  const int64_t date = timestamp.ToInt64() / kDayInMicroSeconds + kEpochDateOffset;
  if (date < std::numeric_limits<uint32_t>::min() || date > std::numeric_limits<uint32_t>::max()) {
    return STATUS(InvalidArgument, "Invalid date");
  }
  return date;
}

Result<uint32_t> DateTime::DateFromUnixTimestamp(const int64_t unix_timestamp) {
  const int64_t date = unix_timestamp / kDayInMilliSeconds + kEpochDateOffset;
  if (date < std::numeric_limits<uint32_t>::min() || date > std::numeric_limits<uint32_t>::max()) {
    return STATUS(InvalidArgument, "Invalid date");
  }
  return date;
}

Result<string> DateTime::DateToString(const uint32_t date) {
  try {
    const auto d = kEpochDate + date_duration(date - kEpochDateOffset);
    return boost::gregorian::to_iso_extended_string(d);
  } catch (std::exception& e) {
    return STATUS(InvalidArgument, "Invalid date", e.what());
  }
}

Timestamp DateTime::DateToTimestamp(uint32_t date) {
  return Timestamp((static_cast<int64_t>(date) - kEpochDateOffset) * kDayInMicroSeconds);
}

int64_t DateTime::DateToUnixTimestamp(uint32_t date) {
  return (static_cast<int64_t>(date) - kEpochDateOffset) * kDayInMilliSeconds;
}

uint32_t DateTime::DateNow() {
  const local_date_time now = local_microsec_clock::local_time(kUtcTimezone);
  const date today = boost::gregorian::date_from_tm(to_tm(now));
  return (today - kEpochDate).days() + kEpochDateOffset;
}

Result<int64_t> DateTime::TimeFromString(const std::string& str) {
  // Regex for time format "hh:mm:ss[.fffffffff]"
  static const regex time_format("(\\d{1,2}):(\\d{1,2}):(\\d{1,2})(\\.(\\d{0,9}))?");
  std::smatch m;
  if (!std::regex_match(str, m, time_format)) {
    return STATUS(InvalidArgument, "Invalid time format");
  }
  const int64_t hour = stoi(m.str(1));
  const int64_t minute = stoi(m.str(2));
  const int64_t second = stoi(m.str(3));
  const int64_t nano_sec = m.str(5).empty() ? 0 : (stoi(m.str(5)) * pow(10, 9 - m.str(5).size()));
  if (hour < 0 || hour > 23) {
    return STATUS(InvalidArgument, "Invalid hour");
  }
  if (minute < 0 || minute > 59) {
    return STATUS(InvalidArgument, "Invalid minute");
  }
  if (second < 0 || second > 59) {
    return STATUS(InvalidArgument, "Invalid second");
  }
  return ((hour * 60 + minute) * 60 + second) * 1000000000 + nano_sec;
}

Result<string> DateTime::TimeToString(int64_t time) {
  if (time < 0) {
    return STATUS(InvalidArgument, "Invalid time");
  }
  const int nano_sec = time % 1000000000; time /= 1000000000;
  const int second = time % 60; time /= 60;
  const int minute = time % 60; time /= 60;
  const int hour = time;
  if (hour > 23) {
    return STATUS(InvalidArgument, "Invalid hour");
  }
  char buffer[19]; // "hh:mm:ss[.fffffffff]"
  const int result = snprintf(buffer, sizeof(buffer), "%2.2d:%2.2d:%2.2d.%9.9d",
                              hour, minute, second, nano_sec);
  CHECK(result > 0 && result < sizeof(buffer)) << "Unexpected snprintf result: " << result;
  return buffer;
}

int64_t DateTime::TimeNow() {
  return (TimestampNow().ToInt64() % kDayInMicroSeconds) * 1000;
}

int64_t DateTime::AdjustPrecision(int64_t val,
                                  int input_precision,
                                  const int output_precision) {
  while (input_precision < output_precision) {
    // In case of overflow we just return max/min values -- this is needed for correctness of
    // comparison operations and is similar to Cassandra behaviour.
    if (val > kInt64MaxOverTen) {
      return INT64_MAX;
    } else if (val < kInt64MinOverTen) {
      return INT64_MIN;
    } else {
      val *= 10;
    }
    input_precision += 1;
  }
  while (input_precision > output_precision) {
    val /= 10;
    input_precision -= 1;
  }
  return val;
}

string DateTime::TimestampToString(const Timestamp timestamp, const OutputFormat& output_format) {
  std::ostringstream ss;
  ss.imbue(output_format.output_locale);
  ptime pt = kEpochTime + microseconds(timestamp.value());
  try {
    local_date_time ldt(pt, kUtcTimezone);
    ss << ldt;
  } catch (...) {
    // If we cannot produce a valid date, default to showing the exact timestamp value.
    // This can happen if timestamp value is outside the standard year range (1400..10000).
    ss << timestamp.value();
  }
  return ss.str();
}

const DateTime::InputFormat DateTime::CqlInputFormat = []() -> InputFormat {
  // declaring format components used to construct regexes below
  string fmt_empty = "()";
  string date_fmt = "(\\d{4})-(\\d{1,2})-(\\d{1,2})";
  string time_fmt = "(\\d{1,2}):(\\d{1,2}):(\\d{1,2})";
  string time_fmt_no_sec = "(\\d{1,2}):(\\d{1,2})" + fmt_empty;
  string time_empty = fmt_empty + fmt_empty + fmt_empty;
  string frac_fmt = "\\.(\\d{1,3})";
  // Offset, i.e. +/-xx:xx, +/-0000, timezone parser will do additional checking.
  string tzX_fmt = "((?:\\+|-)\\d{2}:?\\d{2})";
  // Timezone name, abbreviation, or offset (preceded by space), e.g. PDT, UDT+/-xx:xx, etc..
  // At this point this allows anything that starts with a letter or '+' (after space), and leaves
  // further processing to the timezone parser.
  string tzZ_fmt = " ([a-zA-Z\\+].+)";

  // These cases match the valid Cassandra input formats
  vector<std::regex> regexes {
      // e.g. "1992-06-04 12:30" or "1992-6-4 12:30"
      regex(date_fmt + " " + time_fmt_no_sec + fmt_empty + fmt_empty),
      // e.g. "1992-06-04 12:30+04:00" or "1992-6-4 12:30-04:30"
      regex(date_fmt + " " + time_fmt_no_sec + fmt_empty + tzX_fmt),
      // e.g. "1992-06-04 12:30 UTC+04:00" or "1992-6-4 12:30 UTC-04:30"
      regex(date_fmt + " " + time_fmt_no_sec + fmt_empty + tzZ_fmt),
      // e.g. "1992-06-04 12:30.321" or "1992-6-4 12:30.12"
      regex(date_fmt + " " + time_fmt_no_sec + frac_fmt + fmt_empty),
      // e.g. "1992-06-04 12:30.321+04:00" or "1992-6-4 12:30.12-04:30"
      regex(date_fmt + " " + time_fmt_no_sec + frac_fmt + tzX_fmt),
      // e.g. "1992-06-04 12:30.321 UTC+04:00" or "1992-6-4 12:30.12 UTC-04:30"
      regex(date_fmt + " " + time_fmt_no_sec + frac_fmt + tzZ_fmt),
      // e.g. "1992-06-04 12:30:45" or "1992-6-4 12:30:45"
      regex(date_fmt + " " + time_fmt + fmt_empty + fmt_empty),
      // e.g. "1992-06-04 12:30:45+04:00" or "1992-6-4 12:30:45-04:30"
      regex(date_fmt + " " + time_fmt + fmt_empty + tzX_fmt),
      // e.g. "1992-06-04 12:30:45 UTC+04:00" or "1992-6-4 12:30:45 UTC-04:30"
      regex(date_fmt + " " + time_fmt + fmt_empty + tzZ_fmt),
      // e.g. "1992-06-04 12:30:45.321" or "1992-6-4 12:30:45.12"
      regex(date_fmt + " " + time_fmt + frac_fmt + fmt_empty),
      // e.g. "1992-06-04 12:30:45.321+04:00" or "1992-6-4 12:30:45.12-04:30"
      regex(date_fmt + " " + time_fmt + frac_fmt + tzX_fmt),
      // e.g. "1992-06-04 12:30:45.321 UTC+04:00" or "1992-6-4 12:30:45.12 UTC-04:30"
      regex(date_fmt + " " + time_fmt + frac_fmt + tzZ_fmt),
      // e.g. "1992-06-04T12:30" or "1992-6-4T12:30"
      regex(date_fmt + "T" + time_fmt_no_sec + fmt_empty + fmt_empty),
      // e.g. "1992-06-04T12:30+04:00" or "1992-6-4T12:30-04:30"
      regex(date_fmt + "T" + time_fmt_no_sec + fmt_empty + tzX_fmt),
      // e.g. "1992-06-04T12:30 UTC+04:00" or "1992-6-4T12:30TUTC-04:30"
      regex(date_fmt + "T" + time_fmt_no_sec + fmt_empty + tzZ_fmt),
      // e.g. "1992-06-04T12:30.321" or "1992-6-4T12:30.12"
      regex(date_fmt + "T" + time_fmt_no_sec + frac_fmt + fmt_empty),
      // e.g. "1992-06-04T12:30.321+04:00" or "1992-6-4T12:30.12-04:30"
      regex(date_fmt + "T" + time_fmt_no_sec + frac_fmt + tzX_fmt),
      // e.g. "1992-06-04T12:30.321 UTC+04:00" or "1992-6-4T12:30.12 UTC-04:30"
      regex(date_fmt + "T" + time_fmt_no_sec + frac_fmt + tzZ_fmt),
      // e.g. "1992-06-04T12:30:45" or "1992-6-4T12:30:45"
      regex(date_fmt + "T" + time_fmt + fmt_empty + fmt_empty),
      // e.g. "1992-06-04T12:30:45+04:00" or "1992-6-4T12:30:45-04:30"
      regex(date_fmt + "T" + time_fmt + fmt_empty + tzX_fmt),
      // e.g. "1992-06-04T12:30:45 UTC+04:00" or "1992-6-4T12:30:45 UTC-04:30"
      regex(date_fmt + "T" + time_fmt + fmt_empty + tzZ_fmt),
      // e.g. "1992-06-04T12:30:45.321" or "1992-6-4T12:30:45.12"
      regex(date_fmt + "T" + time_fmt + frac_fmt + fmt_empty),
      // e.g. "1992-06-04T12:30:45.321+04:00" or "1992-6-4T12:30:45.12-04:30"
      regex(date_fmt + "T" + time_fmt + frac_fmt + tzX_fmt),
      // e.g. "1992-06-04T12:30:45.321 UTC+04:00" or "1992-6-4T12:30:45.12 UTC-04:30"
      regex(date_fmt + "T" + time_fmt + frac_fmt + tzZ_fmt),
      // e.g. "1992-06-04" or "1992-6-4"
      regex(date_fmt + time_empty + fmt_empty + fmt_empty),
      // e.g. "1992-06-04+04:00" or "1992-6-4-04:30"
      regex(date_fmt + time_empty + fmt_empty + tzX_fmt),
      // e.g. "1992-06-04 UTC+04:00" or "1992-6-4 UTC-04:30"
      regex(date_fmt + time_empty + fmt_empty + tzZ_fmt)};
  int input_precision = 3; // Cassandra current default
  return InputFormat(regexes, input_precision);
} ();

const DateTime::OutputFormat DateTime::CqlOutputFormat = OutputFormat(
    locale(locale::classic(), new local_time_facet("%Y-%m-%dT%H:%M:%S.%f%q"))
);

} // namespace yb

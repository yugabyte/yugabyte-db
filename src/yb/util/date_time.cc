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

#include "yb/util/date_time.h"
#include "yb/util/logging.h"
#include "boost/date_time/local_time_adjustor.hpp"
#include "boost/date_time/c_local_time_adjustor.hpp"

using std::locale;
using std::vector;
using std::string;
using std::regex;
using boost::gregorian::date;
using boost::local_time::local_date_time;
using boost::local_time::local_time_facet;
using boost::local_time::local_time_input_facet;
using boost::local_time::not_a_date_time;
using boost::local_time::posix_time_zone;
using boost::posix_time::microseconds;
using boost::posix_time::time_duration;
using boost::posix_time::microsec_clock;

namespace yb {

DateTimeFormatBase::DateTimeFormatBase(const time_type epoch_start, const tz_ptr_type default_tz)
  : epoch_start_(epoch_start), default_tz_(default_tz) {
}

time_type DateTimeFormatBase::epoch_start() const {
  return epoch_start_;
}

tz_ptr_type DateTimeFormatBase::default_tz() const {
  return default_tz_;
}

DateTimeInputFormat::DateTimeInputFormat(const time_type epoch_start,
                                         const tz_ptr_type default_tz,
                                         const vector<regex>& regexes,
                                         const int input_precision)
    : DateTimeFormatBase(epoch_start, default_tz),
      regexes_(regexes),
      input_precision_(input_precision) {
}

vector<regex> DateTimeInputFormat::regexes() const {
  return regexes_;
}

int DateTimeInputFormat::input_precision() const {
  return input_precision_;
}

DateTimeOutputFormat::DateTimeOutputFormat(const time_type epoch_start,
                                           const tz_ptr_type default_tz,
                                           const locale output_locale)
    : DateTimeFormatBase(epoch_start, default_tz), output_locale_(output_locale) {
}

locale DateTimeOutputFormat::output_locale() const {
  return output_locale_;
}

Result<Timestamp> DateTime::TimestampFromString(const string& str,
                                                const DateTimeInputFormat input_format) {
  local_date_time ldt(not_a_date_time);
  std::smatch m;
  bool matched = false;
  // trying first regex to match from the format
  for (auto reg : input_format.regexes()) {
    matched = std::regex_match(str, m, reg);
    if (matched)
      break;
  }

  if (matched) {
    // setting default values where missing
    int year = stoi(m.str(1), nullptr);
    int month = stoi(m.str(2), nullptr);
    int day = stoi(m.str(3), nullptr);
    int hours = (m.str(4).empty())?0:stoi(m.str(4), nullptr);
    int minutes = (m.str(5).empty())?0:stoi(m.str(5), nullptr);
    int seconds = (m.str(6).empty())?0:stoi(m.str(6), nullptr);
    int64_t frac = (m.str(7).empty())?0:stoi(m.str(7), nullptr);
    frac = AdjustPrecision(frac, m.str(7).size(), time_duration::num_fractional_digits());
    // constructing date_time and getting difference from epoch to set as Timestamp value
    try {
      tz_ptr_type tz = input_format.default_tz(); // default
      if (!m.str(8).empty()) {
        tz = tz_ptr_type(new posix_time_zone(m.str(8)));
      }
      local_date_time ldt(date(year, month, day), time_duration(hours, minutes, seconds, frac),
          tz, local_date_time::NOT_DATE_TIME_ON_ERROR);
      local_date_time epoch(input_format.epoch_start(), GetUtcTimezone());
      time_duration diff = ldt - epoch;
      int64_t ts = diff.total_microseconds();
      return Timestamp(ts);
    } catch (std::exception& e) {
      return STATUS(InvalidArgument, "Invalid Timestamp: wrong format of input string");
    }
  } else {
    return STATUS(InvalidArgument, "Invalid Timestamp: wrong format of input string");
  }
}

Timestamp DateTime::TimestampFromInt(const int64_t val, const DateTimeInputFormat input_format) {
  int input_precision = input_format.input_precision();
  int64_t adj_val = AdjustPrecision(val, input_precision, kInternalPrecision);
  return Timestamp(adj_val);
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

string DateTime::TimestampToString(const Timestamp timestamp,
                                   const DateTimeOutputFormat output_format) {
  std::ostringstream ss;
  ss.imbue(output_format.output_locale());
  time_type pt = output_format.epoch_start() + microseconds(timestamp.value());
  try {
    local_date_time ldt(pt, output_format.default_tz());
    ss << ldt;
  } catch (...) {
    // If we cannot produce a valid date, default to showing the exact timestamp value.
    // This can happen if timestamp value is outside the standard year range (1400..10000).
    ss << timestamp.value();
  }
  return ss.str();
}

DateTimeInputFormat DateTime::CqlDateTimeInputFormat = []() -> DateTimeInputFormat {
  time_type epoch_start(date(1970, 1, 1));
  tz_ptr_type default_tz = GetSystemTimezone();
  // declaring format components used to construct regexes below
  string fmt_empty = "()";
  string date_fmt = "(\\d{4})-(\\d{1,2})-(\\d{1,2})";
  string time_fmt = "(\\d{1,2}):(\\d{1,2}):(\\d{1,2})";
  string time_fmt_no_sec = "(\\d{1,2}):(\\d{1,2})" + fmt_empty;
  string time_empty = fmt_empty + fmt_empty + fmt_empty;
  string frac_fmt = "\\.(\\d{1,3})";
  string tzX_fmt = "((?:\\+|-).+)";
  string tzZ_fmt = " (.+)";

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
  return DateTimeInputFormat(epoch_start, default_tz, regexes, input_precision);
} ();

DateTimeOutputFormat DateTime::CqlDateTimeOutputFormat = DateTimeOutputFormat(
    time_type(date(1970, 1, 1)),
    GetUtcTimezone(),
    locale(locale::classic(), new local_time_facet("%Y-%m-%d %H:%M:%S.%f%q"))
);

// TODO (mihnea) find a better way to reliably get the system timezone
tz_ptr_type DateTime::GetSystemTimezone() {
  // getting time offset
  time_type t1 = microsec_clock::local_time();
  typedef boost::date_time::c_local_adjustor<time_type> local_adj;
  time_type t2 = local_adj::utc_to_local(t1);
  time_duration diff = t2 - t1;
  // converting offset value to valid timezone id (e.g. +06:00)
  std::ostringstream ss;
  std::string sign = (diff.hours() >= 0)?"+":"-";
  ss << sign << std::setfill('0') << std::setw(2) << diff.hours()
     << ":" << std::setw(2) << diff.minutes();
  tz_ptr_type sys_tz(new posix_time_zone(ss.str()));
  return sys_tz;
}

tz_ptr_type DateTime::GetUtcTimezone() {
  return tz_ptr_type(new posix_time_zone("UTC"));
}

} // namespace yb

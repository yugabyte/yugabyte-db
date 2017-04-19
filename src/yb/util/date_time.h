//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Utilities for DateTime parsing, processing and formatting
// TODO: some parsing and formatting settings (e.g. default timezone) should be configurable using a
// config file or YQL functions
// currently hardcoding default_timezone(UTC), precision, output format and epoch
//--------------------------------------------------------------------------------------------------


#ifndef YB_UTIL_DATE_TIME_H_
#define YB_UTIL_DATE_TIME_H_


#include <locale>
#include <regex>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
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

  static Status TimestampFromString(const std::string& str,
                                    Timestamp* timestamp,
                                    DateTimeInputFormat input_format = CqlDateTimeInputFormat);
  static Timestamp TimestampFromInt(int64_t val,
                                    DateTimeInputFormat input_format = CqlDateTimeInputFormat);
  static std::string TimestampToString(
      Timestamp timestamp, DateTimeOutputFormat output_format = CqlDateTimeOutputFormat);

  static tz_ptr_type GetSystemTimezone();
  static tz_ptr_type GetUtcTimezone();
  static int64_t AdjustPrecision(int64_t val, int input_precision, int output_precision);
  static const int64_t internal_precision = 6; // microseconds
};

} // namespace yb

#endif // YB_UTIL_DATE_TIME_H_

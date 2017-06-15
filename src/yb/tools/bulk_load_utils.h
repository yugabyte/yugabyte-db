// Copyright (c) YugaByte, Inc.

#ifndef YB_TOOLS_BULK_LOAD_UTILS_H
#define YB_TOOLS_BULK_LOAD_UTILS_H

#include "yb/util/date_time.h"
#include "yb/util/status.h"
#include "yb/util/timestamp.h"

namespace yb {
namespace tools {

constexpr const char* kNullStringEscaped = "\\n";
typedef boost::tokenizer< boost::escaped_list_separator<char> , std::string::const_iterator,
    std::string> CsvTokenizer;

CHECKED_STATUS CheckedStoi(const string& str, int32_t* val) {
  try {
    size_t pos;
    *val = stoi(str, &pos);
    if (pos != str.size()) {
      return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid integer", str);
    }
  } catch(std::exception& e) {
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid integer", str);
  }
  return Status::OK();
}

CHECKED_STATUS CheckedStol(const string& str, int64_t* val) {
  try {
    size_t pos;
    *val = stol(str, &pos);
    if (pos != str.size()) {
      return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid long", str);
    }
  } catch(std::exception& e) {
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid long", str);
  }
  return Status::OK();
}

CHECKED_STATUS CheckedStold(const string& str, double* val) {
  try {
    size_t pos;
    *val = stold(str, &pos);
    if (pos != str.size()) {
      return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid double", str);
    }
  } catch(std::exception& e) {
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid double", str);
  }
  return Status::OK();
}

CHECKED_STATUS TimestampFromString(const std::string& str, Timestamp* ts) {
  int64_t val;
  if (CheckedStol(str, &val).ok()) {
    *ts = DateTime::TimestampFromInt(val);
  } else {
    return DateTime::TimestampFromString(str, ts);
  }
  return Status::OK();
}

bool IsNull(std::string str) {
  boost::to_lower(str);
  return str == kNullStringEscaped;
}

CsvTokenizer Tokenize(const std::string& line) {
  boost::escaped_list_separator<char> seps('\\', ',', '\"');
  CsvTokenizer tokenizer(line, seps);
  return tokenizer;
}

} // namespace tools
} // namespace yb

#endif // YB_TOOLS_BULK_LOAD_UTILS_H

// Copyright (c) YugaByte, Inc.

#include "yb/util/bytes_formatter.h"

#include <assert.h>

#include <glog/logging.h>

#include "yb/gutil/stringprintf.h"

using std::string;

namespace yb {
namespace util {

string FormatBytesAsStr(const char* data, size_t n, QuotesType quotes_type) {
  string result;

  char quote = '"';
  switch (quotes_type) {
    case QuotesType::kSingleQuotes:
      quote = '\'';
      break;
    case QuotesType::kDoubleQuotes:
      quote = '"';
      break;
  }

  result.push_back(quote);
  const char* end = data + n;
  for (const char* p = data; p != end; ++p) {
    uint8_t c = static_cast<uint8_t>(*p);
    if (c == quote) {
      result.push_back('\\');
      result.push_back(quote);
    } else if (c == '\\') {
      result.append("\\\\");
    } else if (isgraph(c) || c == ' ') {
      result.push_back(c);
    } else {
      result.append(StringPrintf("\\x%02x", c));
    }
  }
  result.push_back(quote);
  return result;
}

string FormatBytesAsStr(const string& s, QuotesType quotes_type) {
  return FormatBytesAsStr(s.c_str(), s.size(), quotes_type);
}

}  // namespace util
}  // namespace yb

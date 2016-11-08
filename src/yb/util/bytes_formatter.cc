// Copyright (c) YugaByte, Inc.

#include "yb/util/bytes_formatter.h"

#include <assert.h>

#include <glog/logging.h>

#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"

using std::string;
using strings::Substitute;

namespace yb {
namespace util {

string FormatBytesAsStr(const char* data,
                        const size_t n,
                        const QuotesType quotes_type,
                        const size_t max_length) {
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

  // Not including the current character we're looking at. Cast to a signed int to avoid underflow.
  int64_t bytes_left = static_cast<int64_t>(n - 1);

  for (const char* p = data; p != end; ++p, --bytes_left) {
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
    // See if we went above the max size. Don't bother if there is only one byte left to print,
    // so that we can always say "bytes".
    if (result.size() >= max_length && bytes_left > 1) {
      result.append(Substitute("<...$0 bytes skipped>", bytes_left));
      break;
    }
  }
  result.push_back(quote);
  return result;
}

string FormatBytesAsStr(const string& s, QuotesType quotes_type, size_t max_length) {
  return FormatBytesAsStr(s.c_str(), s.size(), quotes_type, max_length);
}

}  // namespace util
}  // namespace yb

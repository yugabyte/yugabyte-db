// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_BYTES_FORMATTER_H
#define YB_UTIL_BYTES_FORMATTER_H

#include <string>

#include "yb/util/bytes_formatter.h"

namespace yb {
namespace util {

enum class QuotesType {
  kSingleQuotes,
  kDoubleQuotes,
  kDefaultQuoteType = kDoubleQuotes
};

// Formats the given sequence of characters as a human-readable string with quotes of the given type
// added around it. Quotes embedded inside the character sequence are escaped using a backslash.
// Backslashes themselves are escaped too. Non-ASCII characters are represented as "\x??".
std::string FormatBytesAsStr(const char* data,
                             size_t n,
                             QuotesType quote_type = QuotesType::kDefaultQuoteType);

// Similar to FormatBytesAsStr(const char*, size_t, quote_type), but takes std::string.
std::string FormatBytesAsStr(const std::string& s,
                             QuotesType quote_type = QuotesType::kDefaultQuoteType);

}
}

#endif

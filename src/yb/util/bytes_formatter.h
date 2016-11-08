// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_BYTES_FORMATTER_H
#define YB_UTIL_BYTES_FORMATTER_H

#include <limits>
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
//
// @param data        The raw bytes to format.
// @param n           Number of bytes pointed by data.
// @param quote_type  Whether to use single or double quotes.
// @param max_length  Maximum length of a string to produce. This is advisory only, because we may
//                    still need to use a few more characters to close the string.
std::string FormatBytesAsStr(const char* data,
                             size_t n,
                             QuotesType quote_type = QuotesType::kDefaultQuoteType,
                             size_t max_length = std::numeric_limits<size_t>::max());

// Similar to FormatBytesAsStr(const char*, size_t, quote_type), but takes std::string.
std::string FormatBytesAsStr(const std::string& s,
                             QuotesType quote_type = QuotesType::kDefaultQuoteType,
                             size_t max_length = std::numeric_limits<size_t>::max());

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_BYTES_FORMATTER_H

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

#pragma once

#include <limits>
#include <string>

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/slice.h"

namespace yb {

enum class QuotesType {
  kSingleQuotes,
  kDoubleQuotes,
  kDefaultQuoteType = kDoubleQuotes
};

YB_DEFINE_ENUM(BinaryOutputFormat, (kEscaped)(kHex)(kEscapedAndHex));

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
std::string FormatBytesAsStr(const std::string_view& s,
                             QuotesType quote_type = QuotesType::kDefaultQuoteType,
                             size_t max_length = std::numeric_limits<size_t>::max());

// Similar to FormatBytesAsStr(const char*, size_t, quote_type), but takes a yb::util::Slice.
std::string FormatSliceAsStr(const Slice& slice,
                             QuotesType quote_type = QuotesType::kDefaultQuoteType,
                             size_t max_length = std::numeric_limits<size_t>::max());

std::string FormatSliceAsStr(
    const Slice& slice,
    BinaryOutputFormat output_format,
    QuotesType quote_type = QuotesType::kDefaultQuoteType,
    size_t max_length = std::numeric_limits<size_t>::max());

}  // namespace yb

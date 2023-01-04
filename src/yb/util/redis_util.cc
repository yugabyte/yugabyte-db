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

#include "yb/util/redis_util.h"

#include "yb/gutil/macros.h"

namespace yb {

using std::string;

namespace {

// Returns whether a given string matches a redis pattern. Ported from Redis.
bool RedisPatternMatchWithLen(
    const char* pattern, size_t pattern_len, const char* string, size_t str_len, bool ignore_case) {
  while (pattern_len > 0) {
    switch (pattern[0]) {
      case '*':
        while (pattern[1] == '*') {
          pattern++;
          pattern_len--;
        }
        if (pattern_len == 1) {
          return true; /* match */
        }
        while (str_len > 0) {
          if (RedisPatternMatchWithLen(
                  pattern + 1, pattern_len - 1, string, str_len, ignore_case)) {
            return true; /* match */
          }
          string++;
          str_len--;
        }
        return false; /* no match */
        break;
      case '?':
        if (str_len == 0) {
          return 0; /* no match */
        }
        string++;
        str_len--;
        break;
      case '[': {
        bool not_match, match;

        pattern++;
        pattern_len--;
        not_match = pattern[0] == '^';
        if (not_match) {
          pattern++;
          pattern_len--;
        }
        match = false;
        while (true) {
          if (pattern[0] == '\\' && pattern_len >= 2) {
            pattern++;
            pattern_len--;
            if (pattern[0] == string[0]) {
              match = true;
            }
          } else if (pattern[0] == ']') {
            break;
          } else if (pattern_len == 0) {
            pattern--;
            pattern_len++;
            break;
          } else if (pattern[1] == '-' && pattern_len >= 3) {
            int start = pattern[0];
            int end = pattern[2];
            int c = string[0];
            if (start > end) {
              int t = start;
              start = end;
              end = t;
            }
            if (ignore_case) {
              start = tolower(start);
              end = tolower(end);
              c = tolower(c);
            }
            pattern += 2;
            pattern_len -= 2;
            if (c >= start && c <= end) {
              match = true;
            }
          } else {
            if (!ignore_case) {
              if (pattern[0] == string[0]) {
                match = true;
              }
            } else {
              if (tolower(static_cast<int>(pattern[0])) == tolower(static_cast<int>(string[0]))) {
                match = true;
              }
            }
          }
          pattern++;
          pattern_len--;
        }
        if (not_match) {
          match = !match;
        }
        if (!match) {
          return false; /* no match */
        }
        string++;
        str_len--;
        break;
      }
      case '\\':
        if (pattern_len >= 2) {
          pattern++;
          pattern_len--;
        }
        FALLTHROUGH_INTENDED;
      default:
        if (!ignore_case) {
          if (pattern[0] != string[0]) {
            return false; /* no match */
          }
        } else {
          if (tolower(static_cast<int>(pattern[0])) != tolower(static_cast<int>(string[0]))) {
            return false; /* no match */
          }
        }
        string++;
        str_len--;
        break;
    }
    pattern++;
    pattern_len--;
    if (str_len == 0) {
      while (*pattern == '*') {
        pattern++;
        pattern_len--;
      }
      break;
    }
  }
  return pattern_len == 0 && str_len == 0;
}

} // namespace

bool RedisPatternMatch(
    const std::string_view& pattern, const std::string_view& string, bool ignore_case) {
  return RedisPatternMatchWithLen(
      pattern.data(), pattern.length(), string.data(), string.length(), ignore_case);
}

} // namespace yb

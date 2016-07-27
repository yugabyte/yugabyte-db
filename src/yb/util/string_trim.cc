// Copyright (c) YugaByte, Inc.

#include "yb/util/string_trim.h"

#include <algorithm>
#include <cctype>
#include <locale>
#include <string>

using std::string;

namespace yb {
namespace util {

string ApplyEagerLineContinuation(const string& s) {
  string result;
  size_t length = s.size();
  for (size_t i = 0; i < length; ++i) {
    while (i > 0 && s[i - 1] == '\\' && s[i] == '\n') {
      // Remove the previous character (backslash).
      result.resize(result.size() - 1);
      // Consume the leading whitespace on the new line. This may be different from how backslash
      // line continuation works in other places, but this is what we need for some of our expected
      // test output handling.

      ++i;  // skip the new line
      // Skip whitespace on the new line.
      while (i < length && std::isspace(s[i], std::locale::classic())) {
        ++i;
      }
    }

    if (i < length) {
      result.push_back(s[i]);
    }
  }
  return result;
}

}
}

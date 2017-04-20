// Copyright (c) YugaByte, Inc.

#include "yb/util/string_trim.h"

#include <algorithm>
#include <cctype>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using std::string;
using std::vector;
using std::istringstream;

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

namespace {

int CountLeadingSpaces(const string& line) {
  int num_spaces = 0;
  for (char c : line) {
    if (c != ' ')
      break;
    num_spaces++;
  }
  return num_spaces;
}

}  // anonymous namespace

string LeftShiftTextBlock(const std::string& s) {
  istringstream input(s);
  vector<string> lines;

  // Split the string into lines.  This could be implemented with boost::split with less data
  // copying and memory allocation.
  while (!input.eof()) {
    lines.emplace_back();
    getline(input, lines.back());
  }

  int min_leading_spaces = std::numeric_limits<int>::max();
  for (const string& line : lines) {
    const int num_spaces = CountLeadingSpaces(line);
    // We're not counting empty lines when calculating the minimum number of leading spaces.
    // TODO: we're counting all-space lines as empty but not if they have e.g. tab chracters.
    if (num_spaces != line.size() && num_spaces < min_leading_spaces) {
      min_leading_spaces = num_spaces;
    }
  }

  string result;
  bool need_newline = false;
  for (const string& line : lines) {
    if (need_newline) {
      result.push_back('\n');
    }
    need_newline = true;
    if (min_leading_spaces <= line.size()) {
      result += line.substr(min_leading_spaces, line.size() - min_leading_spaces);
    }
  }
  return result;
}

}  // namespace util
}  // namespace yb

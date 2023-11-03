// Copyright 2008 and onwards Google Inc.  All rights reserved.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/gutil/strings/join.h"

#include <memory>

#include "yb/util/logging.h"

#include "yb/gutil/strings/ascii_ctype.h"
#include "yb/gutil/strings/escaping.h"

using std::vector;
using std::string;
using std::map;
using std::pair;

// ----------------------------------------------------------------------
// JoinUsing()
//    This merges a vector of string components with delim inserted
//    as separaters between components.
//    This is essentially the same as JoinUsingToBuffer except
//    the return result is dynamically allocated using "new char[]".
//    It is the caller's responsibility to "delete []" the
//
//    If result_length_p is not NULL, it will contain the length of the
//    result string (not including the trailing '\0').
// ----------------------------------------------------------------------
char* JoinUsing(const vector<const char*>& components,
                const char* delim,
                size_t* result_length_p) {
  const auto num_components = components.size();
  const auto delim_length = strlen(delim);
  auto num_chars = num_components > 1 ? delim_length * (num_components - 1) : 0;
  for (size_t i = 0; i < num_components; ++i)
    num_chars += strlen(components[i]);

  auto res_buffer = new char[num_chars + 1];
  return JoinUsingToBuffer(components, delim, num_chars+1,
                           res_buffer, result_length_p);
}

// ----------------------------------------------------------------------
// JoinUsingToBuffer()
//    This merges a vector of string components with delim inserted
//    as separaters between components.
//    User supplies the result buffer with specified buffer size.
//    The result is also returned for convenience.
//
//    If result_length_p is not NULL, it will contain the length of the
//    result string (not including the trailing '\0').
// ----------------------------------------------------------------------
char* JoinUsingToBuffer(const vector<const char*>& components,
                         const char* delim,
                         size_t result_buffer_size,
                         char* result_buffer,
                         size_t* result_length_p) {
  CHECK(result_buffer != nullptr);
  const auto num_components = components.size();
  const auto max_str_len = result_buffer_size - 1;
  char* curr_dest = result_buffer;
  size_t num_chars = 0;
  for (size_t i = 0; (i < num_components) && (num_chars < max_str_len); ++i) {
    const char* curr_src = components[i];
    while ((*curr_src != '\0') && (num_chars < max_str_len)) {
      *curr_dest = *curr_src;
      ++num_chars;
      ++curr_dest;
      ++curr_src;
    }
    if (i != (num_components-1)) {  // not the last component ==> add separator
      curr_src = delim;
      while ((*curr_src != '\0') && (num_chars < max_str_len)) {
        *curr_dest = *curr_src;
        ++num_chars;
        ++curr_dest;
        ++curr_src;
      }
    }
  }

  if (result_buffer_size > 0)
    *curr_dest = '\0';  // add null termination
  if (result_length_p != nullptr)  // set string length value
    *result_length_p = num_chars;

  return result_buffer;
}

// ----------------------------------------------------------------------
// JoinStrings()
//    This merges a vector of string components with delim inserted
//    as separaters between components.
//    This is essentially the same as JoinUsingToBuffer except
//    it uses strings instead of char *s.
//
// ----------------------------------------------------------------------

void JoinStringsInArray(string const* const* components,
                        size_t num_components,
                        const char* delim,
                        string* result) {
  CHECK(result != nullptr);
  result->clear();
  for (size_t i = 0; i < num_components; i++) {
    if (i > 0) {
      (*result) += delim;
    }
    (*result) += *(components[i]);
  }
}

void JoinStringsInArray(string const *components,
                        size_t num_components,
                        const char *delim,
                        string *result) {
  JoinStringsIterator(components,
                      components + num_components,
                      delim,
                      result);
}

// ----------------------------------------------------------------------
// JoinMapKeysAndValues()
// JoinVectorKeysAndValues()
//    This merges the keys and values of a string -> string map or pair
//    of strings vector, with one delim (intra_delim) between each key
//    and its associated value and another delim (inter_delim) between
//    each key/value pair.  The result is returned in a string (passed
//    as the last argument).
// ----------------------------------------------------------------------

void JoinMapKeysAndValues(const map<string, string>& components,
                          const GStringPiece& intra_delim,
                          const GStringPiece& inter_delim,
                          string* result) {
  JoinKeysAndValuesIterator(components.begin(), components.end(),
                            intra_delim, inter_delim,
                            result);
}

void JoinVectorKeysAndValues(const vector< pair<string, string> >& components,
                             const GStringPiece& intra_delim,
                             const GStringPiece& inter_delim,
                             string* result) {
  JoinKeysAndValuesIterator(components.begin(), components.end(),
                            intra_delim, inter_delim,
                            result);
}

// ----------------------------------------------------------------------
// JoinCSVLine()
//    This function is the inverse of SplitCSVLineWithDelimiter() in that the
//    string returned by JoinCSVLineWithDelimiter() can be passed to
//    SplitCSVLineWithDelimiter() to get the original string vector back.
//    Quotes and escapes the elements of original_cols according to CSV quoting
//    rules, and the joins the escaped quoted strings with commas using
//    JoinStrings().  Note that JoinCSVLineWithDelimiter() will not necessarily
//    return the same string originally passed in to
//    SplitCSVLineWithDelimiter(), since SplitCSVLineWithDelimiter() can handle
//    gratuitous spacing and quoting. 'output' must point to an empty string.
//
//    Example:
//     [Google], [x], [Buchheit, Paul], [string with " quoite in it], [ space ]
//     --->  [Google,x,"Buchheit, Paul","string with "" quote in it"," space "]
// ----------------------------------------------------------------------
void JoinCSVLineWithDelimiter(const vector<string>& cols, char delimiter,
                              string* output) {
  CHECK(output);
  CHECK(output->empty());
  vector<string> quoted_cols;

  const string delimiter_str(1, delimiter);
  const string escape_chars = delimiter_str + "\"";

  // If the string contains the delimiter or " anywhere, or begins or ends with
  // whitespace (ie ascii_isspace() returns true), escape all double-quotes and
  // bracket the string in double quotes. string.rbegin() evaluates to the last
  // character of the string.
  for (const auto& col : cols) {
    if ((col.find_first_of(escape_chars) != string::npos) ||
        (!col.empty() && (ascii_isspace(*col.begin()) ||
                              ascii_isspace(*col.rbegin())))) {
      // Double the original size, for escaping, plus two bytes for
      // the bracketing double-quotes, and one byte for the closing \0.
      auto size = 2 * col.size() + 3;
      std::unique_ptr<char[]> buf(new char[size]);

      // Leave space at beginning and end for bracketing double-quotes.
      auto escaped_size = strings::EscapeStrForCSV(col.c_str(), buf.get() + 1, size - 2);
      CHECK_GE(escaped_size, 0) << "Buffer somehow wasn't large enough.";
      CHECK_GE(size, escaped_size + 3)
        << "Buffer should have one space at the beginning for a "
        << "double-quote, one at the end for a double-quote, and "
        << "one at the end for a closing '\0'";
      *buf.get() = '"';
      *((buf.get() + 1) + escaped_size) = '"';
      *((buf.get() + 1) + escaped_size + 1) = '\0';
      quoted_cols.push_back(string(buf.get(), buf.get() + escaped_size + 2));
    } else {
      quoted_cols.push_back(col);
    }
  }
  JoinStrings(quoted_cols, delimiter_str, output);
}

void JoinCSVLine(const vector<string>& cols, string* output) {
  JoinCSVLineWithDelimiter(cols, ',', output);
}

string JoinCSVLine(const vector<string>& cols) {
  string output;
  JoinCSVLine(cols, &output);
  return output;
}

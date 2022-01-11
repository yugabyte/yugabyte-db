// Copyright 2008 and onwards Google, Inc.
//
// #status: RECOMMENDED
// #category: operations on strings
// #summary: Functions for joining strings and numbers using a delimiter.
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
#ifndef YB_GUTIL_STRINGS_JOIN_H
#define YB_GUTIL_STRINGS_JOIN_H

#include <stdio.h>
#include <string.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/strcat.h"    // For backward compatibility.
#include "yb/gutil/strings/stringpiece.h"

using std::back_insert_iterator;
using std::iterator_traits;
using std::map;
using std::multimap;
using std::multiset;
using std::set;
using std::string;
using std::make_pair;
using std::pair;
using std::vector;


// ----------------------------------------------------------------------
// JoinUsing()
//    This concatenates a vector of strings "components" into a new char[]
//    buffer, using the C-string "delim" as a separator between components.
//
//    This is essentially the same as JoinUsingToBuffer except
//    the return result is dynamically allocated using "new char[]".
//    It is the caller's responsibility to "delete []" the char* that is
//    returned.
//
//    If result_length_p is not NULL, it will contain the length of the
//    result string (not including the trailing '\0').
// ----------------------------------------------------------------------
char* JoinUsing(const vector<const char*>& components,
                const char* delim,
                size_t* result_length_p);

// ----------------------------------------------------------------------
// JoinUsingToBuffer()
//    This concatenates a vector of strings "components" into a given char[]
//    buffer, using the C-string "delim" as a separator between components.
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
                        size_t* result_length_p);

// ----------------------------------------------------------------------
// JoinStrings(), JoinStringsIterator(), JoinStringsInArray()
//
//    JoinStrings concatenates a container of strings into a C++ string,
//    using the string "delim" as a separator between components.
//    "components" can be any sequence container whose values are C++ strings
//    or GStringPieces. More precisely, "components" must support STL container
//    iteration; i.e. it must have begin() and end() methods with appropriate
//    semantics, which return forward iterators whose value type is
//    string or GStringPiece. Repeated string fields of protocol messages
//    satisfy these requirements.
//
//    JoinStringsIterator is the same as JoinStrings, except that the input
//    strings are specified with a pair of iterators. The requirements on
//    the iterators are the same as the requirements on components.begin()
//    and components.end() for JoinStrings.
//
//    JoinStringsInArray is the same as JoinStrings, but operates on
//    an array of C++ strings or string pointers.
//
//    There are two flavors of each function, one flavor returns the
//    concatenated string, another takes a pointer to the target string. In
//    the latter case the target string is cleared and overwritten.
// ----------------------------------------------------------------------
template <class CONTAINER>
void JoinStrings(const CONTAINER& components,
                 const GStringPiece& delim,
                 string* result);
template <class CONTAINER>
string JoinStrings(const CONTAINER& components,
                   const GStringPiece& delim);

template <class ITERATOR>
void JoinStringsIterator(const ITERATOR& start,
                         const ITERATOR& end,
                         const GStringPiece& delim,
                         string* result);
template <class ITERATOR>
string JoinStringsIterator(const ITERATOR& start,
                           const ITERATOR& end,
                           const GStringPiece& delim);

// Join the keys of a map using the specified delimiter.
template<typename ITERATOR>
void JoinKeysIterator(const ITERATOR& start,
                      const ITERATOR& end,
                      const GStringPiece& delim,
                      string *result) {
  result->clear();
  for (ITERATOR iter = start; iter != end; ++iter) {
    if (iter == start) {
      StrAppend(result, iter->first);
    } else {
      StrAppend(result, delim, iter->first);
    }
  }
}

template <typename ITERATOR>
string JoinKeysIterator(const ITERATOR& start,
                        const ITERATOR& end,
                        const GStringPiece& delim) {
  string result;
  JoinKeysIterator(start, end, delim, &result);
  return result;
}

// Join the keys and values of a map using the specified delimiters.
template<typename ITERATOR>
void JoinKeysAndValuesIterator(const ITERATOR& start,
                               const ITERATOR& end,
                               const GStringPiece& intra_delim,
                               const GStringPiece& inter_delim,
                               string *result) {
  result->clear();
  for (ITERATOR iter = start; iter != end; ++iter) {
    if (iter == start) {
      StrAppend(result, iter->first, intra_delim, iter->second);
    } else {
      StrAppend(result, inter_delim, iter->first, intra_delim, iter->second);
    }
  }
}

template <typename ITERATOR>
string JoinKeysAndValuesIterator(const ITERATOR& start,
                                 const ITERATOR& end,
                                 const GStringPiece& intra_delim,
                                 const GStringPiece& inter_delim) {
  string result;
  JoinKeysAndValuesIterator(start, end, intra_delim, inter_delim, &result);
  return result;
}

void JoinStringsInArray(string const* const* components,
                        size_t num_components,
                        const char* delim,
                        string* result);
void JoinStringsInArray(string const* components,
                        size_t num_components,
                        const char* delim,
                        string* result);
string JoinStringsInArray(string const* const* components,
                          size_t num_components,
                          const char* delim);
string JoinStringsInArray(string const* components,
                          size_t num_components,
                          const char* delim);

// ----------------------------------------------------------------------
// Definitions of above JoinStrings* methods
// ----------------------------------------------------------------------
template <class CONTAINER>
inline void JoinStrings(const CONTAINER& components,
                        const GStringPiece& delim,
                        string* result) {
  JoinStringsIterator(components.begin(), components.end(), delim, result);
}

template <class CONTAINER>
inline string JoinStrings(const CONTAINER& components,
                          const GStringPiece& delim) {
  string result;
  JoinStrings(components, delim, &result);
  return result;
}

template <class ITERATOR>
void JoinStringsIterator(const ITERATOR& start,
                         const ITERATOR& end,
                         const GStringPiece& delim,
                         string* result) {
  result->clear();

  // Precompute resulting length so we can reserve() memory in one shot.
  if (start != end) {
    auto length = delim.size()*(distance(start, end)-1);
    for (ITERATOR iter = start; iter != end; ++iter) {
      length += iter->size();
    }
    result->reserve(length);
  }

  // Now combine everything.
  for (ITERATOR iter = start; iter != end; ++iter) {
    if (iter != start) {
      result->append(delim.data(), delim.size());
    }
    result->append(iter->data(), iter->size());
  }
}

template <class ITERATOR>
inline string JoinStringsIterator(const ITERATOR& start,
                                  const ITERATOR& end,
                                  const GStringPiece& delim) {
  string result;
  JoinStringsIterator(start, end, delim, &result);
  return result;
}

inline string JoinStringsInArray(string const* const* components,
                                 size_t num_components,
                                 const char* delim) {
  string result;
  JoinStringsInArray(components, num_components, delim, &result);
  return result;
}

inline string JoinStringsInArray(string const* components,
                                 size_t num_components,
                                 const char* delim) {
  string result;
  JoinStringsInArray(components, num_components, delim, &result);
  return result;
}

// ----------------------------------------------------------------------
// JoinMapKeysAndValues()
// JoinHashMapKeysAndValues()
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
                          string* result);
void JoinVectorKeysAndValues(const vector< pair<string, string> >& components,
                             const GStringPiece& intra_delim,
                             const GStringPiece& inter_delim,
                             string* result);

// DEPRECATED(jyrki): use JoinKeysAndValuesIterator directly.
template<typename T>
void JoinHashMapKeysAndValues(const T& container,
                              const GStringPiece& intra_delim,
                              const GStringPiece& inter_delim,
                              string* result) {
  JoinKeysAndValuesIterator(container.begin(), container.end(),
                            intra_delim, inter_delim,
                            result);
}

// ----------------------------------------------------------------------
// JoinCSVLineWithDelimiter()
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
//
// JoinCSVLine()
//    A convenience wrapper around JoinCSVLineWithDelimiter which uses
//    ',' as the delimiter.
// ----------------------------------------------------------------------
void JoinCSVLine(const vector<string>& original_cols, string* output);
string JoinCSVLine(const vector<string>& original_cols);
void JoinCSVLineWithDelimiter(const vector<string>& original_cols,
                              char delimiter,
                              string* output);

// ----------------------------------------------------------------------
// JoinElements()
//    This merges a container of any type supported by StrAppend() with delim
//    inserted as separators between components.  This is essentially a
//    templatized version of JoinUsingToBuffer().
//
// JoinElementsIterator()
//    Same as JoinElements(), except that the input elements are specified
//    with a pair of forward iterators.
// ----------------------------------------------------------------------

template <class ITERATOR>
void JoinElementsIterator(ITERATOR first,
                          ITERATOR last,
                          GStringPiece delim,
                          string* result) {
  result->clear();
  for (ITERATOR it = first; it != last; ++it) {
    if (it != first) {
      StrAppend(result, delim);
    }
    StrAppend(result, *it);
  }
}

template <class ITERATOR>
string JoinElementsIterator(ITERATOR first,
                            ITERATOR last,
                            GStringPiece delim) {
  string result;
  JoinElementsIterator(first, last, delim, &result);
  return result;
}

template <class CONTAINER>
inline void JoinElements(const CONTAINER& components,
                         GStringPiece delim,
                         string* result) {
  JoinElementsIterator(components.begin(), components.end(), delim, result);
}

template <class CONTAINER>
inline string JoinElements(const CONTAINER& components, GStringPiece delim) {
  string result;
  JoinElements(components, delim, &result);
  return result;
}

template <class CONTAINER>
void JoinInts(const CONTAINER& components,
              const char* delim,
              string* result) {
  JoinElements(components, delim, result);
}

template <class CONTAINER>
inline string JoinInts(const CONTAINER& components,
                       const char* delim) {
  return JoinElements(components, delim);
}

#endif  // YB_GUTIL_STRINGS_JOIN_H

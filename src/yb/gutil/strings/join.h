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
#pragma once

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

#include "yb/util/format.h"
#include "yb/util/logging.h"

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
char* JoinUsing(const std::vector<const char*>& components,
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
char* JoinUsingToBuffer(const std::vector<const char*>& components,
                        const char* delim,
                        size_t result_buffer_size,
                        char* result_buffer,
                        size_t* result_length_p);

// ----------------------------------------------------------------------
// JoinStrings(), JoinStringsIterator(), JoinStringsInArray(), JoinStringsLimitCount()
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
//    JoinStringsLimitCount is the same as JoinStrings, but only concatenates a limited number of
//    entries followed by a count of the unconcatenated entries if any; the count is omitted if it
//    is zero.
//
//    There are two flavors of each function, one flavor returns the
//    concatenated string, another takes a pointer to the target string. In
//    the latter case the target string is cleared and overwritten.
// ----------------------------------------------------------------------
template <class CONTAINER>
void JoinStrings(const CONTAINER& components,
                 const GStringPiece& delim,
                 std::string* result);
template <class CONTAINER>
std::string JoinStrings(const CONTAINER& components,
                   const GStringPiece& delim);

template <class ITERATOR>
void JoinStringsIterator(const ITERATOR& start,
                         const ITERATOR& end,
                         const GStringPiece& delim,
                         std::string* result);
template <class ITERATOR>
std::string JoinStringsIterator(const ITERATOR& start,
                           const ITERATOR& end,
                           const GStringPiece& delim);

// Join the keys of a map using the specified delimiter.
template<typename ITERATOR>
void JoinKeysIterator(const ITERATOR& start,
                      const ITERATOR& end,
                      const GStringPiece& delim,
                      std::string *result) {
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
std::string JoinKeysIterator(const ITERATOR& start,
                        const ITERATOR& end,
                        const GStringPiece& delim) {
  std::string result;
  JoinKeysIterator(start, end, delim, &result);
  return result;
}

// Join the keys and values of a map using the specified delimiters.
template<typename ITERATOR>
void JoinKeysAndValuesIterator(const ITERATOR& start,
                               const ITERATOR& end,
                               const GStringPiece& intra_delim,
                               const GStringPiece& inter_delim,
                               std::string *result) {
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
std::string JoinKeysAndValuesIterator(const ITERATOR& start,
                                 const ITERATOR& end,
                                 const GStringPiece& intra_delim,
                                 const GStringPiece& inter_delim) {
  std::string result;
  JoinKeysAndValuesIterator(start, end, intra_delim, inter_delim, &result);
  return result;
}

void JoinStringsInArray(std::string const* const* components,
                        size_t num_components,
                        const char* delim,
                        std::string* result);
void JoinStringsInArray(std::string const* components,
                        size_t num_components,
                        const char* delim,
                        std::string* result);
std::string JoinStringsInArray(std::string const* const* components,
                          size_t num_components,
                          const char* delim);
std::string JoinStringsInArray(std::string const* components,
                          size_t num_components,
                          const char* delim);

template <class CONTAINER>
inline void JoinStringsLimitCount(
    const CONTAINER& components, const GStringPiece& delim, uint32 limit_count,
    std::string* result);

template <class CONTAINER>
inline std::string JoinStringsLimitCount(
    const CONTAINER& components, const GStringPiece& delim, uint32 limit_count);

// ----------------------------------------------------------------------
// Definitions of above JoinStrings* methods
// ----------------------------------------------------------------------
template <class CONTAINER>
inline void JoinStrings(const CONTAINER& components,
                        const GStringPiece& delim,
                        std::string* result) {
  JoinStringsIterator(components.begin(), components.end(), delim, result);
}

template <class CONTAINER>
inline std::string JoinStrings(const CONTAINER& components,
                          const GStringPiece& delim) {
  std::string result;
  JoinStrings(components, delim, &result);
  return result;
}

template <class ITERATOR>
void JoinStringsIterator(const ITERATOR& start,
                         const ITERATOR& end,
                         const GStringPiece& delim,
                         std::string* result) {
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
inline std::string JoinStringsIterator(const ITERATOR& start,
                                  const ITERATOR& end,
                                  const GStringPiece& delim) {
  std::string result;
  JoinStringsIterator(start, end, delim, &result);
  return result;
}

inline std::string JoinStringsInArray(std::string const* const* components,
                                 size_t num_components,
                                 const char* delim) {
  std::string result;
  JoinStringsInArray(components, num_components, delim, &result);
  return result;
}

inline std::string JoinStringsInArray(std::string const* components,
                                 size_t num_components,
                                 const char* delim) {
  std::string result;
  JoinStringsInArray(components, num_components, delim, &result);
  return result;
}

template <class CONTAINER>
inline void JoinStringsLimitCount(
    const CONTAINER& components, const GStringPiece& delim, uint32 limit_count,
    std::string* result) {
  DCHECK_GE(limit_count, 1);
  uint32 count_to_print = std::min(limit_count, static_cast<uint32>(components.size()));
  JoinStringsIterator(components.begin(), components.begin() + count_to_print, delim, result);
  const auto count_skipped = components.size() - count_to_print;
  if (count_skipped > 0) {
    *result += yb::Format(" and $0 others", count_skipped);
  }
}

template <class CONTAINER>
inline std::string JoinStringsLimitCount(
    const CONTAINER& components, const GStringPiece& delim, uint32 limit_count) {
  std::string result;
  JoinStringsLimitCount(components, delim, limit_count, &result);
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

void JoinMapKeysAndValues(const std::map<std::string, std::string>& components,
                          const GStringPiece& intra_delim,
                          const GStringPiece& inter_delim,
                          std::string* result);
void JoinVectorKeysAndValues(const std::vector< std::pair<std::string, std::string> >& components,
                             const GStringPiece& intra_delim,
                             const GStringPiece& inter_delim,
                             std::string* result);

// DEPRECATED(jyrki): use JoinKeysAndValuesIterator directly.
template<typename T>
void JoinHashMapKeysAndValues(const T& container,
                              const GStringPiece& intra_delim,
                              const GStringPiece& inter_delim,
                              std::string* result) {
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
void JoinCSVLine(const std::vector<std::string>& original_cols, std::string* output);
std::string JoinCSVLine(const std::vector<std::string>& original_cols);
void JoinCSVLineWithDelimiter(const std::vector<std::string>& original_cols,
                              char delimiter,
                              std::string* output);

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
                          std::string* result) {
  result->clear();
  for (ITERATOR it = first; it != last; ++it) {
    if (it != first) {
      StrAppend(result, delim);
    }
    StrAppend(result, *it);
  }
}

template <class ITERATOR>
std::string JoinElementsIterator(ITERATOR first,
                            ITERATOR last,
                            GStringPiece delim) {
  std::string result;
  JoinElementsIterator(first, last, delim, &result);
  return result;
}

template <class CONTAINER>
inline void JoinElements(const CONTAINER& components,
                         GStringPiece delim,
                         std::string* result) {
  JoinElementsIterator(components.begin(), components.end(), delim, result);
}

template <class CONTAINER>
inline std::string JoinElements(const CONTAINER& components, GStringPiece delim) {
  std::string result;
  JoinElements(components, delim, &result);
  return result;
}

template <class CONTAINER>
void JoinInts(const CONTAINER& components,
              const char* delim,
              std::string* result) {
  JoinElements(components, delim, result);
}

template <class CONTAINER>
inline std::string JoinInts(const CONTAINER& components,
                       const char* delim) {
  return JoinElements(components, delim);
}

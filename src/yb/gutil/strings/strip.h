// Copyright 2011 Google Inc. All Rights Reserved.
// Refactored from contributions of various authors in strings/strutil.h
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
// This file contains functions that remove a defined part from the string,
// i.e., strip the string.

#pragma once

#include <stddef.h>
#include <string>

#include "yb/gutil/strings/ascii_ctype.h"
#include "yb/gutil/strings/stringpiece.h"

// Given a string and a putative prefix, returns the string minus the
// prefix string if the prefix matches, otherwise the original
// string.
std::string StripPrefixString(GStringPiece str, const GStringPiece& prefix);

// Like StripPrefixString, but return true if the prefix was
// successfully matched.  Write the output to *result.
// It is safe for result to point back to the input string.
bool TryStripPrefixString(GStringPiece str, const GStringPiece& prefix,
                          std::string* result);

// Given a string and a putative suffix, returns the string minus the
// suffix string if the suffix matches, otherwise the original
// string.
std::string StripSuffixString(GStringPiece str, const GStringPiece& suffix);


// Like StripSuffixString, but return true if the suffix was
// successfully matched.  Write the output to *result.
// It is safe for result to point back to the input string.
bool TryStripSuffixString(GStringPiece str, const GStringPiece& suffix,
                          std::string* result);

// ----------------------------------------------------------------------
// StripString
//    Replaces any occurrence of the character 'remove' (or the characters
//    in 'remove') with the character 'replacewith'.
//    Good for keeping html characters or protocol characters (\t) out
//    of places where they might cause a problem.
// ----------------------------------------------------------------------
inline void StripString(char* str, char remove, char replacewith) {
  for (; *str; str++) {
    if (*str == remove)
      *str = replacewith;
  }
}

void StripString(char* str, GStringPiece remove, char replacewith);
void StripString(char* str, int len, GStringPiece remove, char replacewith);
void StripString(std::string* s, GStringPiece remove, char replacewith);

// ----------------------------------------------------------------------
// StripDupCharacters
//    Replaces any repeated occurrence of the character 'dup_char'
//    with single occurrence.  e.g.,
//       StripDupCharacters("a//b/c//d", '/', 0) => "a/b/c/d"
//    Return the number of characters removed
// ----------------------------------------------------------------------
int StripDupCharacters(std::string* s, char dup_char, int start_pos);

// ----------------------------------------------------------------------
// StripWhiteSpace
//    "Removes" whitespace from both sides of string.  Pass in a pointer to an
//    array of characters, and its length.  The function changes the pointer
//    and length to refer to a substring that does not contain leading or
//    trailing spaces; it does not modify the string itself.  If the caller is
//    using NUL-terminated strings, it is the caller's responsibility to insert
//    the NUL character at the end of the substring."
//
//    Note: to be completely type safe, this function should be
//    parameterized as a template: template<typename anyChar> void
//    StripWhiteSpace(anyChar** str, int* len), where the expectation
//    is that anyChar could be char, const char, w_char, const w_char,
//    unicode_char, or any other character type we want.  However, we
//    just provided a version for char and const char.  C++ is
//    inconvenient, but correct, here.  Ask Amit is you want to know
//    the type safety details.
// ----------------------------------------------------------------------
void StripWhiteSpace(const char** str, size_t* len);

//------------------------------------------------------------------------
// StripTrailingWhitespace()
//   Removes whitespace at the end of the string *s.
//------------------------------------------------------------------------
void StripTrailingWhitespace(std::string* s);

//------------------------------------------------------------------------
// StripTrailingNewline(string*)
//   Strips the very last trailing newline or CR+newline from its
//   input, if one exists.  Useful for dealing with MapReduce's text
//   input mode, which appends '\n' to each map input.  Returns true
//   if a newline was stripped.
//------------------------------------------------------------------------
bool StripTrailingNewline(std::string* s);

inline void StripWhiteSpace(char** str, size_t* len) {
  // The "real" type for StripWhiteSpace is ForAll char types C, take
  // (C, int) as input and return (C, int) as output.  We're using the
  // cast here to assert that we can take a char*, even though the
  // function thinks it's assigning to const char*.
  StripWhiteSpace(const_cast<const char**>(str), len);
}

inline void StripWhiteSpace(GStringPiece* str) {
  const char* data = str->data();
  size_t len = str->size();
  StripWhiteSpace(&data, &len);
  str->set(data, len);
}

void StripWhiteSpace(std::string* str);

namespace strings {

template <typename Collection>
inline void StripWhiteSpaceInCollection(Collection* collection) {
  for (typename Collection::iterator it = collection->begin();
       it != collection->end(); ++it)
    StripWhiteSpace(&(*it));
}

}  // namespace strings

// ----------------------------------------------------------------------
// StripLeadingWhiteSpace
//    "Removes" whitespace from beginning of string. Returns ptr to first
//    non-whitespace character if one is present, NULL otherwise. Assumes
//    "line" is null-terminated.
// ----------------------------------------------------------------------

inline const char* StripLeadingWhiteSpace(const char* line) {
  // skip leading whitespace
  while (ascii_isspace(*line))
    ++line;

  if ('\0' == *line)  // end of line, no non-whitespace
    return NULL;

  return line;
}

// StripLeadingWhiteSpace for non-const strings.
inline char* StripLeadingWhiteSpace(char* line) {
  return const_cast<char*>(
      StripLeadingWhiteSpace(const_cast<const char*>(line)));
}

void StripLeadingWhiteSpace(std::string* str);

// Remove leading, trailing, and duplicate internal whitespace.
void RemoveExtraWhitespace(std::string* s);


// ----------------------------------------------------------------------
// SkipLeadingWhiteSpace
//    Returns str advanced past white space characters, if any.
//    Never returns NULL.  "str" must be terminated by a null character.
// ----------------------------------------------------------------------
inline const char* SkipLeadingWhiteSpace(const char* str) {
  while (ascii_isspace(*str))
    ++str;
  return str;
}

inline char* SkipLeadingWhiteSpace(char* str) {
  while (ascii_isspace(*str))
    ++str;
  return str;
}

// ----------------------------------------------------------------------
// StripCurlyBraces
//    Strips everything enclosed in pairs of curly braces and the curly
//    braces. Doesn't touch open braces. It doesn't handle nested curly
//    braces. This is used for removing things like {:stopword} from
//    queries.
// StripBrackets does the same, but allows the caller to specify different
//    left and right bracket characters, such as '(' and ')'.
// ----------------------------------------------------------------------

void StripCurlyBraces(std::string* s);
void StripBrackets(char left, char right, std::string* s);


// ----------------------------------------------------------------------
// StripMarkupTags
//    Strips everything enclosed in pairs of angle brackets and the angle
//    brackets.
//    This is used for stripping strings of markup; e.g. going from
//    "the quick <b>brown</b> fox" to "the quick brown fox."
//    If you want to skip entire sections of markup (e.g. the word "brown"
//    too in that example), see webutil/pageutil/pageutil.h .
//    This function was designed for stripping the bold tags (inserted by the
//    docservers) from the titles of news stories being returned by RSS.
//    This implementation DOES NOT cover all cases in html documents
//    like tags that contain quoted angle-brackets, or HTML comment.
//    For example <IMG SRC = "foo.gif" ALT = "A > B">
//    or <!-- <A comment> -->
//    See "perldoc -q html"
// ----------------------------------------------------------------------

void StripMarkupTags(std::string* s);
std::string OutputWithMarkupTagsStripped(const std::string& s);

// ----------------------------------------------------------------------
// TrimStringLeft
//    Removes any occurrences of the characters in 'remove' from the start
//    of the string.  Returns the number of chars trimmed.
// ----------------------------------------------------------------------
size_t TrimStringLeft(std::string* s, const GStringPiece& remove);

// ----------------------------------------------------------------------
// TrimStringRight
//    Removes any occurrences of the characters in 'remove' from the end
//    of the string.  Returns the number of chars trimmed.
// ----------------------------------------------------------------------
size_t TrimStringRight(std::string* s, const GStringPiece& remove);

// ----------------------------------------------------------------------
// TrimString
//    Removes any occurrences of the characters in 'remove' from either
//    end of the string.
// ----------------------------------------------------------------------
inline size_t TrimString(std::string* s, const GStringPiece& remove) {
  size_t right_trim = TrimStringRight(s, remove);
  return right_trim + TrimStringLeft(s, remove);
}

// ----------------------------------------------------------------------
// TrimRunsInString
//    Removes leading and trailing runs, and collapses middle
//    runs of a set of characters into a single character (the
//    first one specified in 'remove').  Useful for collapsing
//    runs of repeated delimiters, whitespace, etc.  E.g.,
//    TrimRunsInString(&s, " :,()") removes leading and trailing
//    delimiter chars and collapses and converts internal runs
//    of delimiters to single ' ' characters, so, for example,
//    "  a:(b):c  " -> "a b c"
//    "first,last::(area)phone, ::zip" -> "first last area phone zip"
// ----------------------------------------------------------------------
void TrimRunsInString(std::string* s, GStringPiece remove);

// ----------------------------------------------------------------------
// RemoveNullsInString
//    Removes any internal \0 characters from the string.
// ----------------------------------------------------------------------
void RemoveNullsInString(std::string* s);

// ----------------------------------------------------------------------
// strrm()
// memrm()
//    Remove all occurrences of a given character from a string.
//    Returns the new length.
// ----------------------------------------------------------------------

size_t strrm(char* str, char c);
size_t memrm(char* str, size_t strlen, char c);

// ----------------------------------------------------------------------
// strrmm()
//    Remove all occurrences of a given set of characters from a string.
//    Returns the new length.
// ----------------------------------------------------------------------
size_t strrmm(char* str, const char* chars);
size_t strrmm(std::string* str, const std::string& chars);

// Copyright 2011 Google Inc. All Rights Reserved.
// based on contributions of various authors in strings/strutil_unittest.cc
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

#include "yb/gutil/strings/strip.h"

#include <assert.h>
#include <string.h>

#include <algorithm>

using std::string;

string StripPrefixString(GStringPiece str, const GStringPiece& prefix) {
  if (str.starts_with(prefix))
    str.remove_prefix(prefix.length());
  return str.as_string();
}

bool TryStripPrefixString(GStringPiece str, const GStringPiece& prefix,
                                 string* result) {
  const bool has_prefix = str.starts_with(prefix);
  if (has_prefix)
    str.remove_prefix(prefix.length());
  str.as_string().swap(*result);
  return has_prefix;
}

string StripSuffixString(GStringPiece str, const GStringPiece& suffix) {
  if (str.ends_with(suffix))
    str.remove_suffix(suffix.length());
  return str.as_string();
}

bool TryStripSuffixString(GStringPiece str, const GStringPiece& suffix,
                                 string* result) {
  const bool has_suffix = str.ends_with(suffix);
  if (has_suffix)
    str.remove_suffix(suffix.length());
  str.as_string().swap(*result);
  return has_suffix;
}

// ----------------------------------------------------------------------
// StripString
//    Replaces any occurrence of the character 'remove' (or the characters
//    in 'remove') with the character 'replacewith'.
// ----------------------------------------------------------------------
void StripString(char* str, GStringPiece remove, char replacewith) {
  for (; *str != '\0'; ++str) {
    if (remove.find(*str) != GStringPiece::npos) {
      *str = replacewith;
    }
  }
}

void StripString(char* str, int len, GStringPiece remove, char replacewith) {
  char* end = str + len;
  for (; str < end; ++str) {
    if (remove.find(*str) != GStringPiece::npos) {
      *str = replacewith;
    }
  }
}

void StripString(string* s, GStringPiece remove, char replacewith) {
  for (char& c : *s) {
    if (remove.find(c) != GStringPiece::npos) {
      c = replacewith;
    }
  }
}

// ----------------------------------------------------------------------
// StripWhiteSpace
// ----------------------------------------------------------------------
void StripWhiteSpace(const char** str, size_t* len) {
  // strip off trailing whitespace
  while ((*len) > 0 && ascii_isspace((*str)[(*len)-1])) {
    (*len)--;
  }

  // strip off leading whitespace
  while ((*len) > 0 && ascii_isspace((*str)[0])) {
    (*len)--;
    (*str)++;
  }
}

bool StripTrailingNewline(string* s) {
  if (!s->empty() && (*s)[s->size() - 1] == '\n') {
    if (s->size() > 1 && (*s)[s->size() - 2] == '\r')
      s->resize(s->size() - 2);
    else
      s->resize(s->size() - 1);
    return true;
  }
  return false;
}

void StripWhiteSpace(string* str) {
  size_t str_length = str->length();

  // Strip off leading whitespace.
  size_t first = 0;
  while (first < str_length && ascii_isspace((*str)[first])) {
    ++first;
  }
  // If entire string is white space.
  if (first == str_length) {
    str->clear();
    return;
  }
  if (first > 0) {
    str->erase(0, first);
    str_length -= first;
  }

  // Strip off trailing whitespace.
  size_t last = str_length - 1;
  while (last >= 0 && ascii_isspace((*str)[last])) {
    --last;
  }
  if (last != (str_length - 1) && last >= 0) {
    str->erase(last + 1, string::npos);
  }
}

// ----------------------------------------------------------------------
// Misc. stripping routines
// ----------------------------------------------------------------------
void StripCurlyBraces(string* s) {
  return StripBrackets('{', '}', s);
}

void StripBrackets(char left, char right, string* s) {
  string::iterator opencurly = find(s->begin(), s->end(), left);
  while (opencurly != s->end()) {
    string::iterator closecurly = find(opencurly, s->end(), right);
    if (closecurly == s->end())
      return;
    opencurly = s->erase(opencurly, closecurly + 1);
    opencurly = find(opencurly, s->end(), left);
  }
}

void StripMarkupTags(string* s) {
  string::iterator openbracket = find(s->begin(), s->end(), '<');
  while (openbracket != s->end()) {
    string::iterator closebracket = find(openbracket, s->end(), '>');
    if (closebracket == s->end()) {
      s->erase(openbracket, closebracket);
      return;
    }

    openbracket = s->erase(openbracket, closebracket + 1);
    openbracket = find(openbracket, s->end(), '<');
  }
}

string OutputWithMarkupTagsStripped(const string& s) {
  string result(s);
  StripMarkupTags(&result);
  return result;
}


size_t TrimStringLeft(string* s, const GStringPiece& remove) {
  size_t i = 0;
  while (i < s->size() && memchr(remove.data(), (*s)[i], remove.size())) {
    ++i;
  }
  if (i > 0) s->erase(0, i);
  return i;
}

size_t TrimStringRight(string* s, const GStringPiece& remove) {
  size_t i = s->size(), trimmed = 0;
  while (i > 0 && memchr(remove.data(), (*s)[i-1], remove.size())) {
    --i;
  }
  if (i < s->size()) {
    trimmed = s->size() - i;
    s->erase(i);
  }
  return trimmed;
}

// ----------------------------------------------------------------------
// Various removal routines
// ----------------------------------------------------------------------
size_t strrm(char* str, char c) {
  char *src, *dest;
  for (src = dest = str; *src != '\0'; ++src)
    if (*src != c) *(dest++) = *src;
  *dest = '\0';
  return dest - str;
}

size_t memrm(char* str, size_t strlen, char c) {
  char *src, *dest;
  for (src = dest = str; strlen > 0; ++src) {
    --strlen;
    if (*src != c) *(dest++) = *src;
  }
  return dest - str;
}

size_t strrmm(char* str, const char* chars) {
  char *src, *dest;
  for (src = dest = str; *src != '\0'; ++src) {
    bool skip = false;
    for (const char* c = chars; *c != '\0'; c++) {
      if (*src == *c) {
        skip = true;
        break;
      }
    }
    if (!skip) *(dest++) = *src;
  }
  *dest = '\0';
  return dest - str;
}

size_t strrmm(string* str, const string& chars) {
  size_t str_len = str->length();
  size_t in_index = str->find_first_of(chars);
  if (in_index == string::npos)
    return str_len;

  size_t out_index = in_index++;

  while (in_index < str_len) {
    char c = (*str)[in_index++];
    if (chars.find(c) == string::npos)
      (*str)[out_index++] = c;
  }

  str->resize(out_index);
  return out_index;
}

// ----------------------------------------------------------------------
// StripDupCharacters
//    Replaces any repeated occurrence of the character 'repeat_char'
//    with single occurrence.  e.g.,
//       StripDupCharacters("a//b/c//d", '/', 0) => "a/b/c/d"
//    Return the number of characters removed
// ----------------------------------------------------------------------
size_t StripDupCharacters(string* s, char dup_char, int64 start_pos) {
  if (start_pos < 0)
    start_pos = 0;

  // remove dups by compaction in-place
  size_t input_pos = start_pos;   // current reader position
  size_t output_pos = start_pos;  // current writer position
  const size_t input_end = s->size();
  while (input_pos < input_end) {
    // keep current character
    const char curr_char = (*s)[input_pos];
    if (output_pos != input_pos)  // must copy
      (*s)[output_pos] = curr_char;
    ++input_pos;
    ++output_pos;

    if (curr_char == dup_char) {  // skip subsequent dups
      while ((input_pos < input_end) && ((*s)[input_pos] == dup_char))
        ++input_pos;
    }
  }
  const size_t num_deleted = input_pos - output_pos;
  s->resize(s->size() - num_deleted);
  return num_deleted;
}

// ----------------------------------------------------------------------
// RemoveExtraWhitespace()
//   Remove leading, trailing, and duplicate internal whitespace.
// ----------------------------------------------------------------------
void RemoveExtraWhitespace(string* s) {
  assert(s != nullptr);
  // Empty strings clearly have no whitespace, and this code assumes that
  // string length is greater than 0
  if (s->empty())
    return;

  size_t input_pos = 0;   // current reader position
  size_t output_pos = 0;  // current writer position
  const size_t input_end = s->size();
  // Strip off leading space
  while (input_pos < input_end && ascii_isspace((*s)[input_pos])) input_pos++;

  while (input_pos < input_end - 1) {
    char c = (*s)[input_pos];
    char next = (*s)[input_pos + 1];
    // Copy each non-whitespace character to the right position.
    // For a block of whitespace, print the last one.
    if (!ascii_isspace(c) || !ascii_isspace(next)) {
      if (output_pos != input_pos) {  // only copy if needed
        (*s)[output_pos] = c;
      }
      output_pos++;
    }
    input_pos++;
  }
  // Pick up the last character if needed.
  char c = (*s)[input_end - 1];
  if (!ascii_isspace(c)) (*s)[output_pos++] = c;

  s->resize(output_pos);
}

//------------------------------------------------------------------------
// See comment in header file for a complete description.
//------------------------------------------------------------------------
void StripLeadingWhiteSpace(string* str) {
  char const* const leading = StripLeadingWhiteSpace(
      const_cast<char*>(str->c_str()));
  if (leading != nullptr) {
    string const tmp(leading);
    str->assign(tmp);
  } else {
    str->assign("");
  }
}

void StripTrailingWhitespace(string* const s) {
  string::size_type i;
  for (i = s->size(); i > 0 && ascii_isspace((*s)[i - 1]); --i) {
  }

  s->resize(i);
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
void TrimRunsInString(string* s, GStringPiece remove) {
  string::iterator dest = s->begin();
  string::iterator src_end = s->end();
  for (string::iterator src = s->begin(); src != src_end; ) {
    if (remove.find(*src) == GStringPiece::npos) {
      *(dest++) = *(src++);
    } else {
      // Skip to the end of this run of chars that are in 'remove'.
      for (++src; src != src_end; ++src) {
        if (remove.find(*src) == GStringPiece::npos) {
          if (dest != s->begin()) {
            // This is an internal run; collapse it.
            *(dest++) = remove[0];
          }
          *(dest++) = *(src++);
          break;
        }
      }
    }
  }
  s->erase(dest, src_end);
}

// ----------------------------------------------------------------------
// RemoveNullsInString
//    Removes any internal \0 characters from the string.
// ----------------------------------------------------------------------
void RemoveNullsInString(string* s) {
  s->erase(remove(s->begin(), s->end(), '\0'), s->end());
}

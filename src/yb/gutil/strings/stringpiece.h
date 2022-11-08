// Copyright 2001, Google Inc.  All rights reserved.
// Maintainer: mec@google.com (Michael Chastain)
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
// A GStringPiece points to part or all of a string, Cord, double-quoted string
// literal, or other string-like object.  A GStringPiece does *not* own the
// string to which it points.  A GStringPiece is not null-terminated.
//
// You can use GStringPiece as a function or method parameter.  A GStringPiece
// parameter can receive a double-quoted string literal argument, a "const
// char*" argument, a string argument, or a GStringPiece argument with no data
// copying.  Systematic use of GStringPiece for arguments reduces data
// copies and strlen() calls.
//
// You may pass a GStringPiece argument by value or const reference.
// Passing by value generates slightly smaller code.
//   void MyFunction(const GStringPiece& arg);
//   // Slightly better, but same lifetime requirements as const-ref parameter:
//   void MyFunction(GStringPiece arg);
//
// GStringPiece is also suitable for local variables if you know that
// the lifetime of the underlying object is longer than the lifetime
// of your GStringPiece variable.
//
// Beware of binding a GStringPiece to a temporary:
//   GStringPiece sp = obj.MethodReturningString();  // BAD: lifetime problem
//
// This code is okay:
//   string str = obj.MethodReturningString();  // str owns its contents
//   GStringPiece sp(str);  // GOOD, although you may not need sp at all
//
// GStringPiece is sometimes a poor choice for a return value and usually a poor
// choice for a data member.  If you do use a GStringPiece this way, it is your
// responsibility to ensure that the object pointed to by the GStringPiece
// outlives the GStringPiece.
//
// A GStringPiece may represent just part of a string; thus the name "Piece".
// For example, when splitting a string, vector<GStringPiece> is a natural data
// type for the output.  For another example, a Cord is a non-contiguous,
// potentially very long string-like object.  The Cord class has an interface
// that iteratively provides GStringPiece objects that point to the
// successive pieces of a Cord object.
//
// A GStringPiece is not null-terminated.  If you write code that scans a
// GStringPiece, you must check its length before reading any characters.
// Common idioms that work on null-terminated strings do not work on
// GStringPiece objects.
//
// There are several ways to create a null GStringPiece:
//   GStringPiece()
//   GStringPiece(NULL)
//   GStringPiece(NULL, 0)
// For all of the above, sp.data() == NULL, sp.length() == 0,
// and sp.empty() == true.  Also, if you create a GStringPiece with
// a non-NULL pointer then sp.data() != non-NULL.  Once created,
// sp.data() will stay either NULL or not-NULL, except if you call
// sp.clear() or sp.set().
//
// Thus, you can use GStringPiece(NULL) to signal an out-of-band value
// that is different from other GStringPiece values.  This is similar
// to the way that const char* p1 = NULL; is different from
// const char* p2 = "";.
//
// There are many ways to create an empty GStringPiece:
//   GStringPiece()
//   GStringPiece(NULL)
//   GStringPiece(NULL, 0)
//   GStringPiece("")
//   GStringPiece("", 0)
//   GStringPiece("abcdef", 0)
//   GStringPiece("abcdef"+6, 0)
// For all of the above, sp.length() will be 0 and sp.empty() will be true.
// For some empty GStringPiece values, sp.data() will be NULL.
// For some empty GStringPiece values, sp.data() will not be NULL.
//
// Be careful not to confuse: null GStringPiece and empty GStringPiece.
// The set of empty GStringPieces properly includes the set of null GStringPieces.
// That is, every null GStringPiece is an empty GStringPiece,
// but some non-null GStringPieces are empty Stringpieces too.
//
// All empty GStringPiece values compare equal to each other.
// Even a null GStringPieces compares equal to a non-null empty GStringPiece:
//  GStringPiece() == GStringPiece("", 0)
//  GStringPiece(NULL) == GStringPiece("abc", 0)
//  GStringPiece(NULL, 0) == GStringPiece("abcdef"+6, 0)
//
// Look carefully at this example:
//   GStringPiece("") == NULL
// True or false?  TRUE, because GStringPiece::operator== converts
// the right-hand side from NULL to GStringPiece(NULL),
// and then compares two zero-length spans of characters.
// However, we are working to make this example produce a compile error.
//
// Suppose you want to write:
//   bool TestWhat?(GStringPiece sp) { return sp == NULL; }  // BAD
// Do not do that.  Write one of these instead:
//   bool TestNull(GStringPiece sp) { return sp.data() == NULL; }
//   bool TestEmpty(GStringPiece sp) { return sp.empty(); }
// The intent of TestWhat? is unclear.  Did you mean TestNull or TestEmpty?
// Right now, TestWhat? behaves likes TestEmpty.
// We are working to make TestWhat? produce a compile error.
// TestNull is good to test for an out-of-band signal.
// TestEmpty is good to test for an empty GStringPiece.
//
// Caveats (again):
// (1) The lifetime of the pointed-to string (or piece of a string)
//     must be longer than the lifetime of the GStringPiece.
// (2) There may or may not be a '\0' character after the end of
//     GStringPiece data.
// (3) A null GStringPiece is empty.
//     An empty GStringPiece may or may not be a null GStringPiece.

#pragma once

#include <assert.h>

#include <iosfwd>
#include <limits>
#include <string>

#include "yb/gutil/strings/fastmem.h"

class GStringPiece {
 private:
  const char*   ptr_;
  size_t        length_;

 public:
  // standard STL container boilerplate
  typedef char value_type;
  typedef const char* pointer;
  typedef const char& reference;
  typedef const char& const_reference;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  static const size_type npos;
  typedef const char* const_iterator;
  typedef const char* iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;

  // We provide non-explicit singleton constructors so users can pass
  // in a "const char*" or a "string" wherever a "GStringPiece" is
  // expected.
  //
  // Style guide exception granted:
  // http://goto/style-guide-exception-20978288
  GStringPiece() : ptr_(NULL), length_(0) {}
  GStringPiece(const char* str)  // NOLINT(runtime/explicit)
      : ptr_(str), length_(0) {
    if (str != NULL) {
      size_t length = strlen(str);
      assert(length <= static_cast<size_t>(std::numeric_limits<int>::max()));
      length_ = static_cast<int>(length);
    }
  }
  GStringPiece(const std::string& str)  // NOLINT(runtime/explicit)
      : ptr_(str.data()), length_(str.length()) {
  }

  GStringPiece(const char* offset, size_type len) : ptr_(offset), length_(len) {
  }

  // Substring of another GStringPiece.
  // pos must be non-negative and <= x.length().
  GStringPiece(GStringPiece x, size_type pos);
  // Substring of another GStringPiece.
  // pos must be non-negative and <= x.length().
  // len must be non-negative and will be pinned to at most x.length() - pos.
  GStringPiece(GStringPiece x, size_type pos, size_type len);

  // data() may return a pointer to a buffer with embedded NULs, and the
  // returned buffer may or may not be null terminated.  Therefore it is
  // typically a mistake to pass data() to a routine that expects a NUL
  // terminated string.
  const char* data() const { return ptr_; }
  size_type size() const { return length_; }
  size_type length() const { return length_; }
  bool empty() const { return length_ == 0; }

  void clear() {
    ptr_ = NULL;
    length_ = 0;
  }

  void set(const char* data, size_type len) {
    ptr_ = data;
    length_ = len;
  }

  void set(const char* str) {
    ptr_ = str;
    if (str != NULL)
      length_ = strlen(str);
    else
      length_ = 0;
  }

  void set(const void* data, size_type len) {
    ptr_ = reinterpret_cast<const char*>(data);
    length_ = len;
  }

  char operator[](size_type i) const {
    assert(i < length_);
    return ptr_[i];
  }

  void remove_prefix(size_type n) {
    assert(length_ >= n);
    ptr_ += n;
    length_ -= n;
  }

  void remove_suffix(size_type n) {
    assert(length_ >= n);
    length_ -= n;
  }

  // returns {-1, 0, 1}
  int compare(GStringPiece x) const {
    const size_type min_size = length_ < x.length_ ? length_ : x.length_;
    int r = memcmp(ptr_, x.ptr_, min_size);
    if (r < 0) return -1;
    if (r > 0) return 1;
    if (length_ < x.length_) return -1;
    if (length_ > x.length_) return 1;
    return 0;
  }

  std::string as_string() const {
    return ToString();
  }
  // We also define ToString() here, since many other string-like
  // interfaces name the routine that converts to a C++ string
  // "ToString", and it's confusing to have the method that does that
  // for a GStringPiece be called "as_string()".  We also leave the
  // "as_string()" method defined here for existing code.
  std::string ToString() const {
    if (ptr_ == NULL) return std::string();
    return std::string(data(), size());
  }

  void CopyToString(std::string* target) const;
  void AppendToString(std::string* target) const;

  bool starts_with(GStringPiece x) const {
    return (length_ >= x.length_) && (memcmp(ptr_, x.ptr_, x.length_) == 0);
  }

  bool ends_with(GStringPiece x) const {
    return ((length_ >= x.length_) &&
            (memcmp(ptr_ + (length_-x.length_), x.ptr_, x.length_) == 0));
  }

  iterator begin() const { return ptr_; }
  iterator end() const { return ptr_ + length_; }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(ptr_ + length_);
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(ptr_);
  }

  size_type max_size() const { return length_; }
  size_type capacity() const { return length_; }

  // cpplint.py emits a false positive [build/include_what_you_use]
  size_type copy(char* buf, size_type n, size_type pos = 0) const;  // NOLINT

  bool contains(GStringPiece s) const;

  size_type find(GStringPiece s, size_type pos = 0) const;
  size_type find(char c, size_type pos = 0) const;
  size_type rfind(GStringPiece s, size_type pos = npos) const;
  size_type rfind(char c, size_type pos = npos) const;

  size_type find_first_of(GStringPiece s, size_type pos = 0) const;
  size_type find_first_of(char c, size_type pos = 0) const { return find(c, pos); }
  size_type find_first_not_of(GStringPiece s, size_type pos = 0) const;
  size_type find_first_not_of(char c, size_type pos = 0) const;
  size_type find_last_of(GStringPiece s, size_type pos = npos) const;
  size_type find_last_of(char c, size_type pos = npos) const { return rfind(c, pos); }
  size_type find_last_not_of(GStringPiece s, size_type pos = npos) const;
  size_type find_last_not_of(char c, size_type pos = npos) const;

  GStringPiece substr(size_type pos, size_type n = npos) const;

  size_t hash() const;
};

// This large function is defined inline so that in a fairly common case where
// one of the arguments is a literal, the compiler can elide a lot of the
// following comparisons.
inline bool operator==(GStringPiece x, GStringPiece y) {
  auto len = x.size();
  if (len != y.size()) {
    return false;
  }

  return x.data() == y.data() || len <= 0 || strings::memeq(x.data(), y.data(), len);
}

inline bool operator!=(GStringPiece x, GStringPiece y) {
  return !(x == y);
}

inline bool operator<(GStringPiece x, GStringPiece y) {
  const auto min_size = x.size() < y.size() ? x.size() : y.size();
  const int r = memcmp(x.data(), y.data(), min_size);
  return (r < 0) || (r == 0 && x.size() < y.size());
}

inline bool operator>(GStringPiece x, GStringPiece y) {
  return y < x;
}

inline bool operator<=(GStringPiece x, GStringPiece y) {
  return !(x > y);
}

inline bool operator>=(GStringPiece x, GStringPiece y) {
  return !(x < y);
}
class GStringPiece;
template <class X> struct GoodFastHash;

// ------------------------------------------------------------------
// Functions used to create STL containers that use GStringPiece
//  Remember that a GStringPiece's lifetime had better be less than
//  that of the underlying string or char*.  If it is not, then you
//  cannot safely store a GStringPiece into an STL container
// ------------------------------------------------------------------

// SWIG doesn't know how to parse this stuff properly. Omit it.
#ifndef SWIG

namespace std {
template<> struct hash<GStringPiece> {
  size_t operator()(GStringPiece s) const;
};
}  // namespace std


// An implementation of GoodFastHash for GStringPiece.  See
// GoodFastHash values.
template<> struct GoodFastHash<GStringPiece> {
  size_t operator()(GStringPiece s) const {
    return s.hash();
  }

  // Less than operator, for MSVC.
  bool operator()(const GStringPiece& s1, const GStringPiece& s2) const {
    return s1 < s2;
  }
  static const size_t bucket_size = 4;  // These are required by MSVC
  static const size_t min_buckets = 8;  // 4 and 8 are defaults.
};
#endif

// allow GStringPiece to be logged
extern std::ostream& operator<<(std::ostream& o, GStringPiece piece);

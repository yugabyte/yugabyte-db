//
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
//

#pragma once

#include "yb/util/tostring.h" // for ToString

namespace yb {

namespace internal {

template <class Type, class Enabled = void>
class FormatValue {
 public:
  explicit FormatValue(const Type& input) : value_(ToString(input)) {}

  FormatValue(const FormatValue& rhs) = delete;
  void operator=(const FormatValue& rhs) = delete;

  size_t Add(size_t position) const {
    return position + value_.length();
  }

  char* Add(char* position) const {
    size_t len = value_.length();
    memcpy(position, value_.c_str(), len);
    return position + len;
  }
 private:
  std::string value_;
};

template <class T>
class FormatValue<T,
    std::enable_if_t<std::is_integral<T>::value || std::is_same<void*, T>::value>> {
 public:
  template<class U>
  explicit FormatValue(const U& t) {
    auto end = IntToBuffer(t, buffer_);
    len_ = end - buffer_;
  }

  explicit FormatValue(void* const& ptr) {
    buffer_[0] = '0';
    buffer_[1] = 'x';
    FastHex64ToBuffer(reinterpret_cast<size_t>(ptr), buffer_ + 2);
    len_ = 2 + sizeof(ptr) * 2;
  }

  FormatValue(const FormatValue& rhs) = delete;
  void operator=(const FormatValue& rhs) = delete;

  size_t Add(size_t position) const {
    return position + len_;
  }

  char* Add(char* position) const {
    memcpy(position, buffer_, len_);
    return position + len_;
  }

 private:
  char buffer_[kFastToBufferSize];
  size_t len_;
};

template <class T>
class FormatValue<T, std::enable_if_t<std::is_convertible<T, const char*>::value>> {
 public:
  template<class U>
  explicit FormatValue(const U& value) : value_(value), len_(strlen(value_)) {}

  template<class U, size_t N>
  explicit FormatValue(U(&value)[N]) : value_(value), len_(strnlen(value_, N)) {}

  FormatValue(const FormatValue& rhs) = delete;
  void operator=(const FormatValue& rhs) = delete;

  size_t Add(size_t position) const {
    return position + len_;
  }

  char* Add(char* position) const {
    memcpy(position, value_, len_);
    return position + len_;
  }

 private:
  const char *value_;
  const size_t len_;
};

template <class T>
using FormatValueType =
    FormatValue<typename std::remove_cv_t<typename std::remove_reference_t<T>>>;

template<class Current, class... Args>
class FormatTuple;

template<class Current>
class FormatTuple<Current> {
 public:
  typedef FormatValueType<Current> Value;

  explicit FormatTuple(const Current& current)
      : value_(current) {}

  template<class Out>
  Out Add(size_t index, Out out) const {
    if (index == 0) {
      return value_.Add(out);
    }
    return out;
  }
 private:
  Value value_;
};

template<class Current, class... Args>
class FormatTuple {
 public:
  typedef FormatValueType<Current> Value;
  typedef FormatTuple<Args...> Tail;

  explicit FormatTuple(const Current& current, const Args&... args)
      : value_(current), tail_(args...) {}

  template<class Out>
  Out Add(size_t index, Out out) const {
    if (index == 0) {
      return value_.Add(out);
    }
    return tail_.Add(index - 1, out);
  }
 private:
  Value value_;
  Tail tail_;
};

inline size_t FormatCopy(const char* begin, const char* end, size_t position) {
  return position + end - begin;
}

inline size_t FormatPut(char ch, size_t position) {
  return position + 1;
}

inline char* FormatCopy(const char* begin, const char* end, char* position) {
  size_t len = end - begin;
  memcpy(position, begin, len);
  return position + len;
}

inline char* FormatPut(char ch, char* position) {
  *position = ch;
  return position + 1;
}

template<class Tuple, class Out>
Out DoFormat(const char* format, const Tuple& tuple, Out out) {
  Out position = out;
  const char* previous = format;
  for (;;) {
    const char* p = strchr(previous, '$');
    if (!p) {
      break;
    }
    position = FormatCopy(previous, p, position);
    char ch = p[1];
    if (ch >= '0' && ch <= '9') {
      size_t index = 0;
      for (;;) {
        index += ch - '0';
        ++p;
        ch = p[1];
        if (ch < '0' || ch > '9') {
          break;
        }
        index *= 10;
      }
      position = tuple.Add(index, position);
    } else if (ch == '$') {
      position = FormatPut('$', position);
      ++p;  // Skip next char.
    }
    previous = p + 1;
  }
  if (*previous) {
    position = FormatCopy(previous, previous + strlen(previous), position);
  }
  return position;
}

template<class... Args>
std::string FormatImpl(const char* format, const Args&... args) {
  FormatTuple<Args...> tuple(args...);
  size_t size = DoFormat(format, tuple, static_cast<size_t>(0));
  std::string result(size, '\0');
  DoFormat(format, tuple, &result[0]);
  return result;
}

} // namespace internal

template<class... Args>
std::string Format(const std::string& format, const Args&... args) {
  return internal::FormatImpl(format.c_str(), args...);
}

template<class... Args>
std::string Format(const char* format, const Args&... args) {
  return internal::FormatImpl(format, args...);
}

inline std::string Format(std::string format) {
  return format;
}

inline std::string Format(const char* format) {
  return format;
}

} // namespace yb

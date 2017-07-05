//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_UTIL_FORMAT_H
#define YB_UTIL_FORMAT_H

#include "yb/util/tostring.h"

namespace yb {

namespace internal {

template <class Type, bool integral>
class FormatValue {
 public:
  explicit FormatValue(const Type& input) : value_(util::ToString(input)) {}

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

class BufferedValue {
 public:
  BufferedValue() {}

  BufferedValue(const BufferedValue& rhs) = delete;
  void operator=(const BufferedValue& rhs) = delete;

  size_t Add(size_t position) const {
    return position + len_;
  }

  char* Add(char* position) const {
    memcpy(position, buffer_, len_);
    return position + len_;
  }

 protected:
  char buffer_[kFastToBufferSize];
  size_t len_;
};

template <class T>
class FormatValue<T, true> : public BufferedValue {
 public:
  explicit FormatValue(const T& t) {
    auto end = util::IntToBuffer(t, buffer_);
    len_ = end - buffer_;
  }
};

template <>
class FormatValue<void*, false> : public BufferedValue {
 public:
  explicit FormatValue(const void* ptr) {
    buffer_[0] = '0';
    buffer_[1] = 'x';
    FastHex64ToBuffer(reinterpret_cast<size_t>(ptr), buffer_ + 2);
    len_ = 2 + sizeof(ptr) * 2;
  }
};

class FormatCStrValue {
 public:
  FormatCStrValue(const char* value, size_t len) : value_(value), len_(len) {}

  FormatCStrValue(const FormatCStrValue& rhs) = delete;
  void operator=(const FormatCStrValue& rhs) = delete;

  size_t Add(size_t position) const {
    return position + len_;
  }

  char* Add(char* position) const {
    memcpy(position, value_, len_);
    return position + len_;
  }

 private:
  const char *value_;
  size_t len_;
};

template <size_t N>
class FormatValue<char[N], false> {
 public:
  explicit FormatValue(const char* value) : value_(value) {}

  FormatValue(const FormatValue& rhs) = delete;
  void operator=(const FormatValue& rhs) = delete;

  size_t Add(size_t position) const {
    return position + N - 1;
  }

  char* Add(char* position) const {
    memcpy(position, value_, N - 1);
    return position + N - 1;
  }

 private:
  const char *value_;
};

template <>
class FormatValue<const char*, false> : public FormatCStrValue {
 public:
  explicit FormatValue(const char* value) : FormatCStrValue(value, strlen(value)) {}
};

template <>
class FormatValue<char*, false> : public FormatCStrValue {
 public:
  explicit FormatValue(const char* value) : FormatCStrValue(value, strlen(value)) {}
};

template <>
class FormatValue<std::string, false> : public FormatCStrValue {
 public:
  explicit FormatValue(const std::string& value) : FormatCStrValue(value.c_str(), value.length()) {}
};

template <class T>
struct MakeFormatValue {
  typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type CleanedT;
  typedef FormatValue<CleanedT, std::is_integral<CleanedT>::value> type;
};

template<class Current, class... Args>
class FormatTuple;

template<class Current>
class FormatTuple<Current> {
 public:
  typedef typename MakeFormatValue<Current>::type Value;

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
  typedef typename MakeFormatValue<Current>::type Value;
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
      position = tuple.Add(ch - '0', position);
      ++p;
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

} // namespace yb

#endif // YB_UTIL_FORMAT_H

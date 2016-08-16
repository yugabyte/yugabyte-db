// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_CAST_H_
#define YB_UTIL_CAST_H_

namespace yb {
namespace util {

char* to_char_ptr(uint8_t* uptr) {
  return reinterpret_cast<char *>(uptr);
}

const char* to_char_ptr(const uint8_t* uptr) {
  return reinterpret_cast<const char *>(uptr);
}

uint8_t* to_uchar_ptr(char *ptr) {
  return reinterpret_cast<uint8_t *>(ptr);
}

const uint8_t* to_uchar_ptr(const char *ptr) {
  return reinterpret_cast<const uint8_t *>(ptr);
}
}  // namespace util
}  // namespace yb
#endif  // YB_UTIL_CAST_H_

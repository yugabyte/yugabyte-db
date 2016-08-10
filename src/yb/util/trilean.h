// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_TRILEAN_H_
#define YB_UTIL_TRILEAN_H_

#include <string>
#include <ostream>

namespace yb {

// A "three-value boolean". We need this for some sanity checks.
// https://en.wikipedia.org/wiki/Three-valued_logic
enum class Trilean : uint8_t {
  kFalse = 0,
  kTrue = 1,
  kUnknown = 2
};

std::string TrileanToStr(Trilean value);

inline constexpr Trilean ToTrilean(bool b) noexcept {
  return b ? Trilean::kTrue : Trilean::kFalse;
}

inline std::ostream& operator << (std::ostream& out, Trilean trilean) {
  return out << TrileanToStr(trilean);
}

}

#endif

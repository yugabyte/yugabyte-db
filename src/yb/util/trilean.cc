// Copyright (c) YugaByte, Inc.

#include "yb/util/trilean.h"
#include "yb/gutil/strings/substitute.h"

using strings::Substitute;

namespace yb {

std::string TrileanToStr(Trilean value) {
  switch (value) {
    case Trilean::kFalse:
      return "false";
    case Trilean::kTrue:
      return "true";
    case Trilean::kUnknown:
      return "unknown";
  }
  return Substitute("Trilean($0)");
}

}


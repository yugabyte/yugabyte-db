// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/stol_utils.h"

#include <cstring>

#include "yb/util/string_util.h"

using namespace std::placeholders;

namespace yb {

namespace {

Status CreateInvalid(Slice input, int err = 0, const char* type_name = "") {
  auto message = Format("$0 is not a valid number", input.ToDebugString());
  if (*type_name) {
    message += Format(" for type $0", type_name);
  }
  if (err != 0) {
    message += ": ";
    message += std::strerror(err);
  }
  return STATUS(InvalidArgument, message);
}

Status CheckNotSpace(Slice slice) {
  if (slice.empty() || isspace(*slice.cdata())) {
    // disable skip of spaces.
    return CreateInvalid(slice);
  }
  return Status::OK();
}

template <typename T, typename StrToT>
Result<T> CheckedSton(Slice slice, StrToT str_to_t) {
  RETURN_NOT_OK(CheckNotSpace(slice));
  if constexpr (std::is_floating_point_v<T>) {
    auto maybe_special_value = TryParsingNonNumberValue<T>(slice);
    if (maybe_special_value) {
      return *maybe_special_value;
    }
  }

  char* str_end;
  errno = 0;
  T result = str_to_t(slice.cdata(), &str_end);
  // Check errno.
  if (errno != 0) {
    return CreateInvalid(slice, errno, typeid(T).name());
  }
  if constexpr (std::is_unsigned<T>::value) {
    // Do not allow any minus signs for unsigned types.
    for (auto* p = slice.cdata(); p != str_end; ++p) {
      if (*p == '-') {
        return CreateInvalid(slice, /* err= */ 0, typeid(T).name());
      }
    }
  }
  // Check that entire string was processed.
  if (str_end != slice.cend()) {
    return CreateInvalid(slice, /* err= */ 0, typeid(T).name());
  }

  return result;
}

} // Anonymous namespace

Result<int64_t> CheckedStoll(Slice slice) {
  return CheckedSton<int64_t>(slice, std::bind(&std::strtoll, _1, _2, 10));
}

Result<uint64_t> CheckedStoull(Slice slice) {
  return CheckedSton<uint64_t>(slice, std::bind(&std::strtoull, _1, _2, 10));
}

Result<int64_t> DoCheckedStol(Slice value, int64_t*) { return CheckedStoll(value); }
Result<uint64_t> DoCheckedStol(Slice value, uint64_t*) { return CheckedStoull(value); }

Result<long double> CheckedStold(Slice slice) {
  return CheckedSton<long double>(slice, std::strtold);
}

} // namespace yb

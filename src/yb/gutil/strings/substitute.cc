// Copyright 2008 Google Inc.  All rights reserved.
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

#include "yb/gutil/strings/substitute.h"

#include "yb/util/logging.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/ascii_ctype.h"
#include "yb/gutil/strings/escaping.h"

using std::string;

namespace strings {

using internal::SubstituteArg;

const SubstituteArg SubstituteArg::NoArg;

// Returns the number of args in arg_array which were passed explicitly
// to Substitute().
static int CountSubstituteArgs(const SubstituteArg* const* args_array) {
  int count = 0;
  while (args_array[count] != &SubstituteArg::NoArg) {
    ++count;
  }
  return count;
}

namespace internal {
int SubstitutedSize(GStringPiece format,
                    const SubstituteArg* const* args_array) {
  int size = 0;
  for (size_t i = 0; i < format.size(); i++) {
    if (format[i] == '$') {
      if (i+1 >= format.size()) {
        LOG(DFATAL) << "Invalid strings::Substitute() format string: \""
                    << CEscape(format) << "\".";
        return 0;
      } else if (ascii_isdigit(format[i+1])) {
        int index = format[i+1] - '0';
        if (args_array[index]->size() == -1) {
          LOG(DFATAL)
            << "strings::Substitute format string invalid: asked for \"$"
            << index << "\", but only " << CountSubstituteArgs(args_array)
            << " args were given.  Full format string was: \""
            << CEscape(format) << "\".";
          return 0;
        }
        size += args_array[index]->size();
        ++i;  // Skip next char.
      } else if (format[i+1] == '$') {
        ++size;
        ++i;  // Skip next char.
      } else {
        LOG(DFATAL) << "Invalid strings::Substitute() format string: \""
                    << CEscape(format) << "\".";
        return 0;
      }
    } else {
      ++size;
    }
  }
  return size;
}

char* SubstituteToBuffer(GStringPiece format,
                         const SubstituteArg* const* args_array,
                         char* target) {
  CHECK_NOTNULL(target);
  for (size_t i = 0; i < format.size(); i++) {
    if (format[i] == '$') {
      if (ascii_isdigit(format[i+1])) {
        const SubstituteArg* src = args_array[format[i+1] - '0'];
        memcpy(target, src->data(), src->size());
        target += src->size();
        ++i;  // Skip next char.
      } else if (format[i+1] == '$') {
        *target++ = '$';
        ++i;  // Skip next char.
      }
    } else {
      *target++ = format[i];
    }
  }
  return target;
}

} // namespace internal

void SubstituteAndAppend(
    string* output, GStringPiece format, const SubstituteArg& arg0, const SubstituteArg& arg1,
    const SubstituteArg& arg2, const SubstituteArg& arg3, const SubstituteArg& arg4,
    const SubstituteArg& arg5, const SubstituteArg& arg6, const SubstituteArg& arg7,
    const SubstituteArg& arg8, const SubstituteArg& arg9, const SubstituteArg& arg10) {
  const SubstituteArg* const args_array[] = {&arg0, &arg1, &arg2, &arg3, &arg4,  &arg5,
                                             &arg6, &arg7, &arg8, &arg9, &arg10, nullptr};

  // Determine total size needed.
  int size = SubstitutedSize(format, args_array);
  if (size == 0) return;

  // Build the string.
  auto original_size = output->size();
  STLStringResizeUninitialized(output, original_size + size);
  char* target = string_as_array(output) + original_size;

  target = SubstituteToBuffer(format, args_array, target);
  DCHECK_EQ(target - output->data(), output->size());
}

SubstituteArg::SubstituteArg(const void* value) {
  COMPILE_ASSERT(sizeof(scratch_) >= sizeof(value) * 2 + 2,
                 fix_sizeof_scratch_);
  if (value == nullptr) {
    text_ = "NULL";
    size_ = strlen(text_);
  } else {
    char* ptr = scratch_ + sizeof(scratch_);
    uintptr_t num = reinterpret_cast<uintptr_t>(value);
    static const char kHexDigits[] = "0123456789abcdef";
    do {
      *--ptr = kHexDigits[num & 0xf];
      num >>= 4;
    } while (num != 0);
    *--ptr = 'x';
    *--ptr = '0';
    text_ = ptr;
    size_ = scratch_ + sizeof(scratch_) - ptr;
  }
}

}  // namespace strings

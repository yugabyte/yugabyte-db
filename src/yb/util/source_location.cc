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

#include "yb/util/source_location.h"

#include <string.h>

namespace yb {

const char* ShortenSourceFilePath(const char* file_path) {
  if (file_path == nullptr) {
    return file_path;
  }

  // Remove the leading "../../../" stuff.
  while (strncmp(file_path, "../", 3) == 0) {
    file_path += 3;
  }

  // This could be called arbitrarily early or late in program execution as part of backtrace,
  // so we're not using any std::string static constants here.
#define YB_HANDLE_SOURCE_SUBPATH(subpath, prefix_len_to_remove) \
    do { \
      const char* const subpath_ptr = strstr(file_path, (subpath)); \
      if (subpath_ptr != nullptr) { \
        return subpath_ptr + (prefix_len_to_remove); \
      } \
    } while (0);

  YB_HANDLE_SOURCE_SUBPATH("/src/yb/", 5);
  YB_HANDLE_SOURCE_SUBPATH("/src/postgres/src/", 5);
  YB_HANDLE_SOURCE_SUBPATH("/src/rocksdb/", 5);
  YB_HANDLE_SOURCE_SUBPATH("/thirdparty/build/", 1);

  // These are Linuxbrew gcc's standard headers. Keep the path starting from "gcc/...".
  YB_HANDLE_SOURCE_SUBPATH("/Cellar/gcc/", 8);

  // TODO: replace postgres_build with just postgres.
  YB_HANDLE_SOURCE_SUBPATH("/postgres_build/src/", 1);

#undef YB_HANDLE_SOURCE_SUBPATH

  return file_path;
}

}  // namespace yb

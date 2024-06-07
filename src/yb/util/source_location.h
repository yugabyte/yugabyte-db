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

#pragma once

#include <string>

namespace yb {

struct SourceLocation {
  const char* file_name;
  int line_number;

  std::string ToString() const;
};

#define SOURCE_LOCATION() SourceLocation {__FILE__, __LINE__}

// Shorten the given source file path by removing prefixes that are unnecessarily long and/or would
// not prevent us from finding the original source file given that we know the toolchain directory,
// the third-party libraries directory, etc.
const char* ShortenSourceFilePath(const char* file_path);

}  // namespace yb

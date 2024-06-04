// Copyright 2008 Google Inc. All Rights Reserved.
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

#include "yb/gutil/strings/charset.h"

#include <string.h>

namespace strings {

CharSet::CharSet() {
  memset(this, 0, sizeof(*this));
}

CharSet::CharSet(const char* characters) {
  memset(this, 0, sizeof(*this));
  for (; *characters != '\0'; ++characters) {
    Add(*characters);
  }
}

CharSet::CharSet(const CharSet& other) {
  memcpy(this, &other, sizeof(*this));
}

}  // namespace strings

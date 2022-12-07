//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Must not be included from any .h files to avoid polluting the namespace
// with macros.

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <string>

#include "yb/util/slice.h"

namespace rocksdb {

// Append a human-readable time in micros.
int AppendHumanMicros(uint64_t micros, char* output, int len,
                      bool fixed_format);

// Append a human-readable size in bytes
int AppendHumanBytes(uint64_t bytes, char* output, int len);

// Append a human-readable printout of "num" to *str
extern void AppendNumberTo(std::string* str, uint64_t num);

// Append a human-readable printout of "b" to *str
extern void AppendBoolTo(std::string* str, bool b);

// Append a human-readable printout of "value" to *str.
// Escapes any non-printable characters found in "value".
extern void AppendEscapedStringTo(std::string* str, const Slice& value);

// Return a string printout of "num"
extern std::string NumberToString(uint64_t num);

// Return a human-readable version of num.
// for num >= 10.000, prints "xxK"
// for num >= 10.000.000, prints "xxM"
// for num >= 10.000.000.000, prints "xxG"
extern std::string NumberToHumanString(int64_t num);

// Return a human-readable version of "value".
// Escapes any non-printable characters found in "value".
extern std::string EscapeString(const Slice& value);

// Parse a human-readable number from "*in" into *value.  On success,
// advances "*in" past the consumed number and sets "*val" to the
// numeric value.  Otherwise, returns false and leaves *in in an
// unspecified state.
extern bool ConsumeDecimalNumber(Slice* in, uint64_t* val);

}  // namespace rocksdb

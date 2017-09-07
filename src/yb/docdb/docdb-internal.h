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

#ifndef YB_DOCDB_DOCDB_INTERNAL_H_
#define YB_DOCDB_DOCDB_INTERNAL_H_

#include "yb/gutil/strings/substitute.h"

// This file should only be included in .cc files of the docdb subsystem. Defines some macros for
// debugging DocDB functionality.

// Enable this during debugging only. This enables very verbose logging. Should always be undefined
// when code is checked in.
#undef DOCDB_DEBUG

#ifdef DOCDB_DEBUG
#define DOCDB_DEBUG_LOG(...) \
  do { \
    LOG(INFO) << "DocDB DEBUG [" << __func__  << "]: " \
              << strings::Substitute(__VA_ARGS__); \
  } while (false)

#else
// Still compile the debug logging code to make sure it does not get broken silently.
#define DOCDB_DEBUG_LOG(...) \
  do { if (false) { strings::Substitute(__VA_ARGS__); } } while (false)
#endif

#endif  // YB_DOCDB_DOCDB_INTERNAL_H_

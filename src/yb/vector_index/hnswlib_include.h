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

#pragma once

#pragma GCC diagnostic push

// For https://gist.githubusercontent.com/mbautin/db70c2fcaa7dd97081b0c909d72a18a8/raw
#pragma GCC diagnostic ignored "-Wunused-function"

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define USE_AVX512
#define USE_AVX
#endif

#include "hnswlib/hnswlib.h"
#include "hnswlib/hnswalg.h"

#undef USE_AVX
#undef USE_AVX512

#pragma GCC diagnostic pop

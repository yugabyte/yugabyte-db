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

// This header is how we include usearch's header files. We need to push/pop some diagnostic
// pragmas. Should be used primarily in implementation .cc files and tests.

#pragma once

#pragma GCC diagnostic push

#ifdef __clang__
// For https://gist.githubusercontent.com/mbautin/87278fc41654c6c74cf7232960364c95/raw
#pragma GCC diagnostic ignored "-Wpass-failed"
#pragma GCC diagnostic ignored "-Wdeprecated-volatile"

#ifdef __aarch64__
// Temporarily disable failing on #warning directives inside index_plugins.hpp. This will become
// unnecessary once we enable SimSIMD.
#pragma GCC diagnostic ignored "-W#warnings"
#endif

#if __clang_major__ == 14
// For https://gist.githubusercontent.com/mbautin/7856257553a1d41734b1cec7c73a0fb4/raw
#pragma GCC diagnostic ignored "-Wambiguous-reversed-operator"
#endif

// Usearch 2.15.1 has unused variables in the insert_sorted function for to_move and
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif  // __clang__

#define SIMSIMD_NATIVE_BF16 0

#include "usearch/index.hpp"
#include "usearch/index_dense.hpp"

#pragma GCC diagnostic pop

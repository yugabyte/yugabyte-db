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

// Hooks run by the main() that ADD_YB_BENCHMARK links into every benchmark binary. Both hooks have
// weak default definitions in benchmark_main.cc; a benchmark binary replaces one by defining it.

#pragma once

namespace yb {

// Runs before any benchmark. Returning false makes main() exit with status 1. The default only
// calls DefaultBenchmarkInit(); a replacement can call it and then do its own setup.
bool BenchmarkInit(int argc, char** argv);

// Runs after all benchmarks have finished. The default does nothing.
void BenchmarkTeardown();

// The default setup: gives --benchmark_* flags and --help to google/benchmark and all other flags
// to gflags, parses flags the same way tests do, and initializes logging. Returns false on a bad
// command line.
bool DefaultBenchmarkInit(int argc, char** argv);

} // namespace yb

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

// main() for microbenchmarks added with ADD_YB_BENCHMARK. It runs the benchmarks registered with
// google/benchmark's BENCHMARK() macros between the BenchmarkInit() and BenchmarkTeardown() hooks
// declared in benchmark_main.h.
//
// --benchmark_* flags and --help go to google/benchmark; all other flags are gflags (use --helpfull
// to list those). Unlike upstream's BENCHMARK_MAIN(), this does not re-execute the binary with ASLR
// disabled; run it under `setarch -R` if you want that.

#include "yb/util/benchmark_main.h"

#include <benchmark/benchmark.h>

#include <string_view>
#include <vector>

#include "yb/gutil/casts.h"
#include "yb/gutil/port.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"

namespace yb {

bool DefaultBenchmarkInit(int argc, char** argv) {
  // Both google/benchmark and gflags parse the command line, and each rejects the other's flags.
  // Give --benchmark_* flags to google/benchmark and everything else to gflags. Splitting also
  // keeps glog's --v working: google/benchmark defines its own --v and would otherwise consume it.
  std::vector<char*> benchmark_args = {argv[0]};
  std::vector<char*> other_args = {argv[0]};
  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    auto& args = arg.starts_with("--benchmark_") || arg == "--help" ? benchmark_args : other_args;
    args.push_back(argv[i]);
  }
  benchmark_args.push_back(nullptr);
  other_args.push_back(nullptr);

  int benchmark_argc = narrow_cast<int>(benchmark_args.size() - 1);
  benchmark::Initialize(&benchmark_argc, benchmark_args.data());
  if (benchmark::ReportUnrecognizedArguments(benchmark_argc, benchmark_args.data())) {
    return false;
  }

  int other_argc = narrow_cast<int>(other_args.size() - 1);
  char** other_argv = other_args.data();
  // Parses flags the same way test_main.cc does, including AutoFlag promotion. The per-test flag
  // overrides in YBTest::SetUp() (e.g. never_fsync) are deliberately not applied.
  ParseCommandLineFlagsForTests(&other_argc, &other_argv);
  InitGoogleLoggingSafeBasic(argv[0]);
  if (other_argc > 1) {
    LOG(ERROR) << "Unexpected positional argument: " << other_argv[1];
    return false;
  }
  return true;
}

ATTRIBUTE_WEAK bool BenchmarkInit(int argc, char** argv) {
  return DefaultBenchmarkInit(argc, argv);
}

ATTRIBUTE_WEAK void BenchmarkTeardown() {}

} // namespace yb

int main(int argc, char** argv) {
  google::InstallFailureSignalHandler();
  if (!yb::BenchmarkInit(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  yb::BenchmarkTeardown();
  benchmark::Shutdown();
  return 0;
}

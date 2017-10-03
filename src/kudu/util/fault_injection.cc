// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/util/fault_injection.h"

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/time.h>

#include "kudu/gutil/once.h"
#include "kudu/util/debug/leakcheck_disabler.h"
#include "kudu/util/monotime.h"
#include "kudu/util/random.h"
#include "kudu/util/random_util.h"

namespace kudu {
namespace fault_injection {

namespace {
GoogleOnceType g_random_once;
Random* g_random;

void InitRandom() {
  LOG(WARNING) << "FAULT INJECTION ENABLED!";
  LOG(WARNING) << "THIS SERVER MAY CRASH!";

  debug::ScopedLeakCheckDisabler d;
  g_random = new Random(GetRandomSeed32());
  ANNOTATE_BENIGN_RACE_SIZED(g_random, sizeof(Random),
                             "Racy random numbers are OK");
}

} // anonymous namespace

void DoMaybeFault(const char* fault_str, double fraction) {
  GoogleOnceInit(&g_random_once, InitRandom);
  if (PREDICT_TRUE(g_random->NextDoubleFraction() >= fraction)) {
    return;
  }

  // Disable core dumps -- it's not useful to get a core dump when we're
  // purposefully crashing, and some tests cause lots of server crashes
  // in a loop. This avoids filling up the disk with useless cores.
  struct rlimit lim;
  PCHECK(getrlimit(RLIMIT_CORE, &lim) == 0);
  lim.rlim_cur = 0;
  PCHECK(setrlimit(RLIMIT_CORE, &lim) == 0);

  // Set coredump_filter to not dump any parts of the address space.
  // Although the above disables core dumps to files, if core_pattern
  // is set to a pipe rather than a file, it's not sufficient. Setting
  // this pattern results in piping a very minimal dump into the core
  // processor (eg abrtd), thus speeding up the crash.
  int f = open("/proc/self/coredump_filter", O_WRONLY);
  if (f >= 0) {
    write(f, "00000000", 8);
    close(f);
  }

  LOG(FATAL) << "Injected fault: " << fault_str;
}

void DoInjectRandomLatency(double max_ms) {
  GoogleOnceInit(&g_random_once, InitRandom);
  SleepFor(MonoDelta::FromMilliseconds(g_random->NextDoubleFraction() * max_ms));
}

} // namespace fault_injection
} // namespace kudu

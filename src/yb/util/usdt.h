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
// User-level statically defined tracing (USDT) probes, for bpftrace and similar tools, e.g.
//   bpftrace -e 'usdt:/path/to/yb-tserver:yb:ash_code { @[arg1] = count(); }'
// A probe is a single nop until a tracer attaches to it. Builds without <sys/sdt.h> (systemtap's
// header) get no probes.

#pragma once

#if defined(__linux__) && defined(__has_include)
#if __has_include(<sys/sdt.h>)
#include <sys/sdt.h>
#define YB_USDT_ENABLED 1
#endif
#endif

#ifdef YB_USDT_ENABLED

#define YB_USDT1(provider, name, a1) DTRACE_PROBE1(provider, name, a1)
#define YB_USDT2(provider, name, a1, a2) DTRACE_PROBE2(provider, name, a1, a2)
#define YB_USDT3(provider, name, a1, a2, a3) DTRACE_PROBE3(provider, name, a1, a2, a3)

#else

#define YB_USDT1(provider, name, a1) do {} while (false)
#define YB_USDT2(provider, name, a1, a2) do {} while (false)
#define YB_USDT3(provider, name, a1, a2, a3) do {} while (false)

#endif

// Copyright 2007 Google, Inc.
//
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
//
// All rights reserved.


// This module gets enough CPU information to optimize the
// atomicops module on x86.

#include "kudu/gutil/atomicops-internals-x86.h"

#include <string.h>

#include <glog/logging.h>
#include "kudu/gutil/logging-inl.h"
#include "kudu/gutil/integral_types.h"

// This file only makes sense with atomicops-internals-x86.h -- it
// depends on structs that are defined in that file.  If atomicops.h
// doesn't sub-include that file, then we aren't needed, and shouldn't
// try to do anything.
#ifdef GUTIL_ATOMICOPS_INTERNALS_X86_H_

// This macro was copied from //util/cpuid/cpuid.cc
// Inline cpuid instruction.  In PIC compilations, %ebx contains the address
// of the global offset table.  To avoid breaking such executables, this code
// must preserve that register's value across cpuid instructions.
#if defined(__i386__)
#define cpuid(a, b, c, d, inp) \
  asm("mov %%ebx, %%edi\n"     \
      "cpuid\n"                \
      "xchg %%edi, %%ebx\n"    \
      : "=a" (a), "=D" (b), "=c" (c), "=d" (d) : "a" (inp))
#elif defined(__x86_64__)
#define cpuid(a, b, c, d, inp) \
  asm("mov %%rbx, %%rdi\n"     \
      "cpuid\n"                \
      "xchg %%rdi, %%rbx\n"    \
      : "=a" (a), "=D" (b), "=c" (c), "=d" (d) : "a" (inp))
#endif

#if defined(cpuid)        // initialize the struct only on x86

// Set the flags so that code will run correctly and conservatively
// until InitGoogle() is called.
struct AtomicOps_x86CPUFeatureStruct AtomicOps_Internalx86CPUFeatures = {
  false,          // bug can't exist before process spawns multiple threads
  false,          // no SSE2
  false,          // no cmpxchg16b
};

// Initialize the AtomicOps_Internalx86CPUFeatures struct.
static void AtomicOps_Internalx86CPUFeaturesInit() {
  uint32 eax;
  uint32 ebx;
  uint32 ecx;
  uint32 edx;

  // Get vendor string (issue CPUID with eax = 0)
  cpuid(eax, ebx, ecx, edx, 0);
  char vendor[13];
  memcpy(vendor, &ebx, 4);
  memcpy(vendor + 4, &edx, 4);
  memcpy(vendor + 8, &ecx, 4);
  vendor[12] = 0;

  // get feature flags in ecx/edx, and family/model in eax
  cpuid(eax, ebx, ecx, edx, 1);

  int family = (eax >> 8) & 0xf;        // family and model fields
  int model = (eax >> 4) & 0xf;
  if (family == 0xf) {                  // use extended family and model fields
    family += (eax >> 20) & 0xff;
    model += ((eax >> 16) & 0xf) << 4;
  }

  // Opteron Rev E has a bug in which on very rare occasions a locked
  // instruction doesn't act as a read-acquire barrier if followed by a
  // non-locked read-modify-write instruction.  Rev F has this bug in
  // pre-release versions, but not in versions released to customers,
  // so we test only for Rev E, which is family 15, model 32..63 inclusive.
  if (strcmp(vendor, "AuthenticAMD") == 0 &&       // AMD
      family == 15 &&
      32 <= model && model <= 63) {
    AtomicOps_Internalx86CPUFeatures.has_amd_lock_mb_bug = true;
  } else {
    AtomicOps_Internalx86CPUFeatures.has_amd_lock_mb_bug = false;
  }

  // edx bit 26 is SSE2 which we use to tell use whether we can use mfence
  AtomicOps_Internalx86CPUFeatures.has_sse2 = ((edx >> 26) & 1);

  // ecx bit 13 indicates whether the cmpxchg16b instruction is supported
  AtomicOps_Internalx86CPUFeatures.has_cmpxchg16b = ((ecx >> 13) & 1);

  VLOG(1) << "vendor " << vendor <<
             "  family " << family <<
             "  model " << model <<
             "  amd_lock_mb_bug " <<
                   AtomicOps_Internalx86CPUFeatures.has_amd_lock_mb_bug <<
             "  sse2 " << AtomicOps_Internalx86CPUFeatures.has_sse2 <<
             "  cmpxchg16b " << AtomicOps_Internalx86CPUFeatures.has_cmpxchg16b;
}

// AtomicOps initialisation routine for external use.
void AtomicOps_x86CPUFeaturesInit() {
  AtomicOps_Internalx86CPUFeaturesInit();
}

#endif

#endif  // GUTIL_ATOMICOPS_INTERNALS_X86_H_

/* Copyright (c) 2008-2009, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The following only applies to changes made to this file as part of YugaByte development.
 *
 * Portions Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 *
 * ---
 * Author: Kostya Serebryany
 */

#ifdef __cplusplus
# error "This file should be built as pure C to avoid name mangling"
#endif

#include <stdlib.h>
#include <string.h>

#include "yb/gutil/dynamic_annotations.h"

#ifdef __GNUC__
/* valgrind.h uses gcc extensions so it won't build with other compilers */
#include "yb/gutil/valgrind.h"
#endif

/* Compiler-based ThreadSanitizer defines
   DYNAMIC_ANNOTATIONS_EXTERNAL_IMPL = 1
   and provides its own definitions of the functions. */

#ifndef DYNAMIC_ANNOTATIONS_EXTERNAL_IMPL
# define DYNAMIC_ANNOTATIONS_EXTERNAL_IMPL 0
#endif

/* Each function is empty and called (via a macro) only in debug mode.
   The arguments are captured by dynamic tools at runtime. */

#if DYNAMIC_ANNOTATIONS_ENABLED == 1 \
    && DYNAMIC_ANNOTATIONS_EXTERNAL_IMPL == 0

void AnnotateRWLockCreate(const char *file, int line, void *lock) {}
void AnnotateRWLockCreateStatic(const char *file, int line, void *lock) {}
void AnnotateRWLockDestroy(const char *file, int line, void *lock) {}
void AnnotateRWLockAcquired(const char *file, int line, void *lock, long is_w) {}

void AnnotateBarrierInit(const char *file, int line,
                         const volatile void *barrier, long count,
                         long reinitialization_allowed) {}
void AnnotateBarrierWaitBefore(const char *file, int line,
                               const volatile void *barrier) {}
void AnnotateBarrierWaitAfter(const char *file, int line,
                              const volatile void *barrier) {}
void AnnotateBarrierDestroy(const char *file, int line,
                            const volatile void *barrier) {}

void AnnotateCondVarWait(const char *file, int line,
                         const volatile void *cv,
                         const volatile void *lock){}
void AnnotateCondVarSignal(const char *file, int line,
                           const volatile void *cv){}
void AnnotateCondVarSignalAll(const char *file, int line,
                              const volatile void *cv){}
void AnnotatePublishMemoryRange(const char *file, int line,
                                const volatile void *address,
                                long size){}
void AnnotateUnpublishMemoryRange(const char *file, int line,
                                  const volatile void *address,
                                  long size){}
void AnnotatePCQCreate(const char *file, int line,
                       const volatile void *pcq){}
void AnnotatePCQDestroy(const char *file, int line,
                        const volatile void *pcq){}
void AnnotatePCQPut(const char *file, int line,
                    const volatile void *pcq){}
void AnnotatePCQGet(const char *file, int line,
                    const volatile void *pcq){}
void AnnotateNewMemory(const char *file, int line, void *mem, size_t size) {}
void AnnotateExpectRace(const char *file, int line,
                        const volatile void *mem,
                        const char *description){}
void AnnotateBenignRace(const char *file, int line,
                        const volatile void *mem,
                        const char *description){}
void AnnotateBenignRaceSized(const char *file, int line,
                             const volatile void *mem,
                             long size,
                             const char *description) {}
void AnnotateMutexIsUsedAsCondVar(const char *file, int line,
                                  const volatile void *mu){}
void AnnotateTraceMemory(const char *file, int line,
                         const volatile void *arg){}
void AnnotateThreadName(const char *file, int line,
                        const char *name){}
void AnnotateIgnoreReadsBegin(const char *file, int line){}
void AnnotateIgnoreReadsEnd(const char *file, int line){}
void AnnotateIgnoreWritesBegin(const char *file, int line){}
void AnnotateIgnoreWritesEnd(const char *file, int line){}
void AnnotateEnableRaceDetection(const char *file, int line, int enable){}
void AnnotateNoOp(const char *file, int line,
                  const volatile void *arg){}
void AnnotateFlushState(const char *file, int line){}

#endif  /* DYNAMIC_ANNOTATIONS_ENABLED == 1
    && DYNAMIC_ANNOTATIONS_EXTERNAL_IMPL == 0 */

#if DYNAMIC_ANNOTATIONS_EXTERNAL_IMPL == 0

static int GetYbRunningOnValgrind(void) {
#ifdef RUNNING_ON_VALGRIND
  if (RUNNING_ON_VALGRIND) return 1;
#endif
  char *running_on_valgrind_str = getenv("RUNNING_ON_VALGRIND");
  if (running_on_valgrind_str) {
    return strcmp(running_on_valgrind_str, "0") != 0;
  }
  return 0;
}

/* See the comments in dynamic_annotations.h */
int YbRunningOnValgrind(void) {
  static volatile int running_on_valgrind = -1;
  int local_running_on_valgrind = running_on_valgrind;
  /* C doesn't have thread-safe initialization of statics, and we
     don't want to depend on pthread_once here, so hack it. */
  ANNOTATE_BENIGN_RACE(&running_on_valgrind, "safe hack");
  if (local_running_on_valgrind == -1)
    running_on_valgrind = local_running_on_valgrind = GetYbRunningOnValgrind();
  return local_running_on_valgrind;
}

/* See the comments in dynamic_annotations.h */
double YbValgrindSlowdown(void) {
  /* Same initialization hack as in YbRunningOnValgrind(). */
  static volatile double slowdown = 0.0;
  double local_slowdown = slowdown;
  ANNOTATE_BENIGN_RACE(&slowdown, "safe hack");
  if (YbRunningOnValgrind() == 0) {
    return 1.0;
  }
  if (local_slowdown == 0.0) {
    char *env = getenv("VALGRIND_SLOWDOWN");
    slowdown = local_slowdown = env ? atof(env) : 50.0;
  }
  return local_slowdown;
}

#endif  /* DYNAMIC_ANNOTATIONS_EXTERNAL_IMPL == 0 */

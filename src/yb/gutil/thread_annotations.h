// Copyright (c) 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// ---
//
//
// This header file contains the macro definitions for thread safety
// annotations that allow the developers to document the locking policies
// of their multi-threaded code. The annotations can also help program
// analysis tools to identify potential thread safety issues.
//
//
// The annotations are implemented using GCC's "attributes" extension.
// Using the macros defined here instead of the raw GCC attributes allows
// for portability and future compatibility.
//

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
#ifndef YB_GUTIL_THREAD_ANNOTATIONS_H
#define YB_GUTIL_THREAD_ANNOTATIONS_H


#if (defined(__GNUC__) && defined(__SUPPORT_TS_ANNOTATION__) && !defined(SWIG)) || \
    defined(__clang__)
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   // no-op
#endif

#if defined(__GNUC__) && !defined(__clang__)

// Document if a shared variable/field needs to be protected by a lock.
// GUARDED_BY allows the user to specify a particular lock that should be
// held when accessing the annotated variable, while GUARDED_VAR only
// indicates a shared variable should be guarded (by any lock). GUARDED_VAR
// is primarily used when the client cannot express the name of the lock.
#define GUARDED_BY(x)          THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))
#define GUARDED_VAR            THREAD_ANNOTATION_ATTRIBUTE__(guarded)

// Document if the memory location pointed to by a pointer should be guarded
// by a lock when dereferencing the pointer. Similar to GUARDED_VAR,
// PT_GUARDED_VAR is primarily used when the client cannot express the name
// of the lock. Note that a pointer variable to a shared memory location
// could itself be a shared variable. For example, if a shared global pointer
// q, which is guarded by mu1, points to a shared memory location that is
// guarded by mu2, q should be annotated as follows:
//     int *q GUARDED_BY(mu1) PT_GUARDED_BY(mu2);
#define PT_GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(point_to_guarded_by(x))
#define PT_GUARDED_VAR \
  THREAD_ANNOTATION_ATTRIBUTE__(point_to_guarded)

// Document the acquisition order between locks that can be held
// simultaneously by a thread. For any two locks that need to be annotated
// to establish an acquisition order, only one of them needs the annotation.
// (i.e. You don't have to annotate both locks with both ACQUIRED_AFTER
// and ACQUIRED_BEFORE.)
#define ACQUIRED_AFTER(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))
#define ACQUIRED_BEFORE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))
#define REQUIRES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

// The following three annotations document the lock requirements for
// functions/methods.

// Document if a function expects certain locks to be held before it is called
#define EXCLUSIVE_LOCKS_REQUIRED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(exclusive_locks_required(__VA_ARGS__))

#define SHARED_LOCKS_REQUIRED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(shared_locks_required(__VA_ARGS__))

// Document the locks acquired in the body of the function. These locks
// non-reentrant).
#define LOCKS_EXCLUDED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

// Document the lock the annotated function returns without acquiring it.
#define LOCK_RETURNED(x)       THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

// Document if a class/type is a lockable type (such as the Mutex class).
#define LOCKABLE               THREAD_ANNOTATION_ATTRIBUTE__(lockable)

// Document if a class is a scoped lockable type (such as the MutexLock class).
#define SCOPED_LOCKABLE        THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

// The following annotations specify lock and unlock primitives.
#define EXCLUSIVE_LOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(exclusive_lock(__VA_ARGS__))

#define SHARED_LOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(shared_lock(__VA_ARGS__))

#define EXCLUSIVE_TRYLOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(exclusive_trylock(__VA_ARGS__))

#define SHARED_TRYLOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(shared_trylock(__VA_ARGS__))

#define UNLOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(unlock(__VA_ARGS__))

#define CAPABILITY(...)

// An escape hatch for thread safety analysis to ignore the annotated function.
#define NO_THREAD_SAFETY_ANALYSIS \
  THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

// Used to mark functions that need to be fixed, because they are producing
// thread safety warnings.  This macro is intended primarily for use by the
// compiler team; it allows new thread safety warnings to be rolled out
// without breaking existing code.  Code which triggers the new warnings are
// marked with a FIXME, and referred back to the code owners to fix.
#define NO_THREAD_SAFETY_ANALYSIS_FIXME \
  THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

// NO_THREAD_SAFETY_ANALYSIS_OPT turns off thread-safety checking in the
// annotated function in opt (NDEBUG) mode.  It is for use specifically when
// the thread-safety checker is failing in opt mode on an otherwise correct
// piece of code.
#ifdef NDEBUG
#define NO_THREAD_SAFETY_ANALYSIS_OPT NO_THREAD_SAFETY_ANALYSIS
#else
#define NO_THREAD_SAFETY_ANALYSIS_OPT
#endif

// TS_UNCHECKED should be placed around lock expressions that are not valid
// C++ syntax, but which are present for documentation purposes.  The
// expressions are passed unchanged to gcc, which will usually treat them
// as the universal lock.
#define TS_UNCHECKED(x) x

// TS_FIXME is used to mark lock expressions that are not valid C++ syntax.
// This annotation should eventually be either fixed, or changed to
// TS_UNCHECKED.
#define TS_FIXME(x) x

// This is used to pass different annotations to gcc and clang, in cases where
// gcc would reject a lock expression (e.g. &MyClass::mu_) that is accepted
// by clang.  This is seldom needed, since GCC usually ignores invalid lock
// expressions except in certain cases, such as LOCK_RETURNED.
#define TS_CLANG_ONLY(CLANG_EXPR, GCC_EXPR) GCC_EXPR

// Some thread safety annotations that are defined for clang in the section below might need to
// be added to this section from time to time as we start using them, so that compilation under
// GCC succeeds.
#define REQUIRES_SHARED(...)
#define ACQUIRE_SHARED(...)
#define RELEASE(...)
#define EXCLUDES(...)

// Clang Attributes
// The names of attributes in the clang analysis are slightly different
#else  // #if defined(__GNUC__) && !defined(__clang__)

#define CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

#define SCOPED_CAPABILITY \
  THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

#define GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define PT_GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

#define ACQUIRED_BEFORE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define ACQUIRED_AFTER(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define REQUIRES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define REQUIRES_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

#define ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

#define ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

#define RELEASE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

#define RELEASE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

#define TRY_ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))

#define TRY_ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(__VA_ARGS__))

#define EXCLUDES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

#define ASSERT_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

#define ASSERT_SHARED_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

#define RETURN_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#define NO_THREAD_SAFETY_ANALYSIS \
  THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

// Deprecated.
#define PT_GUARDED_VAR \
  THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_var)

// Deprecated.
#define GUARDED_VAR \
  THREAD_ANNOTATION_ATTRIBUTE__(guarded_var)

// Replaced by REQUIRES
#define EXCLUSIVE_LOCKS_REQUIRED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(exclusive_locks_required(__VA_ARGS__))

// Replaced by REQUIRES_SHARED
#define SHARED_LOCKS_REQUIRED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(shared_locks_required(__VA_ARGS__))

// Replaced by CAPABILITY
#define LOCKABLE \
  THREAD_ANNOTATION_ATTRIBUTE__(lockable)

// Replaced by SCOPED_CAPABILITY
#define SCOPED_LOCKABLE \
  THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

// Replaced by ACQUIRE
#define EXCLUSIVE_LOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(exclusive_lock_function(__VA_ARGS__))

// Replaced by ACQUIRE_SHARED
#define SHARED_LOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(shared_lock_function(__VA_ARGS__))

// Replaced by RELEASE and RELEASE_SHARED
#define UNLOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(unlock_function(__VA_ARGS__))

// Replaced by TRY_ACQUIRE
#define EXCLUSIVE_TRYLOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(exclusive_trylock_function(__VA_ARGS__))

// Replaced by TRY_ACQUIRE_SHARED
#define SHARED_TRYLOCK_FUNCTION(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(shared_trylock_function(__VA_ARGS__))

// Replaced by ASSERT_CAPABILITY
#define ASSERT_EXCLUSIVE_LOCK(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_exclusive_lock(__VA_ARGS__))

// Replaced by ASSERT_SHARED_CAPABILITY
#define ASSERT_SHARED_LOCK(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_lock(__VA_ARGS__))

// Replaced by EXCLUDE_CAPABILITY.
#define LOCKS_EXCLUDED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

// Replaced by RETURN_CAPABILITY
#define LOCK_RETURNED(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#define NO_THREAD_SAFETY_ANALYSIS_OPT

#define NO_THREAD_SAFETY_ANALYSIS_FIXME \
  THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

#define TS_UNCHECKED(x) ""

#define TS_FIXME(x) ""

#define TS_CLANG_ONLY(CLANG_EXPR, GCC_EXPR) CLANG_EXPR

#endif  // !defined(__GNUC__) || defined(__clang__)

#endif  // YB_GUTIL_THREAD_ANNOTATIONS_H

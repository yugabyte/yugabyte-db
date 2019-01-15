# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

#
# The full list of analyzers can be seen using:
# thirdparty/clang-toolchain/bin/clang -cc1 -analyzer-checker-help

# The following list of analyzers was extracted using:
#
# thirdparty/clang-toolchain/bin/clang -cc1 -analyzer-checker-help | \
#   egrep 'optin|cplusplus|unix|security|valist|nullability|core|alpha' | \
#   egrep -v 'IteratorRange|CloneChecker|SizeofPtr|cocoa|performance.Padding' | \
#   awk '{print $1}'
#
# Then just copy and paste the output into the "set(analyzer_chekers_list ...)" statement below.

# Excluded:
#   alpha.cplusplus.IteratorRange (crashes)
#   alpha.clone.CloneChecker (detects a lot of "duplicate code")
#   alpha.core.SizeofPtr (what's wrong with invoking sizeof on pointers?)
#   optin.performance.Padding (wants us to reoder struct fields, not a correctness issue)

set(analyzer_checkers_list
    alpha.core.BoolAssignment
    alpha.core.CallAndMessageUnInitRefArg
    alpha.core.CastSize
    alpha.core.CastToStruct
    alpha.core.Conversion
    alpha.core.DynamicTypeChecker
    alpha.core.FixedAddr
    alpha.core.IdenticalExpr
    alpha.core.PointerArithm
    alpha.core.PointerSub
    alpha.core.StackAddressAsyncEscape
    alpha.core.TestAfterDivZero
    alpha.cplusplus.DeleteWithNonVirtualDtor
    alpha.cplusplus.MisusedMovedObject
    alpha.cplusplus.UninitializedObject
    alpha.deadcode.UnreachableCode
    alpha.security.ArrayBound
    alpha.security.ArrayBoundV2
    alpha.security.MallocOverflow
    alpha.security.MmapWriteExec
    alpha.security.ReturnPtrRange
    alpha.security.taint.TaintPropagation
    alpha.unix.BlockInCriticalSection
    alpha.unix.Chroot
    alpha.unix.PthreadLock
    alpha.unix.SimpleStream
    alpha.unix.Stream
    alpha.unix.cstring.BufferOverlap
    alpha.unix.cstring.NotNullTerminated
    alpha.unix.cstring.OutOfBounds
    core.CallAndMessage
    core.DivideZero
    core.DynamicTypePropagation
    core.NonNullParamChecker
    core.NonnilStringConstants
    core.NullDereference
    core.StackAddressEscape
    core.UndefinedBinaryOperatorResult
    core.VLASize
    core.builtin.BuiltinFunctions
    core.builtin.NoReturnFunctions
    core.uninitialized.ArraySubscript
    core.uninitialized.Assign
    core.uninitialized.Branch
    core.uninitialized.CapturedBlockVariable
    core.uninitialized.UndefReturn
    cplusplus.InnerPointer
    cplusplus.NewDelete
    cplusplus.NewDeleteLeaks
    cplusplus.SelfAssignment
    nullability.NullPassedToNonnull
    nullability.NullReturnedFromNonnull
    nullability.NullableDereferenced
    nullability.NullablePassedToNonnull
    nullability.NullableReturnedFromNonnull
    optin.cplusplus.VirtualCall
    optin.mpi.MPI-Checker
    optin.performance.GCDAntipattern
    optin.portability.UnixAPI
    osx.coreFoundation.CFError
    osx.coreFoundation.CFNumber
    osx.coreFoundation.CFRetainRelease
    osx.coreFoundation.containers.OutOfBounds
    osx.coreFoundation.containers.PointerSizedValues
    security.FloatLoopCounter
    security.insecureAPI.UncheckedReturn
    security.insecureAPI.bcmp
    security.insecureAPI.bcopy
    security.insecureAPI.bzero
    security.insecureAPI.getpw
    security.insecureAPI.gets
    security.insecureAPI.mkstemp
    security.insecureAPI.mktemp
    security.insecureAPI.rand
    security.insecureAPI.strcpy
    security.insecureAPI.vfork
    unix.API
    unix.Malloc
    unix.MallocSizeof
    unix.MismatchedDeallocator
    unix.StdCLibraryFunctions
    unix.Vfork
    unix.cstring.BadSizeArg
    unix.cstring.NullArg
    valist.CopyToSelf
    valist.Uninitialized
    valist.Unterminated)

# Create a comma-separated list of analyzers.
set(analyzer_checkers_str "")
foreach(analyzer_checker IN LISTS analyzer_checkers_list)
  if (NOT "${analyzer_checkers_str}" STREQUAL "")
    set(analyzer_checkers_str "${analyzer_checkers_str},")
  endif()
  set(analyzer_checkers_str "${analyzer_checkers_str}${analyzer_checker}")
endforeach(analyzer_checker)

SET(clang_analyzer_flags "-Xanalyzer -analyzer-checker=${analyzer_checkers_str}")

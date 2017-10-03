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

// This file contains all of the functions that must be precompiled
// to an LLVM IR format (note: not bitcode to preserve function
// names for retrieval later).
//
// Note namespace scope is just for convenient symbol resolution.
// To preserve function names, extern "C" linkage is used, so these
// functions (1) must not be duplicated in any of the above headers
// and (2) do not belong to namespace kudu.
//
// NOTE: This file may rely on external definitions from any part of Kudu
// because the code generator will resolve external symbols at load time.
// However, the code generator relies on the fact that our Kudu binaries
// are built with unstripped visible symbols, so this style of code generation
// cannot be used in builds with settings that conflict with the required
// visibility (e.g., the client library).
// NOTE: This file is NOT compiled with ASAN annotations, even if Kudu
// is being built with ASAN.

#include <cstdlib>
#include <cstring>

#include "kudu/common/rowblock.h"
#include "kudu/util/bitmap.h"
#include "kudu/util/memory/arena.h"

// Even though this file is only needed for IR purposes, we need to check for
// IR_BUILD because we use a fake static library target to workaround a cmake
// dependencies bug. See 'ir_fake_target' in CMakeLists.txt.
#ifdef IR_BUILD

// This file uses the 'always_inline' attribute on a bunch of functions to force
// the LLVM optimizer at runtime to inline them where it otherwise might not.
// Because the functions themselves aren't marked 'inline', gcc is unhappy with this.
// But, we can't mark them 'inline' or else they'll get optimized away and not even
// included in the .ll file. So, instead, we just mark them as always_inline in
// the IR_BUILD context.
#define IR_ALWAYS_INLINE __attribute__((always_inline))

// Workaround for an MCJIT deficiency where we see a link error when trying
// to load the JITted library. See the following LLVM bug and suggested workaround.
// https://llvm.org/bugs/show_bug.cgi?id=18062
extern "C" void *__dso_handle __attribute__((__visibility__("hidden"))) = NULL;

#else
#define IR_ALWAYS_INLINE
#endif

namespace kudu {

// Returns whether copy was successful (fails iff slice relocation fails,
// which can only occur if is_string is true).
// If arena is NULL, then no relocation occurs.
IR_ALWAYS_INLINE static bool BasicCopyCell(
    uint64_t size, uint8_t* src, uint8_t* dst, bool is_string, Arena* arena) {
  // Relocate indirect data
  if (is_string) {
    if (PREDICT_TRUE(arena != nullptr)) {
      return PREDICT_TRUE(arena->RelocateSlice(*reinterpret_cast<Slice*>(src),
                                               reinterpret_cast<Slice*>(dst)));
    }
    // If arena is NULL, don't relocate, but do copy the pointers to the raw
    // data (callers that pass arena as NULL should be sure that the indirect
    // data will stay alive after the projections)
  }

  // Copy direct data
  memcpy(dst, src, size);
  return true;
}

extern "C" {

// Preface all used functions with _Precompiled to avoid the possibility
// of name clashes. Notice all the nontrivial types must be passed as
// void* parameters, otherwise LLVM will complain that the type does not match
// (and it is not possible to consistently extract the llvm::Type* from a
// parsed module which has the same signature as the one that would be passed
// as a parameter for the below functions if the did not use void* types).
//
// Note that:
//   (1) There is no void* type in LLVM, instead i8* is used.
//   (2) The functions below are all prefixed with _Precompiled to avoid
//       any potential naming conflicts.


// declare i1 @_PrecompiledCopyCellToRowBlock(
//   i64 size, i8* src, RowBlockRow* dst, i64 col, i1 is_string, Arena* arena)
//
//   Performs the same function as CopyCell, copying size bytes of the
//   cell pointed to by src to the cell of column col in the row pointed
//   to by dst, copying indirect data to the parameter arena if is_string
//   is true. Will hard crash if insufficient memory is available for
//   relocation. Copies size bytes directly from the src cell.
//   If arena is NULL then only the direct copy will occur.
//   Returns whether successful. If not, out-of-memory during relocation of
//   slices has occured, which can only happen if is_string is true.
IR_ALWAYS_INLINE bool _PrecompiledCopyCellToRowBlock(
    uint64_t size, uint8_t* src, RowBlockRow* dst,
    uint64_t col, bool is_string, Arena* arena) {

  // We manually compute the destination cell pointer here, rather than
  // using dst->cell_ptr(), since we statically know the size of the column
  // type. Using the normal access path would generate an 'imul' instruction,
  // since it would be loading the column type info from the RowBlock object
  // instead of our static parameter here.
  size_t idx = dst->row_index();
  const RowBlock* block = dst->row_block();
  uint8_t* dst_cell = block->column_data_base_ptr(col) + idx * size;
  return BasicCopyCell(size, src, dst_cell, is_string, arena);
}

// declare i1 @_PrecompiledCopyCellToRowBlockNullable(
//   i64 size, i8* src, RowBlockRow* dst, i64 col, i1 is_string, Arena* arena,
//   i8* src_bitmap, i64 bitmap_idx)
//
//   Performs the same function as _PrecompiledCopyCellToRowBlock but for nullable
//   columns. Checks the parameter bitmap at the specified index and updates
//   The row's bitmap accordingly. Then goes on to copy the cell over if it
//   is not null.
//   If arena is NULL then only the direct copy will occur (if the source
//   bitmap indicates the cell itself is non-null).
//   Returns whether successful. If not, out-of-memory during relocation of
//   slices has occured, which can only happen if is_string is true.
IR_ALWAYS_INLINE bool _PrecompiledCopyCellToRowBlockNullable(
    uint64_t size, uint8_t* src, RowBlockRow* dst, uint64_t col, bool is_string,
    Arena* arena, uint8_t* src_bitmap, uint64_t bitmap_idx) {
  // Using this method implies the nullablity of the column.
  // Write whether the column is nullable to the RowBlock's ColumnBlock's bitmap
  bool is_null = BitmapTest(src_bitmap, bitmap_idx);
  dst->cell(col).set_null(is_null);
  // No more copies necessary if null
  if (is_null) return true;
  return _PrecompiledCopyCellToRowBlock(size, src, dst, col, is_string, arena);
}

// declare void @_PrecompiledSetRowBlockCellSetNull
//   RowBlockRow* %dst, i64 <column index>, i1 %is_null)
//
//   Sets the cell at column 'col' for destination RowBlockRow 'dst'
//   to be marked as 'is_null' (requires the column is nullable).
IR_ALWAYS_INLINE void _PrecompiledCopyCellToRowBlockSetNull(
    RowBlockRow* dst, uint64_t col, bool is_null) {
  dst->cell(col).set_null(is_null);
}

} // extern "C"
} // namespace kudu

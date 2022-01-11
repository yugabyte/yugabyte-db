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

#include "yb/util/monotime.h"
#include "yb/util/random.h"
#include "yb/util/striped64.h"
#include "yb/util/threadlocal.h"

using yb::striped64::internal::HashCode;
using yb::striped64::internal::Cell;

namespace yb {

namespace striped64 {
namespace internal {
//
// HashCode
//

HashCode::HashCode() {
  std::mt19937_64 random(MonoTime::Now().GetDeltaSince(MonoTime::Min()).ToNanoseconds());
  const uint64_t hash = random();
  code_ = (hash == 0) ? 1 : hash;  // Avoid zero to allow xorShift rehash
}

//
// Cell
//

Cell::Cell()
    : value_(0) {
}
} // namespace internal
} // namespace striped64

//
// Striped64
//
namespace {

const int64_t kNumCpus = sysconf(_SC_NPROCESSORS_ONLN);
thread_local std::unique_ptr<HashCode> hashcode_;

}

Striped64::Striped64()
    : busy_(false),
      cell_buffer_(nullptr),
      cells_(nullptr),
      num_cells_(0) {
}

Striped64::~Striped64() {
  // Cell is a POD, so no need to destruct each one.
  free(cell_buffer_);
}

void Striped64::RetryUpdate(int64_t x, Rehash contention) {
  uint64_t h = hashcode_->code_;
  // There are three operations in this loop.
  //
  // 1. Try to add to the Cell hash table entry for the thread if the table exists.
  //    When there's contention, rehash to try a different Cell.
  // 2. Try to initialize the hash table.
  // 3. Try to update the base counter.
  //
  // These are predicated on successful CAS operations, which is why it's all wrapped in an
  // infinite retry loop.
  while (true) {
    int32_t n = base::subtle::Acquire_Load(&num_cells_);
    if (n > 0) {
      if (contention == kRehash) {
        // CAS failed already, rehash before trying to increment.
        contention = kNoRehash;
      } else {
        Cell *cell = &(cells_[(n - 1) & h]);
        int64_t v = cell->value_.Load();
        if (cell->CompareAndSet(v, Fn(v, x))) {
          // Successfully CAS'd the corresponding cell, done.
          break;
        }
      }
      // Rehash since we failed to CAS, either previously or just now.
      h ^= h << 13;
      h ^= h >> 17;
      h ^= h << 5;
    } else if (n == 0 && CasBusy()) {
      // We think table hasn't been initialized yet, try to do so.
      // Recheck preconditions, someone else might have init'd in the meantime.
      n = base::subtle::Acquire_Load(&num_cells_);
      if (n == 0) {
        n = 1;
        // Calculate the size. Nearest power of two >= NCPU.
        // Also handle a negative NCPU, can happen if sysconf name is unknown
        while (kNumCpus > n) {
          n <<= 1;
        }
        // Allocate cache-aligned memory for use by the cells_ table.
        int err = posix_memalign(&cell_buffer_, CACHELINE_SIZE, sizeof(Cell)*n);
        CHECK_EQ(0, err) << "error calling posix_memalign" << std::endl;
        // Initialize the table
        cells_ = new (cell_buffer_) Cell[n];
        base::subtle::Release_Store(&num_cells_, n);
      }
      // End critical section
      busy_.Store(0);
    } else {
      // Fallback to adding to the base value.
      // Means the table wasn't initialized or we failed to init it.
      int64_t v = base_.value_.Load();
      if (CasBase(v, Fn(v, x))) {
        break;
      }
    }
  }
  // Record index for next time
  hashcode_->code_ = h;
}

void Striped64::InternalReset(int64_t initialValue) {
  const int32_t n = base::subtle::Acquire_Load(&num_cells_);
  base_.value_.Store(initialValue);
  for (int i = 0; i < n; i++) {
    cells_[i].value_.Store(initialValue);
  }
}

void LongAdder::IncrementBy(int64_t x) {
  if (!hashcode_) {
    hashcode_ = std::make_unique<HashCode>();
  }
  // Use hash table if present. If that fails, call RetryUpdate to rehash and retry.
  // If no hash table, try to CAS the base counter. If that fails, RetryUpdate to init the table.
  const int32_t n = base::subtle::Acquire_Load(&num_cells_);
  if (n > 0) {
    Cell *cell = &(cells_[(n - 1) & hashcode_->code_]);
    DCHECK_EQ(0, reinterpret_cast<const uintptr_t>(cell) & (sizeof(Cell) - 1))
        << " unaligned Cell not allowed for Striped64" << std::endl;
    const int64_t old = cell->value_.Load();
    if (!cell->CompareAndSet(old, old + x)) {
      // When we hit a hash table contention, signal RetryUpdate to rehash.
      RetryUpdate(x, kRehash);
    }
  } else {
    int64_t b = base_.value_.Load();
    if (!base_.CompareAndSet(b, b + x)) {
      // Attempt to initialize the table. No need to rehash since the contention was for the
      // base counter, not the hash table.
      RetryUpdate(x, kNoRehash);
    }
  }
}

//
// LongAdder
//

int64_t LongAdder::Value() const {
  int64_t sum = base_.value_.Load();
  const int32_t n = base::subtle::Acquire_Load(&num_cells_);
  for (int i = 0; i < n; i++) {
    sum += cells_[i].value_.Load();
  }
  return sum;
}

} // namespace yb

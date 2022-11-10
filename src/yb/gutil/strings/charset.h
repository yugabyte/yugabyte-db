// Copyright 2008 Google Inc. All Rights Reserved.
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

#pragma once

#include "yb/gutil/integral_types.h"

namespace strings {

// A CharSet is a simple map from (1-byte) characters to Booleans. It simply
// exposes the mechanism of checking if a given character is in the set, fairly
// efficiently. Useful for string tokenizing routines.
//
// Run on asherah (2 X 2400 MHz CPUs); 2008/11/10-13:18:03
// CPU: Intel Core2 (2 cores) dL1:32KB dL2:4096KB
// ***WARNING*** CPU scaling is enabled, the benchmark timings may be noisy,
// Benchmark                Time(ns)    CPU(ns) Iterations
// -------------------------------------------------------
// BM_CharSetTesting/1K           21         21   32563138
// BM_CharSetTesting/4K           21         21   31968433
// BM_CharSetTesting/32K          21         21   32114953
// BM_CharSetTesting/256K         22         22   31679082
// BM_CharSetTesting/1M           21         21   32563138
//
// This class is thread-compatible.
//
// This class has an implicit constructor.
// Style guide exception granted:
// http://goto/style-guide-exception-20978288

class CharSet {
 public:
  // Initialize a CharSet containing no characters or the given set of
  // characters, respectively.
  CharSet();
  // Deliberately an implicit constructor, so anything that takes a CharSet
  // can also take an explicit list of characters.
  CharSet(const char* characters);  // NOLINT(runtime/explicit)
  explicit CharSet(const CharSet& other);

  // Add or remove a character from the set.
  void Add(unsigned char c) { bits_[Word(c)] |= BitMask(c); }
  void Remove(unsigned char c) { bits_[Word(c)] &= ~BitMask(c); }

  // Return true if this character is in the set
  bool Test(unsigned char c) const { return bits_[Word(c)] & BitMask(c); }

 private:
  // The numbers below are optimized for 64-bit hardware. TODO(user): In the
  // future, we should change this to use uword_t and do various bits of magic
  // to calculate the numbers at compile time.

  // In general,
  // static const int kNumWords = max(32 / sizeof(uword_t), 1);
  uint64 bits_[4];

  // 4 words => the high 2 bits of c are the word number. In general,
  // kShiftValue = 8 - log2(kNumWords)
  static int Word(unsigned char c) { return c >> 6; }

  // And the value we AND with c is ((1 << shift value) - 1)
  // static const int kLowBitsMask = (256 / kNumWords) - 1;
  static uint64 BitMask(unsigned char c) {
    uint64 mask = 1;
    return mask << (c & 0x3f);
  }
};

}  // namespace strings

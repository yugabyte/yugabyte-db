// Copyright 2011 Google Inc. All Rights Reserved.
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
// The core Jenkins Lookup2-based hashing routines. These are legacy hashing
// routines and should be avoided in new code. Their implementations are dated
// and cannot be changed due to values being recorded and breaking if not
// preserved. New code which explicitly desires this property should use the
// consistent hashing libraries. New code which does not explicitly desire this
// behavior should use the generic hashing routines in hash.h.

#pragma once

#include <stddef.h>
#include "yb/gutil/integral_types.h"

// ----------------------------------------------------------------------
// Hash32StringWithSeed()
// Hash64StringWithSeed()
// Hash32NumWithSeed()
// Hash64NumWithSeed()
//   These are Bob Jenkins' hash functions, one for 32 bit numbers
//   and one for 64 bit numbers.  Each takes a string as input and
//   a start seed.  Hashing the same string with two different seeds
//   should give two independent hash values.
//      The *Num*() functions just do a single mix, in order to
//   convert the given number into something *random*.
//
// Note that these methods may return any value for the given size, while
// the corresponding HashToXX() methods avoids certain reserved values.
// ----------------------------------------------------------------------

// These slow down a lot if inlined, so do not inline them  --Sanjay
uint32 Hash32StringWithSeed(const char *s, size_t len, uint32 c);
uint64 Hash64StringWithSeed(const char *s, size_t len, uint64 c);

// This is a reference implementation of the same fundamental algorithm as
// Hash32StringWithSeed. It is used primarily as a performance metric.
uint32 Hash32StringWithSeedReferenceImplementation(const char *s, size_t len, uint32 c);

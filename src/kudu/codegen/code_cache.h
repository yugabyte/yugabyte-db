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

#ifndef KUDU_CODEGEN_CODE_CACHE_H
#define KUDU_CODEGEN_CODE_CACHE_H

#include "kudu/codegen/row_projector.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/faststring.h"

namespace kudu {

class Cache;
class CacheDeleter;
class Schema;

namespace codegen {

class JITWrapper;

// A code cache is a specialized LRU cache with the following services:
//   1. It supports only one writer at a time, but multiple concurrent
//      readers.
//   2. If its items are taking too much space, it evicts the least-
//      recently-used member of the cache.
//
// The cache takes shared ownership of its entry values, the JITWrappers,
// by incrementing their reference count.
//
// LRU eviction does not guarantee that a JITWrapper is deleted, only that
// the cache releases its shared ownership (by decrementing the reference
// count) of the jit code.
class CodeCache {
 public:
  // TODO: currently CodeCache is implemented using the Cache in
  // kudu/util/cache.h, which requires some transformation to nongeneric
  // Slice-type keys, and void* values. Furthermore, the Cache implementation
  // provides concurrent write guarantees (thus relies on locks heavily), which
  // is unnecessary for the CodeCache. A potential improvement would be to
  // implement a single-writer multi-reader LRU cache with proper generics.

  // TODO: a potential improvment would be for the cache to monitor its memory
  // consumption explicity and keep its usage under a size limit specified at
  // construction time. In order to do this, the cache would have to inject
  // a custom memory manager into the CodeGenerator's execution engine which
  // intercepts allocation calls and tracks code size.

  // Generates an empty code cache which stores at most 'capacity' JITWrappers.
  // A JIT payload is defined to be the combination of objects which rely on jitted
  // code and the classes which own the jitted code.
  explicit CodeCache(size_t capacity);
  ~CodeCache();

  // This function is NOT thread safe (only one writer may call this at
  // a time). Attempts to add a new entry 'wrapper' to the cache, using
  // wrapper->EncodeOwnKey() as the key. Overwrites the previous value
  // if one exists. If insertion results in excess capacity, LRU eviction
  // occurs. Returns Status::OK() upon success.
  Status AddEntry(const scoped_refptr<JITWrapper>& wrapper);

  // This function may be called from any thread concurrently with other
  // writes and reads to the cache. Looks in the cache for the specified key.
  // Returns a reference to the associated payload, or NULL if no such entry
  // exists in the cache.
  scoped_refptr<JITWrapper> Lookup(const Slice& key);

 private:

  gscoped_ptr<CacheDeleter> deleter_;
  gscoped_ptr<Cache> cache_;

  DISALLOW_COPY_AND_ASSIGN(CodeCache);
};

} // namespace codegen
} // namespace kudu

#endif

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

#include "kudu/codegen/code_cache.h"

#include "kudu/codegen/jit_wrapper.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/cache.h"
#include "kudu/util/faststring.h"
#include "kudu/util/slice.h"

namespace kudu {
namespace codegen {

namespace {

class Deleter : public CacheDeleter {
 public:
  Deleter() {}
  virtual void Delete(const Slice& key, void* value) OVERRIDE {
    // The Cache from cache.h deletes the memory that it allocates for its
    // own copy of key, but it expects its users to delete their own
    // void* values. To delete, we just release our shared ownership.
    static_cast<JITWrapper*>(value)->Release();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(Deleter);
};

} // anonymous namespace

CodeCache::CodeCache(size_t capacity)
  : cache_(NewLRUCache(DRAM_CACHE, capacity, "code_cache")) {
  deleter_.reset(new Deleter());
}

CodeCache::~CodeCache() {}

Status CodeCache::AddEntry(const scoped_refptr<JITWrapper>& value) {
  // Get the key
  faststring key;
  RETURN_NOT_OK(value->EncodeOwnKey(&key));

  // Because Cache only accepts void* values, we store just the JITWrapper*
  // and increase its ref count.
  value->AddRef();

  // Insert into cache and release the handle (we have a local copy of a refptr).
  // We CHECK_NOTNULL because this is always a DRAM-based cache, and if allocation
  // failed, we'd just crash the process.
  Cache::Handle* inserted = CHECK_NOTNULL(cache_->Insert(key, value.get(), 1, deleter_.get()));
  cache_->Release(inserted);
  return Status::OK();
}

scoped_refptr<JITWrapper> CodeCache::Lookup(const Slice& key) {
  // Look up in Cache after generating key, returning NULL if not found.
  Cache::Handle* found = cache_->Lookup(key, Cache::EXPECT_IN_CACHE);
  if (!found) return scoped_refptr<JITWrapper>();

  // Retrieve the value
  scoped_refptr<JITWrapper> value =
    static_cast<JITWrapper*>(cache_->Value(found));

  // No need to hold on to handle after we have our copy
  cache_->Release(found);

  return value;
}

} // namespace codegen
} // namespace kudu

// Copyright (c) YugabyteDB, Inc.
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

#include "yb/rocksdb/table/block.h"
#include "yb/rocksdb/table/format.h"
#include "yb/rocksdb/util/crc32c.h"
#include "yb/rocksdb/util/xxhash.h"

#include "yb/ash/wait_state.h"

#include "yb/util/file_system.h"
#include "yb/util/logging.h"

namespace rocksdb {

namespace block_based_table {

constexpr char kFilterBlockPrefix[] = "filter.";
constexpr char kFullFilterBlockPrefix[] = "fullfilter.";
constexpr char kFixedSizeFilterBlockPrefix[] = "fixedsizefilter.";

// Read the block identified by "handle" from "file".
// The only relevant option is options.verify_checksums for now.
// On failure return non-OK.
// On success fill *result and return OK - caller owns *result
inline Status ReadBlockFromFile(
    RandomAccessFileReader* file, const Footer& footer, const ReadOptions& options,
    const BlockHandle& handle, std::unique_ptr<Block>* result, Env* env,
    const std::shared_ptr<yb::MemTracker>& mem_tracker,
    bool do_uncompress = true) {
  SCOPED_WAIT_STATUS(RocksDB_ReadBlockFromFile);
  BlockContents contents;
  Status s = ReadBlockContents(file, footer, options, handle, &contents, env,
                               mem_tracker, do_uncompress);
  if (s.ok()) {
    result->reset(new Block(std::move(contents)));
  }

  return s;
}

// The longest prefix of the cache key used to identify blocks.
// We are using the fact that we know the size of the unique ID for Posix files.
// Contains: inode + mtime (encoded with fixed32) + checksum (encoded with fixed32).
static constexpr size_t kMaxCacheKeyPrefixSize =
    yb::FileWithNameAndUniqueId::kPosixFileUniqueIdMaxSize + 2 * sizeof(uint32_t) + 1;
static constexpr size_t kCacheKeyBufferSize =
    block_based_table::kMaxCacheKeyPrefixSize + yb::kMaxVarint64Length;

struct CacheKeyPrefixBuffer {
  char data[kMaxCacheKeyPrefixSize];
  size_t size = 0;
};

inline Slice GetCacheKey(const CacheKeyPrefixBuffer& cache_key_prefix, const BlockHandle& handle,
    char* cache_key) {
  DCHECK_ONLY_NOTNULL(cache_key);
  DCHECK_NE(cache_key_prefix.size, 0);
  DCHECK_LE(cache_key_prefix.size, kMaxCacheKeyPrefixSize);
  memcpy(cache_key, cache_key_prefix.data, cache_key_prefix.size);
  char* end = EncodeVarint64(cache_key + cache_key_prefix.size, handle.offset());
  return Slice(cache_key, static_cast<size_t>(end - cache_key));
}

// Generate a cache key prefix from the file. Used for both data and metadata files.
inline void GenerateCachePrefix(
    Cache* cc, yb::FileWithNameAndUniqueId* file, uint32_t meta_block_checksum,
    CacheKeyPrefixBuffer* prefix) {
  // generate an id from the file
  prefix->size = file->GetUniqueId(prefix->data);

  // If the prefix wasn't generated or was too long,
  // create one from the cache.
  if (prefix->size == 0) {
    char* end = EncodeVarint64(prefix->data, cc->NewId());
    prefix->size = static_cast<size_t>(end - prefix->data);
  } else {
    // Add meta-block checksum
    EncodeFixed32(prefix->data + prefix->size, meta_block_checksum);
    prefix->size += sizeof(uint32_t);
  }
  LOG(INFO) << "Generated cache prefix " << Slice(prefix->data, prefix->size) << " for file "
            << file->filename();
}

// The function is used to compute a checksum of meta-block to construct block cache key.
// The checksum may be different from one stored in disk. Only support kCRC32c and kxxHash
// for now. Different checksum algorithms with lower collision rate may be used in future.
inline uint32_t ComputeChecksumForBlockCacheKey(ChecksumType type, const Slice& block_contents) {
  switch(type) {
    case kNoChecksum:
    case kCRC32c: {
        return crc32c::Value(block_contents.data(), block_contents.size());
    }
    case kxxHash: {
      void* xxh = XXH32_init(0);
      XXH32_update(xxh, block_contents.data(),
                   static_cast<uint32_t>(block_contents.size()));
      return XXH32_digest(xxh);
    }
  }
  FATAL_INVALID_ENUM_VALUE(ChecksumType, type);
}

} // namespace block_based_table

} // namespace rocksdb

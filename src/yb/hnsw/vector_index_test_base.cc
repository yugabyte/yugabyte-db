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

#include "yb/hnsw/vector_index_test_base.h"

#include "yb/gutil/endian.h"

#include "yb/rocksdb/cache.h"

#include "yb/util/env.h"
#include "yb/util/faststring.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"

namespace yb::hnsw {

void VectorIndexTestBase::RandomVector(Vector& out) {
  out.clear();
  out.reserve(dimensions_);
  std::uniform_real_distribution<float> dist(0.0, 1.0);
  while (out.size() < dimensions_) {
    out.push_back(dist(rng_));
  }
}

Vector VectorIndexTestBase::RandomVector() {
  Vector result;
  RandomVector(result);
  return result;
}

std::vector<Vector> VectorIndexTestBase::RandomVectors(size_t num_vectors) {
  std::vector<Vector> result(num_vectors);
  for (auto& vector : result) {
    RandomVector(vector);
  }
  return result;
}

void VectorIndexTestBase::SetUp() {
  YBTest::SetUp();
  rocksdb_cache_ = rocksdb::NewLRUCache(BlockCacheCapacity(), 4);;
  block_cache_ = std::make_shared<BlockCache>(
      *Env::Default(),
      MemTracker::GetRootTracker()->FindOrCreateTracker(1ULL << 30, "block_cache"),
      metric_entity_,
      *rocksdb_cache_);
}

namespace {

// Returns the offset of the footer, which starts with the format version.
Result<size_t> YbHnswFooterOffset(const faststring& content, const std::string& path) {
  SCHECK_GE(content.size(), sizeof(uint64_t), IllegalState, Format("$0 is too small", path));
  const auto footer_size = LittleEndian::Load64(
      content.data() + content.size() - sizeof(uint64_t));
  SCHECK_LT(footer_size, content.size(), IllegalState, Format("Bad footer size in $0", path));
  return content.size() - footer_size;
}

} // namespace

Status VectorIndexTestBase::DamageYbHnswBlocks(const std::string& path, size_t stride) {
  faststring content;
  RETURN_NOT_OK(ReadFileToString(env_.get(), path, &content));
  const auto footer_offset = VERIFY_RESULT(YbHnswFooterOffset(content, path));
  for (size_t pos = 0; pos < footer_offset; pos += stride) {
    content[pos] ^= 0xff;
  }
  return WriteStringToFile(env_.get(), content, path);
}

Result<uint8_t> VectorIndexTestBase::YbHnswFormatVersion(const std::string& path) {
  faststring content;
  RETURN_NOT_OK(ReadFileToString(env_.get(), path, &content));
  return content[VERIFY_RESULT(YbHnswFooterOffset(content, path))];
}

size_t VectorIndexTestBase::BlockCacheCapacity() {
  return 8_MB;
}

} // namespace yb::hnsw

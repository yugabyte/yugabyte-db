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

#include <cstddef>
#include <cstdint>
#include <string>

#include "yb/gutil/endian.h"

#include "yb/util/enums.h"
#include "yb/util/result.h"

#include "yb/vector_index/distance.h"
#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/hnsw_options.h"
#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::tools {

// Integer vertex ids, also used as the keys of bigint pgvector tables.
inline vector_index::VectorId UIntToVectorId(uint64_t value) {
  auto id = vector_index::VectorId::Nil();
  BigEndian::Store64(id.data(), value);
  return id;
}

inline uint64_t VectorIdToUInt(vector_index::VectorId value) {
  return BigEndian::Load64(value.data());
}

// Type of the table's `id` column. kBigint is for tables loaded by other tools.
YB_DEFINE_ENUM(PgVectorIdType, (kUuid)(kBigint));

// Configuration of the hnsw_tool pgvector backend.
struct PgVectorConfig {
  std::string host = "127.0.0.1";
  uint16_t port = 5433;
  std::string user = "yugabyte";
  std::string password;
  std::string database = "yugabyte";
  std::string table = "vi_benchmark";

  // ybhnsw index name. Empty accepts any matching ybhnsw index and auto-names a new one.
  std::string index;

  // kBigint requires `read_only`, since tables created by the backend use uuid.
  PgVectorIdType id_type = PgVectorIdType::kUuid;

  // Never modify the cluster. Reserve() fails if the table or index is missing or not ready.
  bool read_only = false;

  // Connection pool size. Zero means the max of the concurrency limits passed to Reserve().
  size_t num_connections = 0;

  // Rows per COPY batch.
  size_t batch_size = 1024;
};

// Implemented by the pgvector VectorIndexIf, reachable via dynamic_cast.
class PgVectorIndexInfo {
 public:
  virtual ~PgVectorIndexInfo() = default;

  // True if Reserve() found the dataset already loaded and indexed, so Insert must be skipped.
  virtual bool DataAlreadyExists() const = 0;
};

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
vector_index::VectorIndexTraitsPtr<Vector, DistanceResult> CreatePgVectorIndexTraits(
    const vector_index::HNSWOptions& options, PgVectorConfig config);

}  // namespace yb::tools

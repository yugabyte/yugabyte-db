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

#include <optional>

#include "yb/common/common.pb.h"
#include "yb/common/hybrid_time.h"

#include "yb/docdb/doc_vector_index.h"

#include "yb/qlexpr/index.h"

#include "yb/util/metrics.h"
#include "yb/util/test_util.h"

METRIC_DEFINE_entity(table);

namespace yb::docdb {

namespace {

class StubVectorIndexContext : public DocVectorIndexContext {
 public:
  Result<DocVectorIndexReverseMappingReaderPtr> CreateReverseMappingReader(
      const ReadHybridTime&, DocDBStatistics*) const override {
    return STATUS(NotSupported, "Not expected to be called");
  }
};

} // namespace

class DocVectorIndexTest : public YBTest {
 protected:
  Result<DocVectorIndexPtr> CreateIndex(std::optional<HnswBackend> backend) {
    IndexInfoPB index_info_pb;
    index_info_pb.set_table_id("test_vector_index");
    auto& options = *index_info_pb.mutable_vector_idx_options();
    options.set_dist_type(PgVectorDistanceType::DIST_L2);
    options.set_idx_type(PgVectorIndexType::HNSW);
    options.set_dimensions(4);
    options.set_id("1");
    auto& hnsw = *options.mutable_hnsw();
    hnsw.set_m(16);
    hnsw.set_m0(32);
    hnsw.set_ef_construction(64);
    if (backend) {
      hnsw.set_backend(*backend);
    }

    return CreateDocVectorIndex(
        "T test: ", GetTestPath("vector_index"),
        [] { return DocVectorIndexThreadPools{}; }, Slice(), TableWritesReverseMapping::kFalse,
        HybridTime::kMin,
        /* split_generation= */ 0, qlexpr::IndexInfo(index_info_pb),
        std::make_unique<StubVectorIndexContext>(), /* block_cache= */ nullptr,
        /* mem_tracker= */ nullptr,
        METRIC_ENTITY_table.Instantiate(&metric_registry_, "test"));
  }

  MetricRegistry metric_registry_;
};

TEST_F(DocVectorIndexTest, DeprecatedBackendNotSupported) {
  // Unset backend reads as DEPRECATED_USEARCH: such indexes predate block-based backends.
  for (auto backend : {std::optional<HnswBackend>(),
                       std::optional(HnswBackend::DEPRECATED_USEARCH),
                       std::optional(HnswBackend::DEPRECATED_HNSWLIB)}) {
    auto result = CreateIndex(backend);
    ASSERT_NOK(result);
    ASSERT_TRUE(result.status().IsNotSupported()) << result.status();
    ASSERT_STR_CONTAINS(result.status().ToString(), "drop and recreate the index");
  }
}

} // namespace yb::docdb

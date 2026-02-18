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

#include <google/protobuf/any.pb.h>

#include "yb/ann_methods/ann_methods.h"
#include "yb/ann_methods/hnswlib_wrapper.h"
#include "yb/ann_methods/usearch_wrapper.h"
#include "yb/ann_methods/vector_lsm-test.pb.h"

#include "yb/rocksdb/metadata.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/path_util.h"
#include "yb/util/priority_thread_pool.h"
#include "yb/util/test_util.h"
#include "yb/util/thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/vector_index/vector_lsm.h"
#include "yb/vector_index/vectorann_util.h"

using namespace std::literals;

DECLARE_bool(TEST_usearch_exact);
DECLARE_bool(TEST_vector_index_skip_manifest_update_during_shutdown);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_int32(vector_index_files_number_compaction_trigger);
DECLARE_int32(vector_index_compaction_size_amp_max_percent);
DECLARE_int32(vector_index_compaction_size_ratio_percent);
DECLARE_int32(vector_index_compaction_size_ratio_min_merge_width);
DECLARE_uint64(TEST_vector_index_delay_saving_first_chunk_ms);
DECLARE_uint64(vector_index_compaction_always_include_size_threshold);

METRIC_DEFINE_entity(table);

namespace yb::vector_index {

extern MonoDelta TEST_sleep_on_merged_chunk_populated;

}

namespace yb::ann_methods {

using vector_index::VectorId;

using FloatVectorLSM = vector_index::VectorLSM<std::vector<float>, float>;
using InsertEntries = typename FloatVectorLSM::InsertEntries;
using MergeFilter = vector_index::VectorLSMMergeFilter;
using MergeFilterPtr = vector_index::VectorLSMMergeFilterPtr;

using TestUsearchIndexFactory = vector_index::MakeVectorIndexFactory<
    SimplifiedUsearchIndexFactory, FloatVectorLSM>;
using TestHnswlibIndexFactory = vector_index::MakeVectorIndexFactory<
    HnswlibIndexFactory, FloatVectorLSM>;

class SimpleVectorLSMKeyValueStorage {
 public:
  SimpleVectorLSMKeyValueStorage() = default;

  void StoreVector(const VectorId& vector_id, size_t index) {
    storage_.emplace(vector_id, index);
  }

  size_t GetVectorIndex(VectorId vector_id) {
    auto it = storage_.find(vector_id);
    CHECK(it != storage_.end());
    return it->second;
  }

 private:
  std::unordered_map<VectorId, size_t> storage_;
};

class TestFrontier : public storage::UserFrontier {
 public:
  std::unique_ptr<UserFrontier> Clone() const override {
    return std::make_unique<TestFrontier>(*this);
  }

  std::string ToString() const override {
    return YB_CLASS_TO_STRING(vertex_id);
  }

  void ToPB(google::protobuf::Any* any) const override {
    vector_index::VectorLSMTestFrontierPB pb;
    pb.set_vertex_id(vertex_id_.data(), vertex_id_.size());
    any->PackFrom(pb);
  }

  bool Equals(const UserFrontier& pre_rhs) const override {
    const auto& lhs = *this;
    const auto& rhs = down_cast<const TestFrontier&>(pre_rhs);
    return YB_CLASS_EQUALS(vertex_id);
  }

  void Update(const UserFrontier& pre_rhs, storage::UpdateUserValueType update_type) override {
    const auto& rhs = down_cast<const TestFrontier&>(pre_rhs);
    switch (update_type) {
      case storage::UpdateUserValueType::kLargest:
        vertex_id_ = std::max(vertex_id_, rhs.vertex_id_);
        return;
      case storage::UpdateUserValueType::kSmallest:
        vertex_id_ = std::min(vertex_id_, rhs.vertex_id_);
        return;
    }
    FATAL_INVALID_ENUM_VALUE(storage::UpdateUserValueType, update_type);
  }

  bool IsUpdateValid(const UserFrontier& rhs, storage::UpdateUserValueType type) const override {
    return true;
  }

  Slice FilterAsSlice() override {
    return Slice();
  }

  void ResetFilter() override {
  }

  void FromOpIdPBDeprecated(const OpIdPB& op_id) override {
  }

  Status FromPB(const google::protobuf::Any& any) override {
    vector_index::VectorLSMTestFrontierPB pb;
    if (!any.UnpackTo(&pb)) {
      return STATUS_FORMAT(Corruption, "Unpack test frontier failed");
    }

    vertex_id_ = VERIFY_RESULT(vector_index::FullyDecodeVectorId(pb.vertex_id()));
    return Status::OK();
  }

  uint64_t GetHybridTimeAsUInt64() const override {
    return 0;
  }

  VectorId vertex_id() const {
    return vertex_id_;
  }

  void SetVertexId(VectorId vertex_id) {
    vertex_id_ = vertex_id;
  }

 private:
  VectorId vertex_id_;
};

using TestFrontiers = storage::UserFrontiersBase<TestFrontier>;

class VectorLSMTest : public YBTest, public testing::WithParamInterface<ANNMethodKind> {
 protected:
  // Usearch creates an index with min capacity of 64 vectors.
  constexpr static size_t kDefaultChunkSize = 64;

  VectorLSMTest()
      : thread_pool_(rpc::ThreadPoolOptions {
          .name = "Insert Thread Pool",
          .max_workers = 10,
        }),
        priority_thread_pool_(/* max_running_tasks = */ 2) {
    FLAGS_TEST_usearch_exact = true;
  }

  void SetUp() override {
    FLAGS_vector_index_enable_compactions = true;
    YBTest::SetUp();
  }

  Status InitVectorLSM(FloatVectorLSM& lsm, size_t dimensions, size_t vectors_per_chunk);

  Status OpenVectorLSM(FloatVectorLSM& lsm, size_t dimensions, size_t vectors_per_chunk);

  Status InsertCube(
      FloatVectorLSM& lsm, size_t dimensions,
      size_t block_size = std::numeric_limits<size_t>::max(),
      size_t min_entry_idx = 0);

  Status InsertRandom(
      FloatVectorLSM& lsm, size_t dimensions, size_t num_entries,
      size_t block_size = std::numeric_limits<size_t>::max());

  Status InsertRandomAndFlush(
      FloatVectorLSM& lsm, size_t dimensions, size_t num_entries,
      size_t block_size = std::numeric_limits<size_t>::max());

  Status WaitForBackgroundInsertsDone(
      const FloatVectorLSM& lsm, MonoDelta timeout = MonoDelta::FromSeconds(20));

  Status WaitForCompactionsDone(
      const FloatVectorLSM& lsm, MonoDelta timeout = MonoDelta::FromSeconds(20));

  void VerifyVectorLSM(FloatVectorLSM& lsm, size_t dimensions);

  void CheckQueryVector(
      FloatVectorLSM& lsm, const FloatVectorLSM::Vector& query_vector, size_t max_num_results);

  Result<std::vector<std::string>> GetFiles(FloatVectorLSM& lsm);

  void TestBootstrap(bool flush);

  void TestBackgroundCompactionSizeRatio(bool test_metrics);

  MergeFilterPtr GetMergeFilter();

  void SetMergeFilter(MergeFilterPtr&& filter);

  void SetMergeFilter(storage::FilterDecision decision) {
    SetMergeFilter(CreateDummyMergeFilter(decision));
  }

  template <typename FilterImpl>
  struct FilterProxy : public MergeFilter {
    FilterImpl filter;
    explicit FilterProxy(FilterImpl&& impl) : filter(std::move(impl)) {}
    storage::FilterDecision Filter(VectorId vector_id) override {
      return filter(vector_id);
    }
  };

  static MergeFilterPtr CreateDummyMergeFilter(storage::FilterDecision decision) {
    auto filter = [decision](VectorId vector_id) {
      VLOG(1) << "DummyMergeFilter: " << vector_id << " => " << decision;
      return decision;
    };
    return std::make_unique<FilterProxy<decltype(filter)>>(std::move(filter));
  }

  rpc::ThreadPool thread_pool_;
  PriorityThreadPool priority_thread_pool_;
  SimpleVectorLSMKeyValueStorage key_value_storage_;
  InsertEntries inserted_entries_;
  simple_spinlock merge_filter_mutex_;
  MergeFilterPtr merge_filter_;

  std::unique_ptr<MetricRegistry> metric_registry_ = std::make_unique<MetricRegistry>();
  MetricEntityPtr vector_index_metric_entity_ =
      METRIC_ENTITY_table.Instantiate(metric_registry_.get(), "test");
};

auto GetVectorIndexFactory(ANNMethodKind ann_method) {
  switch (ann_method) {
    case ANNMethodKind::kUsearch:
      return TestUsearchIndexFactory::Create;
    case ANNMethodKind::kHnswlib:
      return TestHnswlibIndexFactory::Create;
  }
  return decltype(&TestUsearchIndexFactory::Create)(nullptr);
}

constexpr static size_t GetNumEntriesByDimensions(size_t dimensions) {
  return 1ULL << dimensions;
}

static size_t GetNumDimensionsByEntries(size_t num_entries) {
  return std::ceil(std::log2(num_entries));
}

FloatVectorLSM::InsertEntries CubeInsertEntries(size_t dimensions) {
  FloatVectorLSM::InsertEntries result;
  for (size_t i = 1; i <= GetNumEntriesByDimensions(dimensions); ++i) {
    auto bits = i - 1;
    FloatVector vector(dimensions);
    for (size_t d = 0; d != dimensions; ++d) {
      vector[d] = 1.f * ((bits >> d) & 1);
    }
    result.emplace_back(FloatVectorLSM::InsertEntry {
      .vector_id = VectorId::GenerateRandom(),
      .vector = std::move(vector),
    });
  }
  return result;
}

FloatVectorLSM::InsertEntries RandomEntries(size_t dimensions, size_t num_entries) {
  static std::uniform_real_distribution<> distribution;

  FloatVectorLSM::InsertEntries result;
  while (num_entries-- > 0) {
    result.emplace_back(FloatVectorLSM::InsertEntry {
      .vector_id = VectorId::GenerateRandom(),
      .vector = RandomFloatVector(dimensions, distribution),
    });
  }
  return result;
}

auto GenerateVectorIds(size_t num) {
  std::vector<VectorId> result;
  result.reserve(num);
  while (result.size() < num) {
    result.emplace_back(VectorId::GenerateRandom());
  }
  return result;
}

Status VectorLSMTest::InsertCube(
    FloatVectorLSM& lsm, size_t dimensions, size_t block_size,
    size_t min_entry_idx) {
  inserted_entries_ = CubeInsertEntries(dimensions);
  size_t num_inserts = 0;
  for (size_t i = 0; i < inserted_entries_.size(); i += block_size) {
    auto begin = inserted_entries_.begin() + i;
    auto end = inserted_entries_.begin() + std::min(i + block_size, inserted_entries_.size());
    if (i < min_entry_idx) {
      ptrdiff_t delta = min_entry_idx - i;
      if (delta >= end - begin) {
        continue;
      }
      begin += delta;
    }
    FloatVectorLSM::InsertEntries block_entries(begin, end);
    TestFrontiers frontiers;
    frontiers.Smallest().SetVertexId(block_entries.front().vector_id);
    frontiers.Largest().SetVertexId(block_entries.front().vector_id);
    for (; begin != end; ++begin) {
      key_value_storage_.StoreVector(
          begin->vector_id, begin - inserted_entries_.begin() + 1);
    }
    RETURN_NOT_OK(lsm.Insert(block_entries, { .frontiers = &frontiers }));
    ++num_inserts;
  }
  LOG(INFO) << "Inserted " << inserted_entries_.size() << " entries "
            << "via " << num_inserts << " batches";
  return Status::OK();
}

Status VectorLSMTest::InsertRandom(
    FloatVectorLSM& lsm, size_t dimensions, size_t num_entries, size_t batch_size) {
  inserted_entries_ = RandomEntries(dimensions, num_entries);
  size_t num_inserts = 0;
  for (size_t i = 0; i < inserted_entries_.size(); i += batch_size) {
    auto begin = inserted_entries_.begin() + i;
    auto end = inserted_entries_.begin() + std::min(i + batch_size, inserted_entries_.size());
    FloatVectorLSM::InsertEntries block_entries(begin, end);
    TestFrontiers frontiers;
    frontiers.Smallest().SetVertexId(block_entries.front().vector_id);
    frontiers.Largest().SetVertexId(block_entries.front().vector_id);
    for (; begin != end; ++begin) {
      key_value_storage_.StoreVector(
          begin->vector_id, begin - inserted_entries_.begin() + 1);
    }
    RETURN_NOT_OK(lsm.Insert(block_entries, { .frontiers = &frontiers }));
    ++num_inserts;
  }
  LOG(INFO) << "Inserted " << inserted_entries_.size() << " entries "
            << "via " << num_inserts << " batches";
  return Status::OK();
}

Status VectorLSMTest::InsertRandomAndFlush(
    FloatVectorLSM& lsm, size_t dimensions, size_t num_entries, size_t batch_size) {
  RETURN_NOT_OK(InsertRandom(lsm, dimensions, num_entries, batch_size));
  RETURN_NOT_OK(WaitForBackgroundInsertsDone(lsm));
  return lsm.Flush(/* wait = */ true);
}

Status VectorLSMTest::WaitForBackgroundInsertsDone(const FloatVectorLSM& lsm, MonoDelta timeout) {
  return LoggedWaitFor(
      [&lsm] { return !lsm.TEST_HasBackgroundInserts(); }, timeout,
      "Background inserts done", MonoDelta::FromMilliseconds(100), /* delay_multiplier = */ 1.0);
}

Status VectorLSMTest::WaitForCompactionsDone(const FloatVectorLSM& lsm, MonoDelta timeout) {
  return LoggedWaitFor(
      [&lsm] { return !lsm.TEST_HasCompactions(); }, timeout,
      "Compactions done", MonoDelta::FromMilliseconds(100), /* delay_multiplier = */ 1.0);
}

Status VectorLSMTest::OpenVectorLSM(
    FloatVectorLSM& lsm, size_t dimensions, size_t vectors_per_chunk) {

  std::string test_dir;
  RETURN_NOT_OK(Env::Default()->GetTestDirectory(&test_dir));
  test_dir = JoinPathSegments(test_dir, "vector_lsm_test_" + Uuid::Generate().ToString());

  auto factory = GetVectorIndexFactory(GetParam());
  FloatVectorLSM::Options options = {
    .log_prefix = "Test: ",
    .storage_dir = JoinPathSegments(test_dir, "vector_lsm"),
    .vector_index_factory = [factory, dimensions](vector_index::FactoryMode mode) {
      vector_index::HNSWOptions hnsw_options = {
        .dimensions = dimensions,
      };
      return factory(mode, hnsw_options);
    },
    .vectors_per_chunk = vectors_per_chunk,
    .thread_pool = &thread_pool_,
    .insert_thread_pool = &thread_pool_,
    .compaction_token = std::make_shared<PriorityThreadPoolToken>(priority_thread_pool_),
    .frontiers_factory = [] { return std::make_unique<TestFrontiers>(); },
    .vector_merge_filter_factory = [this] { return GetMergeFilter(); },
    .file_extension = "",
    .metric_entity = vector_index_metric_entity_,
  };
  auto status = lsm.Open(std::move(options));
  if (status.ok()) {
    lsm.EnableAutoCompactions();
  }
  return status;
}

Status VectorLSMTest::InitVectorLSM(
    FloatVectorLSM& lsm, size_t dimensions, size_t vectors_per_chunk) {
  RETURN_NOT_OK(OpenVectorLSM(lsm, dimensions, vectors_per_chunk));
  return InsertCube(lsm, dimensions, vectors_per_chunk);
}

void VectorLSMTest::VerifyVectorLSM(FloatVectorLSM& lsm, size_t dimensions) {
  CheckQueryVector(lsm, FloatVectorLSM::Vector(dimensions, 0.f), dimensions + 1);
  CheckQueryVector(lsm, FloatVectorLSM::Vector(dimensions, 1.f), dimensions + 1);
}

void VectorLSMTest::CheckQueryVector(
    FloatVectorLSM& lsm, const FloatVectorLSM::Vector& query_vector, size_t max_num_results) {
  bool stop = false;

  FloatVectorLSM::SearchResults expected_results;
  for (const auto& entry : inserted_entries_) {
    expected_results.emplace_back(entry.vector_id, lsm.Distance(query_vector, entry.vector));
  }
  auto less_condition = [](const auto& lhs, const auto& rhs) {
    return lhs.distance == rhs.distance ? lhs.vector_id < rhs.vector_id
                                        : lhs.distance < rhs.distance;
  };
  std::sort(expected_results.begin(), expected_results.end(), less_condition);

  expected_results.resize(std::min(expected_results.size(), max_num_results));

  while (!stop) {
    stop = !lsm.TEST_HasBackgroundInserts();

    vector_index::SearchOptions options = {
      .max_num_results = max_num_results,
      .ef = 0,
    };
    auto search_result = ASSERT_RESULT(lsm.Search(query_vector, options));
    VLOG(1) << "Search result: " << AsString(search_result);

    ASSERT_EQ(search_result.size(), expected_results.size());

    std::sort(search_result.begin(), search_result.end(), less_condition);

    for (size_t i = 0; i != expected_results.size(); ++i) {
      ASSERT_EQ(search_result[i].distance, expected_results[i].distance);
      ASSERT_EQ(search_result[i].vector_id, expected_results[i].vector_id);
    }
  }
}

Result<std::vector<std::string>> VectorLSMTest::GetFiles(FloatVectorLSM& lsm) {
  return path_utils::GetVectorIndexFiles(*lsm.TEST_GetEnv(), lsm.StorageDir());
}

MergeFilterPtr VectorLSMTest::GetMergeFilter() {
  if (!merge_filter_) {
    SetMergeFilter(storage::FilterDecision::kKeep);
  }
  auto filter = [this](VectorId vector_id) {
    std::lock_guard lock(merge_filter_mutex_);
    return merge_filter_->Filter(vector_id);
  };
  return std::make_unique<FilterProxy<decltype(filter)>>(std::move(filter));
}

void VectorLSMTest::SetMergeFilter(MergeFilterPtr&& filter) {
  std::lock_guard lock(merge_filter_mutex_);
  merge_filter_ = std::move(filter);
}

TEST_P(VectorLSMTest, Simple) {
  constexpr size_t kDimensions = 4;

  FloatVectorLSM lsm;
  ASSERT_OK(InitVectorLSM(lsm, kDimensions, 1000));

  VerifyVectorLSM(lsm, kDimensions);
}

TEST_P(VectorLSMTest, MultipleChunks) {
  constexpr size_t kDimensions = 9;
  constexpr size_t kChunkSize  = 2 * kDefaultChunkSize;
  static_assert(GetNumEntriesByDimensions(kDimensions) > 2 * kChunkSize);

  FloatVectorLSM lsm;
  ASSERT_OK(InitVectorLSM(lsm, kDimensions, kChunkSize));
  ASSERT_GT(lsm.NumImmutableChunks(), 1);

  VerifyVectorLSM(lsm, kDimensions);
}

TEST_P(VectorLSMTest, SingleChunkSimpleCompaction) {
  constexpr size_t kDimensions = 4;
  constexpr size_t kNumEntries = GetNumEntriesByDimensions(kDimensions);

  // Turn off background compactions to not interfere with manual compaction.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  FloatVectorLSM lsm;
  ASSERT_OK(OpenVectorLSM(lsm, kDimensions, 2 * kNumEntries));
  ASSERT_EQ(0, lsm.TEST_NextManifestFileNo());

  // Empty compaction, nothing is compacted.
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(0, lsm.TEST_NextManifestFileNo());

  ASSERT_OK(InsertCube(lsm, kDimensions, 2 * kNumEntries));
  ASSERT_EQ(kNumEntries, inserted_entries_.size());
  ASSERT_OK(WaitForBackgroundInsertsDone(lsm));
  ASSERT_EQ(0, lsm.NumImmutableChunks());

  ASSERT_OK(lsm.Flush(/* wait = */ true));
  ASSERT_EQ(1, lsm.NumImmutableChunks());
  ASSERT_EQ(1, lsm.TEST_NextManifestFileNo());
  VerifyVectorLSM(lsm, kDimensions);

  // Compact single file into a single file.
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(1, lsm.NumImmutableChunks());
  ASSERT_EQ(2, lsm.TEST_NextManifestFileNo());
  VerifyVectorLSM(lsm, kDimensions);

  // Compact single file into a single file again.
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(1, lsm.NumImmutableChunks());
  ASSERT_EQ(3, lsm.TEST_NextManifestFileNo());
  VerifyVectorLSM(lsm, kDimensions);

  // Wait for cleanup is completed and check files on disk.
  while (lsm.TEST_ObsoleteFilesCleanupInProgress()) {
    SleepFor(MonoDelta::FromSeconds(1));
  }
  auto files = AsString(ASSERT_RESULT(GetFiles(lsm)));
  ASSERT_STR_EQ(files, "[0.meta, 1.meta, 2.meta, vectorindex_3]");
}

TEST_P(VectorLSMTest, MultipleChunksSimpleCompaction) {
  constexpr size_t kDimensions = 8;
  constexpr size_t kNumEntries = GetNumEntriesByDimensions(kDimensions);
  static_assert(kNumEntries > 2 * kDefaultChunkSize);

  // Turn off background compactions to not interfere with manual compaction.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  constexpr size_t kBlocksPerChunk = 5;
  constexpr size_t kBlockSize = kDefaultChunkSize / kBlocksPerChunk;
  constexpr size_t kNumInserts = (kNumEntries + kBlockSize - 1) / kBlockSize;
  constexpr size_t kExpectedNumChunks = (kNumInserts + kBlocksPerChunk - 1) / kBlocksPerChunk;

  FloatVectorLSM lsm;
  ASSERT_OK(OpenVectorLSM(lsm, kDimensions, kDefaultChunkSize));
  ASSERT_EQ(0, lsm.TEST_NextManifestFileNo());
  ASSERT_OK(InsertCube(lsm, kDimensions, kBlockSize));
  ASSERT_EQ(kNumEntries, inserted_entries_.size());
  ASSERT_OK(WaitForBackgroundInsertsDone(lsm));
  ASSERT_EQ(kExpectedNumChunks - 1, lsm.NumImmutableChunks());
  VerifyVectorLSM(lsm, kDimensions);

  ASSERT_OK(lsm.Flush(/* wait = */ true));
  ASSERT_EQ(kExpectedNumChunks, lsm.NumImmutableChunks());
  ASSERT_EQ(1, lsm.TEST_NextManifestFileNo());
  VerifyVectorLSM(lsm, kDimensions);

  // Compact all files into a single file.
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(1, lsm.NumImmutableChunks());
  ASSERT_EQ(2, lsm.TEST_NextManifestFileNo());
  VerifyVectorLSM(lsm, kDimensions);

  // Wait for cleanup is completed and check files on disk.
  while (lsm.TEST_ObsoleteFilesCleanupInProgress()) {
    SleepFor(MonoDelta::FromSeconds(1));
  }
  auto compacted_idx = kExpectedNumChunks + 1;
  auto files = AsString(ASSERT_RESULT(GetFiles(lsm)));
  ASSERT_STR_EQ(files, Format("[0.meta, 1.meta, vectorindex_$0]", compacted_idx));

  // Compact again.
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(1, lsm.NumImmutableChunks());
  ASSERT_EQ(3, lsm.TEST_NextManifestFileNo());
  VerifyVectorLSM(lsm, kDimensions);

  // Wait for cleanup is completed and check files on disk.
  while (lsm.TEST_ObsoleteFilesCleanupInProgress()) {
    SleepFor(MonoDelta::FromSeconds(1));
  }
  ++compacted_idx;
  files = AsString(ASSERT_RESULT(GetFiles(lsm)));
  ASSERT_STR_EQ(files, Format("[0.meta, 1.meta, 2.meta, vectorindex_$0]", compacted_idx));
}

// The purpose of this test is to make sure compaction works fine if all chunks got
// filtered out during the compaction, https://github.com/yugabyte/yugabyte-db/issues/29016.
TEST_P(VectorLSMTest, AllVectorsRemovalCompaction) {
  constexpr size_t kDimensions = 4;
  constexpr size_t kNumEntries = GetNumEntriesByDimensions(kDimensions);

  // Turn off background compactions to not interfere with manual compaction.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  // Discard all vectors on compaction.
  SetMergeFilter(storage::FilterDecision::kDiscard);

  FloatVectorLSM lsm;
  ASSERT_OK(OpenVectorLSM(lsm, kDimensions, 2 * kNumEntries));
  ASSERT_OK(InsertCube(lsm, kDimensions, 2 * kNumEntries));
  ASSERT_EQ(kNumEntries, inserted_entries_.size());
  ASSERT_OK(WaitForBackgroundInsertsDone(lsm));
  ASSERT_OK(lsm.Flush(/* wait = */ true));
  ASSERT_EQ(1, lsm.TEST_NextManifestFileNo());
  VerifyVectorLSM(lsm, kDimensions);

  // Compact single file into a single file.
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(1, lsm.NumImmutableChunks());
  ASSERT_EQ(2, lsm.TEST_NextManifestFileNo());

  // Wait for cleanup is completed and check files on disk.
  while (lsm.TEST_ObsoleteFilesCleanupInProgress()) {
    SleepFor(MonoDelta::FromSeconds(1));
  }
  auto files = AsString(ASSERT_RESULT(GetFiles(lsm)));
  ASSERT_STR_EQ(files, "[0.meta, 1.meta]");

  // Verify results.
  inserted_entries_.clear();
  VerifyVectorLSM(lsm, kDimensions);

  // Make sure further writes work fine.
  SetMergeFilter(storage::FilterDecision::kKeep);
  ASSERT_OK(InsertCube(lsm, kDimensions, 2 * kNumEntries));
  ASSERT_EQ(kNumEntries, inserted_entries_.size());
  ASSERT_OK(WaitForBackgroundInsertsDone(lsm));
  ASSERT_OK(lsm.Flush(/* wait = */ true));
  ASSERT_EQ(2, lsm.TEST_NextManifestFileNo());
  VerifyVectorLSM(lsm, kDimensions);
  files = AsString(ASSERT_RESULT(GetFiles(lsm)));
  ASSERT_STR_EQ(files, "[0.meta, 1.meta, vectorindex_2]");

  // Make sure further compaction works fine.
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(1, lsm.NumImmutableChunks());
  ASSERT_EQ(3, lsm.TEST_NextManifestFileNo());
  VerifyVectorLSM(lsm, kDimensions);

  // Wait for cleanup is completed and check files on disk.
  while (lsm.TEST_ObsoleteFilesCleanupInProgress()) {
    SleepFor(MonoDelta::FromSeconds(1));
  }
  files = AsString(ASSERT_RESULT(GetFiles(lsm)));
  ASSERT_STR_EQ(files, "[0.meta, 1.meta, 2.meta, vectorindex_3]");
}

TEST_P(VectorLSMTest, BackgroundCompactionSizeAmp) {
  constexpr size_t kDimensions = 8;
  constexpr size_t kNumChunks  = 6;

  // Make sure background compaction are turned on.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = true;

  // Turn off compactions by size ratio to not interfere with compactions by size amp.
  FLAGS_vector_index_compaction_size_ratio_percent = -100;

  // Ensure background compaction flags.
  FLAGS_vector_index_files_number_compaction_trigger = narrow_cast<int32_t>(kNumChunks / 2);
  FLAGS_vector_index_compaction_size_amp_max_percent = narrow_cast<int32_t>((kNumChunks - 1) * 100);

  FloatVectorLSM lsm;
  ASSERT_OK(OpenVectorLSM(lsm, kDimensions, kDefaultChunkSize));

  for (size_t n = 0; n < kNumChunks; ++n) {
    // Check files right before the background compaction would trigger.
    if (n == kNumChunks - 1) {
      std::stringstream expected_files;
      expected_files << "0.meta";
      for (size_t i = 1; i <= n; ++i) { expected_files << ", vectorindex_" << i; }
      const auto files = AsString(ASSERT_RESULT(GetFiles(lsm)));
      ASSERT_STR_EQ(files, Format("[$0]", expected_files.str()));
    }

    ASSERT_OK(InsertRandomAndFlush(lsm, kDimensions, 10));
  }

  ASSERT_OK(WaitForCompactionsDone(lsm));

  auto last_chunk_id = kNumChunks + 1;
  auto files = AsString(ASSERT_RESULT(GetFiles(lsm)));
  ASSERT_STR_EQ(files, Format("[0.meta, vectorindex_$0]", last_chunk_id));

  // Wait for cleanup is completed and check files on disk.
  while (lsm.TEST_ObsoleteFilesCleanupInProgress()) {
    SleepFor(MonoDelta::FromSeconds(1));
  }

  // Trigger background compaction on the same size. At this point there's one big chunk which
  // is approximately equal to the size of six random chunks. So, inserting six more chunks
  // should trigger next background compaction.
  FLAGS_vector_index_compaction_size_amp_max_percent = 100;
  for (size_t n = 0; n < kNumChunks; ++n) {
    // Check files right before the background compaction would trigger.
    if (n == kNumChunks - 1) {
      std::stringstream expected_files;
      expected_files << "0.meta, " << "vectorindex_" << last_chunk_id;
      for (size_t i = 1; i <= n; ++i) { expected_files << ", vectorindex_" << last_chunk_id + i; }
      const auto files = AsString(ASSERT_RESULT(GetFiles(lsm)));
      ASSERT_STR_EQ(files, Format("[$0]", expected_files.str()));
    }

    ASSERT_OK(InsertRandomAndFlush(lsm, kDimensions, 10));
  }

  ASSERT_OK(WaitForCompactionsDone(lsm));

  last_chunk_id += kNumChunks + 1;
  files = AsString(ASSERT_RESULT(GetFiles(lsm)));
  ASSERT_STR_EQ(files, Format("[0.meta, vectorindex_$0]", last_chunk_id));

  // Wait for cleanup is completed and check files on disk.
  while (lsm.TEST_ObsoleteFilesCleanupInProgress()) {
    SleepFor(MonoDelta::FromSeconds(1));
  }
}

void VectorLSMTest::TestBackgroundCompactionSizeRatio(bool test_metrics) {
  constexpr size_t kDimensions = 8;
  constexpr size_t kNumLargeChunks = 2;
  constexpr size_t kNumSmallChunks = 6;
  constexpr size_t kNumMinChunks = 2;
  constexpr size_t kNumChunks = kNumLargeChunks + kNumSmallChunks + kNumMinChunks;
  constexpr size_t kMinChunkNumVectors = 1;
  constexpr size_t kSmallChunkNumVectors = 9 * kMinChunkNumVectors;
  constexpr size_t kMediumChunkNumVectors = 8 * kSmallChunkNumVectors;
  constexpr size_t kLargeChunkNumVectors = 4 * kMediumChunkNumVectors;
  constexpr size_t kChunkSize = 1 + kLargeChunkNumVectors; // To trigger explicit flush.

  // Turn background compaction off to prepare files.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  // Turn off compactions by size amp to not interfere with compactions by size ratio.
  FLAGS_vector_index_compaction_size_amp_max_percent = -1;

  // Ensure background compaction flags.
  FLAGS_vector_index_compaction_always_include_size_threshold = 0;
  FLAGS_vector_index_files_number_compaction_trigger = narrow_cast<int32_t>(kNumChunks / 2);
  FLAGS_vector_index_compaction_size_ratio_min_merge_width = kNumMinChunks + 1;

  // Round up to the nearest tens (e.g. 1.33 => 40%).
  FLAGS_vector_index_compaction_size_ratio_percent = -100 + 10 * static_cast<int>(
      std::ceil((10.0 * kMediumChunkNumVectors) / (kNumSmallChunks * kSmallChunkNumVectors)));

  FloatVectorLSM lsm;
  ASSERT_OK(OpenVectorLSM(lsm, kDimensions, kChunkSize));

  // Keep chunks initial tree.
  std::vector<size_t> num_vectors_by_file;
  num_vectors_by_file.reserve(kNumChunks);
  for (size_t i = 0; i < kNumChunks; ++i) {
    num_vectors_by_file.push_back(
      i == 0 ? kLargeChunkNumVectors :
          i == 1 ? kMediumChunkNumVectors :
              i < (kNumChunks - kNumMinChunks) ? kSmallChunkNumVectors : kMinChunkNumVectors);
  }

  // Write all chunks except the last one to use it as a compaction trigger.
  std::stringstream expected_files;
  expected_files << "0.meta";
  for (size_t i = 1; i < num_vectors_by_file.size(); ++i) {
    ASSERT_OK(InsertRandomAndFlush(lsm, kDimensions, num_vectors_by_file[i - 1]));
    expected_files << ", vectorindex_" << i;
  }

  // Make sure chunks are expected.
  auto files = AsString(ASSERT_RESULT(GetFiles(lsm)));
  ASSERT_STR_EQ(files, Format("[$0]", expected_files.str()));

  if (test_metrics) {
    ASSERT_EQ(lsm.metrics().compact_write_bytes->value(), 0);
    ASSERT_EQ(lsm.metrics().compact_read_bytes->value(), 0);
  }

  // Insert the last min chunk to trigger background compaction.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = true;
  ASSERT_OK(InsertRandomAndFlush(lsm, kDimensions, num_vectors_by_file[kNumChunks - 1]));
  ASSERT_OK(WaitForCompactionsDone(lsm));

  // Background compaction won't consider min chunks because min merge width is greater than the
  // number of min chunks. And the most earliest chunk doesn't meet the criteria of size ratio
  // as it is too large. So, it is expected to end up with 1 large chunk, 2 min chunks and 1 new
  // compacted chunk.
  if (test_metrics) {
    // The write metric should be incremented by the size of the new chunk created by compaction.
    ASSERT_EQ(lsm.metrics().compact_write_bytes->value(), lsm.TEST_LatestChunkSize());
    ASSERT_GT(lsm.metrics().compact_read_bytes->value(), 0);
  } else {
    // Check expected files after the compaction.
    expected_files.str({});
    expected_files << "0.meta, vectorindex_1";
    for (size_t n = kNumChunks - kNumMinChunks + 1; n <= kNumChunks + 1; ++n) {
      expected_files << ", vectorindex_" << n;
    }
    files = AsString(ASSERT_RESULT(GetFiles(lsm)));
    ASSERT_STR_EQ(files, Format("[$0]", expected_files.str()));

    // Wait for cleanup is completed and check files on disk.
    while (lsm.TEST_ObsoleteFilesCleanupInProgress()) {
      SleepFor(MonoDelta::FromSeconds(1));
    }
  }
}

TEST_P(VectorLSMTest, BackgroundCompactionSizeRatio) {
  TestBackgroundCompactionSizeRatio(/* test_metrics= */ false);
}

// Verify metrics for background compaction leaving some chunks uncompacted.
TEST_P(VectorLSMTest, BackgroundCompactionSizeRatioMetrics) {
  TestBackgroundCompactionSizeRatio(/* test_metrics= */ true);
}

TEST_P(VectorLSMTest, CompactionCancelOnShutdown) {
  constexpr size_t kDimensions = 9;
  constexpr size_t kChunkSize  = 2 * kDefaultChunkSize;
  static_assert(GetNumEntriesByDimensions(kDimensions) > 2 * kChunkSize);

  FloatVectorLSM lsm;
  ASSERT_OK(InitVectorLSM(lsm, kDimensions, kChunkSize));
  ASSERT_OK(WaitForBackgroundInsertsDone(lsm));
  ASSERT_GT(lsm.NumImmutableChunks(), 1);

  // Pause Vector LSM compactions.
  const auto merge_timeout = 5s * kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(vector_index::TEST_sleep_on_merged_chunk_populated) = merge_timeout;

  // Spawn a separate thread and start shutting down
  ThreadHolder threads;
  threads.AddThreadFunctor([&lsm, merge_timeout] {
    SleepFor(merge_timeout / 2);

    // Compaction should be already started by this time (and paused).
    lsm.StartShutdown();
  });

  auto status = lsm.Compact(/* wait = */ true);
  ASSERT_TRUE(status.IsShutdownInProgress()) << status;
}

// Verify metrics for manual compaction of empty, single and multiple chunk/s.
TEST_P(VectorLSMTest, SimpleCompactionMetrics) {
  constexpr size_t kDimensions = 8;
  constexpr size_t kNumEntriesPerChunk = 32;
  static_assert(kNumEntriesPerChunk <= kDefaultChunkSize);

  // Turn off background compactions to not interfere with manual compaction.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  FloatVectorLSM lsm;
  ASSERT_OK(OpenVectorLSM(lsm, kDimensions, kDefaultChunkSize));
  ASSERT_EQ(0, lsm.TEST_NextManifestFileNo());
  ASSERT_EQ(lsm.metrics().compact_write_bytes->value(), 0);
  ASSERT_EQ(lsm.metrics().compact_read_bytes->value(), 0);

  // Empty compaction, write metric remains unchanged.
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(lsm.metrics().compact_write_bytes->value(), 0);
  ASSERT_EQ(lsm.metrics().compact_read_bytes->value(), 0);

  // Insert 1 batch of entries to create 1 chunk file.
  ASSERT_OK(InsertRandomAndFlush(lsm, kDimensions, kNumEntriesPerChunk));
  ASSERT_EQ(kNumEntriesPerChunk, inserted_entries_.size());
  ASSERT_EQ(1, lsm.NumImmutableChunks());

  // Compact single file into a single file, write metric increases by size of new chunk file.
  // The single input chunk is read during compaction.
  auto compaction_reads = lsm.TEST_LatestChunkSize();
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(1, lsm.NumImmutableChunks());
  auto compaction_writes = lsm.TEST_LatestChunkSize();
  ASSERT_EQ(lsm.metrics().compact_write_bytes->value(), compaction_writes);
  ASSERT_EQ(lsm.metrics().compact_read_bytes->value(), compaction_reads);

  // Insert 5 more batches for a total of 6 chunk files.
  // Track input sizes: the compacted chunk from the 1st compaction is now an input to the 2nd.
  constexpr size_t kNumChunks = 6;
  compaction_reads += compaction_writes;
  for (size_t i = 1; i < kNumChunks; ++i) {
    ASSERT_OK(InsertRandomAndFlush(lsm, kDimensions, kNumEntriesPerChunk));
    ASSERT_EQ(kNumEntriesPerChunk, inserted_entries_.size());
    compaction_reads += lsm.TEST_LatestChunkSize();
  }
  ASSERT_EQ(kNumChunks, lsm.NumImmutableChunks());

  // Compact all files into a single file.
  ASSERT_OK(lsm.Compact(/* wait = */ true));
  ASSERT_EQ(1, lsm.NumImmutableChunks());
  compaction_writes += lsm.TEST_LatestChunkSize();
  ASSERT_EQ(lsm.metrics().compact_write_bytes->value(), compaction_writes);
  ASSERT_EQ(lsm.metrics().compact_read_bytes->value(), compaction_reads);
}

void VectorLSMTest::TestBootstrap(bool flush) {
  constexpr size_t kChunkSize  = kDefaultChunkSize;
  const     size_t kDimensions = GetNumDimensionsByEntries(kChunkSize * 4);

  {
    FloatVectorLSM lsm;
    ASSERT_OK(InitVectorLSM(lsm, kDimensions, kChunkSize));
    if (flush) {
      ASSERT_OK(lsm.Flush(true));
    }
  }

  {
    FloatVectorLSM lsm;
    ASSERT_OK(OpenVectorLSM(lsm, kDimensions, kChunkSize));
    auto frontier_ptr = lsm.GetFlushedFrontier();

    // Find entry idx by frontier's vertex id, inserted on the first step (InitVectorLSM).
    size_t frontier_entry_idx = 0;
    if (frontier_ptr) {
      const auto frontier_vertex_id = down_cast<TestFrontier*>(frontier_ptr.get())->vertex_id();
      for (; frontier_entry_idx < inserted_entries_.size(); ++frontier_entry_idx) {
        if (inserted_entries_[frontier_entry_idx].vector_id == frontier_vertex_id) {
          break;
        }
      }
      ASSERT_LT(frontier_entry_idx, inserted_entries_.size());
    }
    ASSERT_OK(InsertCube(lsm, kDimensions, kChunkSize, frontier_entry_idx));

    VerifyVectorLSM(lsm, kDimensions);
  }
}

TEST_P(VectorLSMTest, Bootstrap) {
  TestBootstrap(/* flush= */ false);
}

TEST_P(VectorLSMTest, BootstrapWithFlush) {
  TestBootstrap(/* flush= */ true);
}

TEST_P(VectorLSMTest, NotSavedChunk) {
  FLAGS_TEST_vector_index_delay_saving_first_chunk_ms = 1000 * kTimeMultiplier;
  FLAGS_TEST_vector_index_skip_manifest_update_during_shutdown = true;
  TestBootstrap(/* flush= */ false);
}

TEST_F(VectorLSMTest, MergeChunkResults) {
  const auto kIds = GenerateVectorIds(7);

  using ChunkResults = std::vector<vector_index::VectorWithDistance<float>>;
  ChunkResults a_src = {{kIds[4], 1}, {kIds[2], 3}, {kIds[0], 5}, {kIds[5], 7}};
  ChunkResults b_src = {{kIds[1], 2}, {kIds[2], 3}, {kIds[3], 4}, {kIds[6], 7}, {kIds[5], 7}};
  for (size_t i = 1; i != a_src.size() + b_src.size(); ++i) {
    auto a = a_src;
    auto b = b_src;
    MergeChunkResults(a, b, i);
    auto sum = a_src;
    sum.insert(sum.end(), b_src.begin(), b_src.end());
    std::sort(sum.begin(), sum.end());
    sum.erase(std::unique(sum.begin(), sum.end()), sum.end());
    sum.resize(std::min(i, sum.size()));
    ASSERT_EQ(a, sum);
  }
}

std::string ANNMethodKindToString(
    const testing::TestParamInfo<ANNMethodKind>& param_info) {
  return AsString(param_info.param);
}

INSTANTIATE_TEST_SUITE_P(
    , VectorLSMTest, ::testing::ValuesIn(kANNMethodKindArray), ANNMethodKindToString);

}  // namespace yb::ann_methods

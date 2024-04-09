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

#pragma GCC diagnostic push

#ifdef __clang__
// For https://gist.githubusercontent.com/mbautin/87278fc41654c6c74cf7232960364c95/raw
#pragma GCC diagnostic ignored "-Wpass-failed"

#if __clang_major__ == 14
// For https://gist.githubusercontent.com/mbautin/7856257553a1d41734b1cec7c73a0fb4/raw
#pragma GCC diagnostic ignored "-Wambiguous-reversed-operator"
#endif
#endif  // __clang__

#include "usearch/index.hpp"
#include "usearch/index_dense.hpp"

#pragma GCC diagnostic pop

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <random>
#include <cassert>

#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

// Helper function to generate random vectors
template<typename Distribution>
std::vector<float> GenerateRandomVector(size_t dimensions, Distribution& dis) {
  std::vector<float> vec(dimensions);
  for (auto& v : vec) {
    v = static_cast<float>(dis(yb::ThreadLocalRandom()));
  }
  return vec;
}

namespace yb {

class UsearchVectorIndexTest : public YBTest {
};

void ReportPerf(
    const char* verb_past, size_t num_items, const char* item_description_plural,
    size_t dimensions, int64_t elapsed_usec, size_t num_threads) {
  LOG(INFO) << verb_past << " " << num_items << " " << item_description_plural << " with "
            << dimensions << " dimensions in " << (elapsed_usec / 1000.0) << " ms "
            << "(" << (elapsed_usec * 1.0 / num_items) << " usec per vector, or "
            << (elapsed_usec * 1.0 / num_items / dimensions) << " usec per coordinate), "
            << "using " << num_threads << " threads";
}

TEST_F(UsearchVectorIndexTest, CreateAndQuery) {
  using namespace unum::usearch;

  // Create a metric and index
  const size_t kDimensions = 96;
  metric_punned_t metric(kDimensions, metric_kind_t::l2sq_k, scalar_kind_t::f32_k);

  // Generate and add vectors to the index
  const size_t kNumVectors = ReleaseVsDebugVsAsanVsTsan(100000, 20000, 15000, 10000);
  const size_t kNumIndexingThreads = 4;

  std::uniform_real_distribution<> uniform_distrib(0, 1);

  std::string index_path;
  {
    TestThreadHolder indexing_thread_holder;
    index_dense_t index = index_dense_t::make(metric);
    index.reserve(kNumVectors);
    auto load_start_time = MonoTime::Now();
    CountDownLatch latch(kNumIndexingThreads);
    std::atomic<size_t> num_vectors_inserted{0};
    for (size_t thread_index = 0; thread_index < kNumIndexingThreads; ++thread_index) {
      indexing_thread_holder.AddThreadFunctor(
          [&num_vectors_inserted, &index, &latch, &uniform_distrib]() {
            std::random_device rd;
            size_t vector_id;
            while ((vector_id = num_vectors_inserted.fetch_add(1)) < kNumVectors) {
              auto vec = GenerateRandomVector(kDimensions, uniform_distrib);
              ASSERT_TRUE(index.add(vector_id, vec.data()));
            }
            latch.CountDown();
          });
    }
    latch.Wait();
    auto load_elapsed_usec = (MonoTime::Now() - load_start_time).ToMicroseconds();
    ReportPerf("Indexed", kNumVectors, "vectors", kDimensions, load_elapsed_usec,
               kNumIndexingThreads);

    // Save the index to a file
    index_path = GetTestDataDirectory() + "/hnsw_index.usearch";
    ASSERT_TRUE(index.save(index_path.c_str()));
  }
  auto file_size = ASSERT_RESULT(Env::Default()->GetFileSize(index_path));
  LOG(INFO) << "Index saved to " << index_path;
  LOG(INFO) << "Index file size: " << file_size << " bytes "
            << "(" << (file_size / 1024.0 / 1024.0) << " MiB), "
            << (file_size * 1.0 / kNumVectors ) << " average bytes per vector, "
            << (file_size * 1.0 / (kNumVectors * kDimensions)) << " average bytes per coordinate";

  // Load the index from the file
  auto index_load_start_time = MonoTime::Now();
  index_dense_t loaded_index = index_dense_t::make(index_path.c_str(), /* load= */ true);
  ASSERT_TRUE(loaded_index); // Ensure loading was successful
  auto index_load_elapsed_usec = (MonoTime::Now() - index_load_start_time).ToMicroseconds();
  LOG(INFO) << "Index loaded from " << index_path << " in " << (index_load_elapsed_usec / 1000.0)
            << " ms";

  auto query_start_time = MonoTime::Now();
  const size_t kNumQueryThreads = 4;
  const size_t kNumQueries = ReleaseVsDebugVsAsanVsTsan(100000, 30000, 40000, 10000);
  const size_t kNumResultsPerQuery = 10;
  CountDownLatch latch(kNumQueryThreads);
  std::atomic<size_t> num_vectors_queried;
  TestThreadHolder query_thread_holder;
  for (size_t thread_index = 0; thread_index < kNumQueryThreads; ++thread_index) {
    query_thread_holder.AddThreadFunctor(
      [&num_vectors_queried, &loaded_index, &latch, &uniform_distrib, kNumResultsPerQuery]() {
        // Perform searches on the loaded index
        while (num_vectors_queried.fetch_add(1) < kNumQueries) {
          auto query_vec = GenerateRandomVector(kDimensions, uniform_distrib);
          auto results = loaded_index.search(query_vec.data(), kNumResultsPerQuery);
          ASSERT_LE(results.size(), kNumResultsPerQuery);
        }
        latch.CountDown();
      });
  }
  latch.Wait();
  auto query_elapsed_usec = (MonoTime::Now() - query_start_time).ToMicroseconds();
  ReportPerf("Performed", kNumQueries, "queries", kDimensions, query_elapsed_usec,
              kNumIndexingThreads);
};

}  // namespace yb

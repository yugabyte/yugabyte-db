// Copyright 2025 The Google Research Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// C++ port of scann_ops_test.py - tests ScannInterface without TensorFlow.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <random>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "scann/data_format/datapoint.h"
#include "scann/data_format/dataset.h"
#include "scann/oss_wrappers/scann_status.h"
#include "scann/scann_ops/cc/scann.h"
#include "scann/utils/common.h"
#include "scann/utils/types.h"

namespace research_scann {
namespace {

constexpr int kNumDatasetPoints = 10000;
constexpr int kNumQueries = 100;
constexpr int kDimension = 32;
constexpr uint32_t kSeed = 518;

// Generates random float data in [0, 1).
std::vector<float> RandomDataset(size_t n_points, size_t dim, uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  std::vector<float> data(n_points * dim);
  for (auto& v : data) v = dist(rng);
  return data;
}

// Reference brute-force dot product: for each query, compute dot products with
// all dataset points and return top-k indices and distances. DotProductDistance
// returns -dot_product (smaller = better), so we match that convention.
void NumpyBruteForceDotProduct(ConstSpan<float> dataset, ConstSpan<float> queries,
                               size_t n_dataset, size_t n_queries, size_t dim,
                               int k, std::vector<int32_t>* out_indices,
                               std::vector<float>* out_distances) {
  out_indices->resize(n_queries * k);
  out_distances->resize(n_queries * k);

  for (size_t q = 0; q < n_queries; ++q) {
    const float* qptr = queries.data() + q * dim;
    std::vector<std::pair<float, int32_t>> products(n_dataset);

    for (size_t i = 0; i < n_dataset; ++i) {
      float dot = 0;
      const float* dptr = dataset.data() + i * dim;
      for (size_t j = 0; j < dim; ++j) dot += qptr[j] * dptr[j];
      products[i] = {-dot, static_cast<int32_t>(i)};  // -dot for distance
    }

    std::partial_sort(
        products.begin(), products.begin() + k, products.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    for (int j = 0; j < k; ++j) {
      (*out_distances)[q * k + j] = products[j].first;
      (*out_indices)[q * k + j] = products[j].second;
    }
  }
}

std::string ConfigWithDimensionality(const std::string& base_config, int dim) {
  return base_config + R"(
    input_output {
      pure_dynamic_config {
        dimensionality: )" +
         std::to_string(dim) + R"(
      }
    }
  )";
}

// Builds a config for AH (asymmetric hashing) scoring.
std::string AhConfig(int num_neighbors, int dim) {
  return ConfigWithDimensionality(
      R"(
        num_neighbors: )" +
          std::to_string(num_neighbors) + R"(
        distance_measure { distance_measure: "DotProductDistance" }
        hash {
          asymmetric_hash {
            lookup_type: INT8_LUT16
            use_residual_quantization: false
            use_global_topn: true
            quantization_distance { distance_measure: "SquaredL2Distance" }
            num_clusters_per_block: 16
            projection {
              input_dim: )" +
          std::to_string(dim) + R"(
              projection_type: CHUNK
              num_blocks: )" +
          std::to_string(dim / 2) + R"(
              num_dims_per_block: 2
            }
            fixed_point_lut_conversion_options {
              float_to_int_conversion_method: ROUND
            }
            expected_sample_size: 100000
            max_clustering_iterations: 10
          }
        }
      )",
      dim);
}

// Builds a config for tree + AH.
std::string TreeAhConfig(int num_neighbors, int dim) {
  return ConfigWithDimensionality(
      R"(
        num_neighbors: )" +
          std::to_string(num_neighbors) + R"(
        distance_measure { distance_measure: "DotProductDistance" }
        partitioning {
          num_children: 100
          min_cluster_size: 20
          max_clustering_iterations: 12
          single_machine_center_initialization: RANDOM_INITIALIZATION
          partitioning_distance { distance_measure: "SquaredL2Distance" }
          query_spilling {
            spilling_type: FIXED_NUMBER_OF_CENTERS
            max_spill_centers: 20
          }
          expected_sample_size: 100000
          query_tokenization_distance_override { distance_measure: "DotProductDistance" }
          partitioning_type: GENERIC
          query_tokenization_type: FLOAT
        }
        hash {
          asymmetric_hash {
            lookup_type: INT8_LUT16
            use_residual_quantization: true
            use_global_topn: true
            quantization_distance { distance_measure: "SquaredL2Distance" }
            num_clusters_per_block: 16
            projection {
              input_dim: )" +
          std::to_string(dim) + R"(
              projection_type: CHUNK
              num_blocks: )" +
          std::to_string(dim / 2) + R"(
              num_dims_per_block: 2
            }
            fixed_point_lut_conversion_options {
              float_to_int_conversion_method: ROUND
            }
            expected_sample_size: 100000
            max_clustering_iterations: 10
          }
        }
      )",
      dim);
}

// Builds a config for tree + brute force.
std::string TreeBruteForceConfig(int num_neighbors, int dim) {
  return ConfigWithDimensionality(
      R"(
        num_neighbors: )" +
          std::to_string(num_neighbors) + R"(
        distance_measure { distance_measure: "DotProductDistance" }
        partitioning {
          num_children: 100
          min_cluster_size: 10
          max_clustering_iterations: 12
          single_machine_center_initialization: RANDOM_INITIALIZATION
          partitioning_distance { distance_measure: "SquaredL2Distance" }
          query_spilling {
            spilling_type: FIXED_NUMBER_OF_CENTERS
            max_spill_centers: 10
          }
          expected_sample_size: 100000
          query_tokenization_distance_override { distance_measure: "DotProductDistance" }
          partitioning_type: GENERIC
          query_tokenization_type: FLOAT
        }
        brute_force {
          fixed_point { enabled: true }
        }
      )",
      dim);
}

// Builds a config for brute force only (no partitioning).
std::string BruteForceConfig(int num_neighbors, int dim) {
  return ConfigWithDimensionality(
      R"(
        num_neighbors: )" +
          std::to_string(num_neighbors) + R"(
        distance_measure { distance_measure: "DotProductDistance" }
        brute_force {
          fixed_point { enabled: true }
        }
      )",
      dim);
}

// Builds a config for AH + reordering.
std::string ReorderConfig(int num_neighbors, int dim) {
  return ConfigWithDimensionality(
      R"(
        num_neighbors: )" +
          std::to_string(num_neighbors) + R"(
        distance_measure { distance_measure: "DotProductDistance" }
        hash {
          asymmetric_hash {
            lookup_type: INT8_LUT16
            use_residual_quantization: false
            use_global_topn: true
            quantization_distance { distance_measure: "SquaredL2Distance" }
            num_clusters_per_block: 16
            projection {
              input_dim: )" +
          std::to_string(dim) + R"(
              projection_type: CHUNK
              num_blocks: )" +
          std::to_string(dim / 2) + R"(
              num_dims_per_block: 2
            }
            fixed_point_lut_conversion_options {
              float_to_int_conversion_method: ROUND
            }
            expected_sample_size: 100000
            max_clustering_iterations: 10
          }
        }
        exact_reordering {
          approx_num_neighbors: 40
          fixed_point { enabled: false }
        }
      )",
      dim);
}

class ScannOpsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dataset_ = RandomDataset(kNumDatasetPoints, kDimension, kSeed);
    queries_ = RandomDataset(kNumQueries, kDimension, kSeed + 1);
  }

  void SerializationTester(const std::string& config) {
    ScannInterface scann1;
    Status s1 = scann1.Initialize(
        MakeConstSpan(dataset_), kNumDatasetPoints, config, 4);
    ASSERT_TRUE(s1.ok()) << s1.ToString();

    std::vector<NNResultsVector> results1(kNumQueries);
    DenseDataset<float> query_dataset(
        std::vector<float>(queries_.begin(), queries_.end()), kNumQueries);
    Status s2 = scann1.SearchBatched(query_dataset, MakeMutableSpan(results1),
                                     15, 15, 0);
    ASSERT_TRUE(s2.ok()) << s2.ToString();

    auto tmpdir = std::filesystem::temp_directory_path() /
                  ("scann_ops_test_" + std::to_string(kSeed));
    std::filesystem::create_directories(tmpdir);
    auto assets_or = scann1.Serialize(tmpdir.string(), false);
    ASSERT_TRUE(assets_or.ok()) << assets_or.status().ToString();

    std::string assets_pbtxt;
    ASSERT_TRUE(google::protobuf::TextFormat::PrintToString(assets_or.value(), &assets_pbtxt))
        << "Failed to serialize ScannAssets to text";
    {
      std::ofstream out(tmpdir.string() + "/scann_assets.pbtxt");
      ASSERT_TRUE(out) << "Failed to open scann_assets.pbtxt for writing";
      out << assets_pbtxt;
    }

    ScannInterface scann2;
    auto load_or =
        ScannInterface::LoadArtifacts(tmpdir.string(), "");
    ASSERT_TRUE(load_or.ok()) << load_or.status().ToString();
    Status s3 = scann2.Initialize(std::move(load_or).value());
    ASSERT_TRUE(s3.ok()) << s3.ToString();

    std::vector<NNResultsVector> results2(kNumQueries);
    Status s4 = scann2.SearchBatched(query_dataset, MakeMutableSpan(results2),
                                     15, 15, 0);
    ASSERT_TRUE(s4.ok()) << s4.ToString();

    for (int q = 0; q < kNumQueries; ++q) {
      ASSERT_EQ(results1[q].size(), results2[q].size()) << "query " << q;
      for (size_t j = 0; j < results1[q].size(); ++j) {
        EXPECT_EQ(results1[q][j].first, results2[q][j].first)
            << "query " << q << " neighbor " << j;
        EXPECT_FLOAT_EQ(results1[q][j].second, results2[q][j].second)
            << "query " << q << " neighbor " << j;
      }
    }

    std::filesystem::remove_all(tmpdir);
  }

  std::vector<float> dataset_;
  std::vector<float> queries_;
};

TEST_F(ScannOpsTest, AhSerialization) {
  SerializationTester(AhConfig(15, kDimension));
}

TEST_F(ScannOpsTest, TreeAhSerialization) {
  SerializationTester(TreeAhConfig(15, kDimension));
}

TEST_F(ScannOpsTest, TreeBruteForceSerialization) {
  SerializationTester(TreeBruteForceConfig(10, kDimension));
}

TEST_F(ScannOpsTest, BruteForceInt8Serialization) {
  SerializationTester(BruteForceConfig(10, kDimension));
}

TEST_F(ScannOpsTest, ReorderingSerialization) {
  SerializationTester(ReorderConfig(10, kDimension));
}

TEST_F(ScannOpsTest, BruteForceMatchesReference) {
  ScannInterface scann;
  ASSERT_TRUE(scann.Initialize(
      MakeConstSpan(dataset_), kNumDatasetPoints,
      BruteForceConfig(10, kDimension), 4).ok());

  std::vector<NNResultsVector> results(kNumQueries);
  DenseDataset<float> query_dataset(
      std::vector<float>(queries_.begin(), queries_.end()), kNumQueries);
  ASSERT_TRUE(scann.SearchBatched(query_dataset, MakeMutableSpan(results), 10,
                                  10, 0).ok());

  std::vector<int32_t> ref_indices;
  std::vector<float> ref_distances;
  NumpyBruteForceDotProduct(
      MakeConstSpan(dataset_), MakeConstSpan(queries_), kNumDatasetPoints, kNumQueries, kDimension,
      15, &ref_indices, &ref_distances);

  for (int q = 0; q < kNumQueries; ++q) {
    auto q_ref_start = ref_indices.begin() + q * 15;
    auto q_ref_end = ref_indices.begin() + (q + 1) * 15;
    std::set<int32_t> ref_index_set(q_ref_start, q_ref_end);
    for (int j = 0; j < 10; ++j) {
      int32_t idx = results[q][j].first;
      EXPECT_TRUE(ref_index_set.count(idx))
          << "query " << q << " neighbor " << j << " index " << idx;
      int ref_pos = std::find(q_ref_start, q_ref_end, idx) - q_ref_start;
      ASSERT_LT(ref_pos, 15) << "query " << q << " index " << idx;
      EXPECT_NEAR(results[q][j].second, ref_distances[q * 15 + ref_pos], 0.03f)
          << "query " << q << " neighbor " << j;
    }
  }
}

TEST_F(ScannOpsTest, BruteForceFinalNumNeighbors) {
  ScannInterface scann;
  ASSERT_TRUE(scann.Initialize(
      MakeConstSpan(dataset_), kNumDatasetPoints,
      BruteForceConfig(10, kDimension), 4).ok());

  DenseDataset<float> query_dataset(
      std::vector<float>(queries_.begin(), queries_.end()), kNumQueries);

  std::vector<NNResultsVector> results20(kNumQueries);
  ASSERT_TRUE(scann.SearchBatched(query_dataset, MakeMutableSpan(results20),
                                  20, 20, 0).ok());

  std::vector<int32_t> ref_indices;
  std::vector<float> ref_distances;
  NumpyBruteForceDotProduct(
      MakeConstSpan(dataset_), MakeConstSpan(queries_), kNumDatasetPoints, kNumQueries, kDimension,
      25, &ref_indices, &ref_distances);

  for (int q = 0; q < kNumQueries; ++q) {
    ASSERT_EQ(results20[q].size(), 20u) << "query " << q;
    auto q_ref_start = ref_indices.begin() + q * 25;
    auto q_ref_end = ref_indices.begin() + (q + 1) * 25;
    std::set<int32_t> ref_index_set(q_ref_start, q_ref_end);
    for (int j = 0; j < 20; ++j) {
      int32_t idx = results20[q][j].first;
      EXPECT_TRUE(ref_index_set.count(idx))
          << "query " << q << " neighbor " << j << " index " << idx;
      int ref_pos = std::find(q_ref_start, q_ref_end, idx) - q_ref_start;
      ASSERT_LT(ref_pos, 25) << "query " << q << " index " << idx;
      EXPECT_NEAR(results20[q][j].second, ref_distances[q * 25 + ref_pos], 0.03f)
          << "query " << q << " neighbor " << j;
    }
  }
}

TEST_F(ScannOpsTest, SingleQuerySearch) {
  ScannInterface scann;
  ASSERT_TRUE(scann.Initialize(
      MakeConstSpan(dataset_), kNumDatasetPoints,
      BruteForceConfig(10, kDimension), 4).ok());

  std::vector<int32_t> ref_indices;
  std::vector<float> ref_distances;
  NumpyBruteForceDotProduct(
      MakeConstSpan(dataset_), MakeConstSpan(queries_), kNumDatasetPoints, 1, kDimension, 25,
      &ref_indices, &ref_distances);

  NNResultsVector single_result;
  ASSERT_TRUE(scann.Search(
      MakeDatapointPtr(queries_.data(), kDimension), &single_result, 20, 20, 0).ok());

  ASSERT_EQ(single_result.size(), 20u);
  std::set<int32_t> ref_index_set(ref_indices.begin(), ref_indices.end());
  for (int j = 0; j < 20; ++j) {
    int32_t idx = single_result[j].first;
    EXPECT_TRUE(ref_index_set.count(idx)) << "neighbor " << j << " index " << idx;
    int ref_pos = std::find(ref_indices.begin(), ref_indices.end(), idx) - ref_indices.begin();
    ASSERT_LT(ref_pos, static_cast<int>(ref_indices.size())) << "index " << idx;
    EXPECT_NEAR(single_result[j].second, ref_distances[ref_pos], 0.03f) << "neighbor " << j;
  }
}

TEST_F(ScannOpsTest, ParallelMatchesSequential) {
  std::vector<float> dataset = RandomDataset(10000, 32, 518);
  std::vector<float> queries = RandomDataset(1000, 32, 519);

  ScannInterface scann;
  ASSERT_TRUE(scann.Initialize(
      MakeConstSpan(dataset), 10000,
      TreeAhConfig(10, 32), 4).ok());

  scann.SetNumThreads(4);

  DenseDataset<float> query_dataset(
      std::vector<float>(queries.begin(), queries.end()), 1000);

  std::vector<NNResultsVector> results_seq(1000);
  ASSERT_TRUE(scann.SearchBatched(query_dataset, MakeMutableSpan(results_seq),
                                  10, 10, 0).ok());

  std::vector<NNResultsVector> results_par(1000);
  ASSERT_TRUE(scann.SearchBatchedParallel(query_dataset,
                                           MakeMutableSpan(results_par), 10,
                                           10, 0, 256).ok());

  for (int q = 0; q < 1000; ++q) {
    ASSERT_EQ(results_seq[q].size(), results_par[q].size()) << "query " << q;
    for (size_t j = 0; j < results_seq[q].size(); ++j) {
      EXPECT_EQ(results_seq[q][j].first, results_par[q][j].first)
          << "query " << q << " neighbor " << j;
      EXPECT_FLOAT_EQ(results_seq[q][j].second, results_par[q][j].second)
          << "query " << q << " neighbor " << j;
    }
  }
}

TEST_F(ScannOpsTest, ReorderingShapes) {
  ScannInterface scann;
  ASSERT_TRUE(scann.Initialize(
      MakeConstSpan(dataset_), kNumDatasetPoints,
      ReorderConfig(5, kDimension), 4).ok());

  DenseDataset<float> query_dataset(
      std::vector<float>(queries_.begin(), queries_.end()), kNumQueries);

  std::vector<NNResultsVector> results(kNumQueries);
  ASSERT_TRUE(scann.SearchBatched(query_dataset, MakeMutableSpan(results), 5,
                                  5, 0).ok());
  for (int q = 0; q < kNumQueries; ++q) {
    EXPECT_EQ(results[q].size(), 5u) << "query " << q;
  }

  ASSERT_TRUE(scann.SearchBatched(query_dataset, MakeMutableSpan(results), 8,
                                  8, 0).ok());
  for (int q = 0; q < kNumQueries; ++q) {
    EXPECT_EQ(results[q].size(), 8u) << "query " << q;
  }

  ASSERT_TRUE(scann.SearchBatched(query_dataset, MakeMutableSpan(results), 5,
                                  8, 0).ok());
  for (int q = 0; q < kNumQueries; ++q) {
    EXPECT_EQ(results[q].size(), 5u) << "query " << q;
  }

  ASSERT_TRUE(scann.SearchBatched(query_dataset, MakeMutableSpan(results), 6,
                                  8, 0).ok());
  for (int q = 0; q < kNumQueries; ++q) {
    EXPECT_EQ(results[q].size(), 6u) << "query " << q;
  }

  NNResultsVector single_result;
  ASSERT_TRUE(scann.Search(
      MakeDatapointPtr(queries_.data(), kDimension), &single_result, 5, 5, 0).ok());
  EXPECT_EQ(single_result.size(), 5u);

  ASSERT_TRUE(scann.Search(
      MakeDatapointPtr(queries_.data(), kDimension), &single_result, 8, 8, 0).ok());
  EXPECT_EQ(single_result.size(), 8u);

  ASSERT_TRUE(scann.Search(
      MakeDatapointPtr(queries_.data(), kDimension), &single_result, 5, 8, 0).ok());
  EXPECT_EQ(single_result.size(), 5u);

  ASSERT_TRUE(scann.Search(
      MakeDatapointPtr(queries_.data(), kDimension), &single_result, 6, 8, 0).ok());
  EXPECT_EQ(single_result.size(), 6u);
}

}  // namespace
}  // namespace research_scann

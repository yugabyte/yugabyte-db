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

#ifndef SCANN_SCANN_OPS_CC_SCANN_H_
#define SCANN_SCANN_OPS_CC_SCANN_H_

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <tuple>

#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "google/protobuf/text_format.h"
#include "scann/base/search_parameters.h"
#include "scann/base/single_machine_base.h"
#include "scann/base/single_machine_factory_options.h"
#include "scann/base/single_machine_factory_scann.h"
#include "scann/data_format/dataset.h"
#include "scann/scann_ops/scann_assets.pb.h"
#include "scann/utils/common.h"
#include "scann/utils/threads.h"

namespace research_scann {

/// High-level interface for SCANN (Scalable Approximate Nearest Neighbor)
/// search. Manages configuration, indexing, and querying of vector datasets
/// with support for single and batched searches, serialization, and
/// reindexing.
class ScannInterface {
 public:
  /// Tuple of artifacts needed to build or restore a searcher: config,
  /// optional in-memory dataset, and factory options (partitions, etc.).
  using ScannArtifacts =
      std::tuple<ScannConfig, shared_ptr<DenseDataset<float>>,
                 SingleMachineFactoryOptions>;

  /// Loads SCANN artifacts from an in-memory config and ScannAssets.
  ///
  /// \param config SCANN configuration (e.g. distance, partitioning).
  /// \param orig_assets Serialized assets (partitions, hashed dataset, etc.).
  /// \return ScannArtifacts on success, or error if loading fails.
  static StatusOr<ScannArtifacts> LoadArtifacts(const ScannConfig& config,
                                                const ScannAssets& orig_assets);

  /// Loads SCANN artifacts from disk.
  ///
  /// \param artifacts_dir Directory containing serialized SCANN files.
  /// \param scann_assets_pbtxt Optional path to scann_assets.pbtxt within
  ///        artifacts_dir; if empty, a default path is used.
  /// \return ScannArtifacts on success, or error if loading fails.
  static StatusOr<ScannArtifacts> LoadArtifacts(
      const std::string& artifacts_dir,
      const std::string& scann_assets_pbtxt = "");

  /// Builds a single-machine searcher from pre-loaded artifacts.
  ///
  /// \param artifacts Output from LoadArtifacts (config, dataset, options).
  /// \return Unique pointer to the searcher, or error on failure.
  static StatusOr<std::unique_ptr<SingleMachineSearcherBase<float>>>
  CreateSearcher(ScannArtifacts artifacts);

  /// Initializes the interface from config and assets stored as text protos.
  ///
  /// \param config_pbtxt Text-format ScannConfig.
  /// \param scann_assets_pbtxt Text-format ScannAssets (or path to file).
  /// \return OkStatus on success.
  Status Initialize(const std::string& config_pbtxt,
                    const std::string& scann_assets_pbtxt);

  /// Initializes the interface from raw buffers and config/options.
  /// Used when supplying dataset and optional compressed/quantized views
  /// directly (e.g. after custom loading or preprocessing).
  ///
  /// \param config SCANN configuration.
  /// \param opts Factory options (partitions, etc.).
  /// \param dataset Dense float dataset (row-major, size n_points * dim).
  /// \param datapoint_to_token Optional partition token per datapoint.
  /// \param hashed_dataset Optional hashed representation.
  /// \param int8_dataset Optional int8-quantized dataset.
  /// \param int8_multipliers Optional multipliers for int8 dequantization.
  /// \param dp_norms Optional per-datapoint norms.
  /// \param n_points Number of datapoints in the dataset.
  /// \return OkStatus on success.
  Status Initialize(ScannConfig config, SingleMachineFactoryOptions opts,
                    ConstSpan<float> dataset,
                    ConstSpan<int32_t> datapoint_to_token,
                    ConstSpan<uint8_t> hashed_dataset,
                    ConstSpan<int8_t> int8_dataset,
                    ConstSpan<float> int8_multipliers,
                    ConstSpan<float> dp_norms, DatapointIndex n_points);

  /// Initializes and trains/indexes from a raw dataset and config string.
  /// Performs training (e.g. partition centers) and builds the index.
  ///
  /// \param dataset Dense float dataset (row-major).
  /// \param n_points Number of datapoints.
  /// \param config Text-format ScannConfig.
  /// \param training_threads Number of threads for training/indexing.
  /// \return OkStatus on success.
  Status Initialize(ConstSpan<float> dataset, DatapointIndex n_points,
                    const std::string& config, int training_threads);

  /// Initializes the interface from pre-loaded ScannArtifacts.
  ///
  /// \param artifacts Output from LoadArtifacts.
  /// \return OkStatus on success.
  Status Initialize(ScannArtifacts artifacts);

  /// Returns a mutator for in-place updates to the underlying index (e.g.
  /// add/remove datapoints). Not all searcher types support mutation.
  ///
  /// \return Pointer to Mutator on success, or error if not supported.
  StatusOr<typename SingleMachineSearcherBase<float>::Mutator*> GetMutator()
      const {
    return scann_->GetMutator();
  }

  /// Retrains the model (e.g. partition centers) and rebuilds the index
  /// using the given config. Dataset is taken from the current searcher.
  ///
  /// \param config New text-format ScannConfig for retraining.
  /// \return Updated ScannConfig on success, or error on failure.
  StatusOr<ScannConfig> RetrainAndReindex(const string& config);

  /// Performs approximate nearest neighbor search for a single query.
  ///
  /// \param query Query vector (DatapointPtr to float data).
  /// \param res Output vector of (datapoint_index, distance) pairs.
  /// \param final_nn Number of nearest neighbors to return.
  /// \param pre_reorder_nn Number of candidates before final reordering.
  /// \param leaves Number of partition leaves to search (0 = auto).
  /// \return OkStatus on success.
  Status Search(const DatapointPtr<float> query, NNResultsVector* res,
                int final_nn, int pre_reorder_nn, int leaves) const;

  /// Performs ANN search for a batch of queries (single-threaded).
  ///
  /// \param queries DenseDataset of query vectors.
  /// \param res Mutable span of result vectors; res.size() must equal
  ///        queries.size(); each entry filled with (index, distance) pairs.
  /// \param final_nn Number of neighbors per query.
  /// \param pre_reorder_nn Candidates before reorder.
  /// \param leaves Partition leaves to search (0 = auto).
  /// \return OkStatus on success.
  Status SearchBatched(const DenseDataset<float>& queries,
                       MutableSpan<NNResultsVector> res, int final_nn,
                       int pre_reorder_nn, int leaves) const;

  /// Batched ANN search using the internal thread pool for parallelism.
  ///
  /// \param queries DenseDataset of query vectors.
  /// \param res Mutable span of result vectors; size must match queries.size().
  /// \param final_nn Number of neighbors per query.
  /// \param pre_reorder_nn Candidates before reorder.
  /// \param leaves Partition leaves to search (0 = auto).
  /// \param batch_size Number of queries per parallel batch.
  /// \return OkStatus on success.
  Status SearchBatchedParallel(const DenseDataset<float>& queries,
                               MutableSpan<NNResultsVector> res, int final_nn,
                               int pre_reorder_nn, int leaves,
                               int batch_size = 256) const;

  /// Serializes the current index and config to disk.
  ///
  /// \param path Directory path to write artifacts into.
  /// \param relative_path If true, store relative paths in metadata.
  /// \return ScannAssets describing what was written, or error on failure.
  StatusOr<ScannAssets> Serialize(std::string path, bool relative_path = false);

  /// Extracts factory options (partitions, etc.) from the current searcher
  /// for reuse (e.g. when creating another searcher with the same structure).
  ///
  /// \return SingleMachineFactoryOptions on success, or error if unavailable.
  StatusOr<SingleMachineFactoryOptions> ExtractOptions();

  /// Copies single-query NN results into separate index and distance arrays,
  /// applying the internal result multiplier to distances.
  ///
  /// \param res NN result vector of (index, distance) pairs.
  /// \param indices Output array of neighbor indices (length res.size()).
  /// \param distances Output array of (scaled) distances (length res.size()).
  template <typename T_idx>
  void ReshapeNNResult(const NNResultsVector& res, T_idx* indices,
                       float* distances);

  /// Copies batched NN results into contiguous index and distance arrays.
  /// Each query gets exactly neighbors_per_query slots; missing neighbors
  /// are filled with index 0 and NaN distance.
  ///
  /// \param res One NNResultsVector per query.
  /// \param indices Output array: row-major [num_queries][neighbors_per_query].
  /// \param distances Output array: same layout as indices.
  /// \param neighbors_per_query Number of neighbor slots per query.
  template <typename T_idx>
  void ReshapeBatchedNNResult(ConstSpan<NNResultsVector> res, T_idx* indices,
                              float* distances, int neighbors_per_query);

  /// Returns the underlying float32 dataset if the searcher needs it (e.g. for
  /// reordering). Not all searchers expose this; returns error if unavailable.
  ///
  /// \return Shared pointer to const DenseDataset<float>, or error.
  StatusOr<shared_ptr<const DenseDataset<float>>> Float32DatasetIfNeeded() {
    return scann_->SharedFloatDatasetIfNeeded();
  }

  /// Returns the number of datapoints in the indexed dataset.
  size_t n_points() const { return scann_->DatasetSize().value(); }

  /// Returns the dimensionality of the dataset and query vectors.
  DimensionIndex dimensionality() const { return dimensionality_; }

  /// Returns the current SCANN configuration. May refresh from the searcher
  /// if the searcher holds a config.
  const ScannConfig* config() {
    if (scann_->config().has_value()) config_ = *scann_->config();
    return &config_;
  }

  /// Returns the thread pool used for parallel batched search.
  std::shared_ptr<ThreadPool> parallel_query_pool() const {
    return parallel_query_pool_;
  }

  /// Sets the number of threads used for parallel query execution by
  /// (re)starting the internal query thread pool.
  ///
  /// \param num_threads Desired number of threads.
  void SetNumThreads(int num_threads) {
    parallel_query_pool_ = StartThreadPool("ScannQueryingPool", num_threads);
  }

  using ScannHealthStats = SingleMachineSearcherBase<float>::HealthStats;

  /// Returns health/stats for the underlying searcher (e.g. cache usage).
  /// Requires InitializeHealthStats() to have been called.
  StatusOr<ScannHealthStats> GetHealthStats() const;

  /// Enables and initializes collection of health stats for GetHealthStats().
  Status InitializeHealthStats();

 private:
  /// Builds SearchParameters from final_nn, pre_reorder_nn, and leaves.
  SearchParameters GetSearchParameters(int final_nn, int pre_reorder_nn,
                                       int leaves) const;

  /// Builds a vector of SearchParameters for batched search (e.g. one per
  /// query or per batch). set_unspecified fills in defaults for omitted fields.
  vector<SearchParameters> GetSearchParametersBatched(
      int batch_size, int final_nn, int pre_reorder_nn, int leaves,
      bool set_unspecified) const;

  /// Dimensionality of dataset and query vectors.
  DimensionIndex dimensionality_;
  /// Underlying single-machine searcher (owned).
  std::unique_ptr<SingleMachineSearcherBase<float>> scann_;
  /// Cached copy of SCANN config (see config()).
  ScannConfig config_;
  /// Multiplier applied to distances when reshaping results (e.g. for squared L2).
  float result_multiplier_;
  /// Minimum batch size used for batched search tuning.
  size_t min_batch_size_;
  /// Thread pool for parallel batched queries.
  std::shared_ptr<ThreadPool> parallel_query_pool_;
};

template <typename T_idx>
void ScannInterface::ReshapeNNResult(const NNResultsVector& res, T_idx* indices,
                                     float* distances) {
  // Copies (index, distance) pairs into separate arrays; distances scaled by
  // result_multiplier_. Caller must ensure indices/distances have length res.size().
  for (const auto& p : res) {
    *(indices++) = static_cast<T_idx>(p.first);
    *(distances++) = result_multiplier_ * p.second;
  }
}

template <typename T_idx>
void ScannInterface::ReshapeBatchedNNResult(ConstSpan<NNResultsVector> res,
                                            T_idx* indices, float* distances,
                                            int neighbors_per_query) {
  // Flattens batched results to row-major [num_queries][neighbors_per_query];
  // pads missing neighbors with index 0 and NaN distance.
  for (const auto& result_vec : res) {
    DCHECK_LE(result_vec.size(), neighbors_per_query);
    for (const auto& pair : result_vec) {
      *(indices++) = static_cast<T_idx>(pair.first);
      *(distances++) = result_multiplier_ * pair.second;
    }

    for (int i = result_vec.size(); i < neighbors_per_query; i++) {
      *(indices++) = 0;
      *(distances++) = std::numeric_limits<float>::quiet_NaN();
    }
  }
}

/// Parses a text-format protobuf string into the given proto message.
///
/// \param proto Output proto message to fill (e.g. ScannConfig, ScannAssets).
/// \param proto_str Text-format serialization of the same message type.
/// \return OkStatus on success, or error if parsing fails.
template <typename T>
Status ParseTextProto(T* proto, absl::string_view proto_str) {
  ::google::protobuf::TextFormat::ParseFromString(std::string(proto_str), proto);
  return OkStatus();
}

}  // namespace research_scann

#endif

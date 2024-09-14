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

#include <iostream>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/string_util.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector/ann_methods.h"
#include "yb/vector/ann_validation.h"
#include "yb/vector/benchmark_data.h"
#include "yb/vector/distance.h"
#include "yb/vector/graph_repr_defs.h"
#include "yb/vector/hnsw_options.h"
#include "yb/vector/hnsw_util.h"
#include "yb/vector/hnswlib_wrapper.h"
#include "yb/vector/sharded_index.h"
#include "yb/vector/usearch_wrapper.h"
#include "yb/vector/vector_index_wrapper_util.h"

#include "yb/tools/tool_arguments.h"

namespace po = boost::program_options;

using namespace std::literals;

namespace yb::tools {

// Rather than constantly adding needed identifiers from the vectorindex namespace here, it seem to
// be reasonable to import the whole namespace in this vector index focused tool.
using namespace yb::vectorindex;  // NOLINT

#define HNSW_ACTIONS (Help)(Benchmark)

YB_DEFINE_ENUM(HnswAction, HNSW_ACTIONS);

// ------------------------------------------------------------------------------------------------
// Help command
// ------------------------------------------------------------------------------------------------

const std::string kHelpDescription = kCommonHelpDescription;

using HelpArguments = CommonHelpArguments;

std::unique_ptr<OptionsDescription> HelpOptions() {
  return CommonHelpOptions();
}

Status HelpExecute(const HelpArguments& args) {
  return CommonHelpExecute<HnswAction>(args);
}

// ------------------------------------------------------------------------------------------------
// Benchmark command
// ------------------------------------------------------------------------------------------------

const std::string kBenchmarkDescription =
    "Run a benchmark on the HNSW algorithm with varying parameters";

// See the command-line help below for the documentation of these options.
struct BenchmarkArguments {
  bool validate_ground_truth = false;
  CoordinateKind coordinate_kind = CoordinateKind::kFloat32;
  HNSWOptions hnsw_options;
  size_t k = 0;
  size_t max_memory_for_loading_vectors_mb = 4096;
  size_t num_indexing_threads = 0;
  size_t num_query_threads = 0;
  size_t num_threads = 0;
  size_t num_validation_queries = 0;
  size_t num_vectors_to_insert = 0;
  size_t report_num_keys = 2500;
  size_t num_index_shards = 1;
  std::string build_vecs_path;
  std::string ground_truth_path;
  std::string query_vecs_path;
  ANNMethodKind ann_method;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        build_vecs_path,
        ground_truth_path,
        hnsw_options,
        k,
        num_indexing_threads,
        num_query_threads,
        num_threads,
        num_validation_queries,
        num_vectors_to_insert,
        query_vecs_path,
        report_num_keys,
        validate_ground_truth
    );
  }

  // Set default values of some options, potentially based on the values of other options.
  void FinalizeDefaults() {
    if (num_threads == 0) {
      num_threads = std::thread::hardware_concurrency();
    }
    if (num_indexing_threads == 0) {
      num_indexing_threads = num_threads;
    }
    if (num_query_threads == 0) {
      num_query_threads = num_threads;
    }
  }
};

std::unique_ptr<OptionsDescription> BenchmarkOptions() {
  auto result = std::make_unique<OptionsDescriptionImpl<BenchmarkArguments>>(kBenchmarkDescription);
  auto& args = result->args;
  const HNSWOptions default_hnsw_options;

#define OPTIONAL_ARG_FIELD(field_name) \
    BOOST_PP_STRINGIZE(field_name), po::value(&args.field_name)->default_value( \
        args.field_name)

#define OPTIONAL_ARG_FIELD_WITH_LOWER_BOUND(field_name, lower_bound) \
    OPTIONAL_ARG_FIELD(field_name)->notifier( \
        OptionLowerBound(BOOST_PP_STRINGIZE(field_name), lower_bound))

#define BOOL_SWITCH_ARG_FIELD(field_name) \
    BOOST_PP_STRINGIZE(field_name), po::bool_switch(&args.field_name)

#define HNSW_OPTION_ARG(field_name) \
    BOOST_PP_STRINGIZE(field_name), \
    po::value(&args.hnsw_options.field_name)->default_value(default_hnsw_options.field_name)

// TODO: we only support bool options where default values are false.
#define HNSW_OPTION_BOOL_ARG(field_name) \
    BOOST_PP_STRINGIZE(field_name), po::bool_switch(&args.hnsw_options.field_name)

  const auto ann_method_help =
      Format("Approximate nearest neighbor search method to use. Possible values: $0.",
             ValidEnumValuesCommaSeparatedForHelp<ANNMethodKind>());
  const auto distance_kind_help =
      Format("What kind of distance function (metric) to use. Possible values: $0." +
             ValidEnumValuesCommaSeparatedForHelp<DistanceKind>());

  result->desc.add_options()
      (OPTIONAL_ARG_FIELD(ann_method),
       ann_method_help.c_str() /* Boost copies the string internally */)
      (OPTIONAL_ARG_FIELD(num_vectors_to_insert),
       "Number of vectors to use for building the index. This is used if no input file is "
       "specified.")
      (OPTIONAL_ARG_FIELD(build_vecs_path),
       "Input file containing vectors to build the index on, in the fvecs/bvecs/ivecs format.")
      (OPTIONAL_ARG_FIELD(query_vecs_path),
       "Input file containing vectors to query the dataset with, in the fvecs/bvecs/ivecs format.")
      (OPTIONAL_ARG_FIELD(ground_truth_path),
       "Input file containing integer vectors of correct nearest neighbor vector identifiers "
       "(0-based in the input dataset) for each query.")
      (OPTIONAL_ARG_FIELD(k),
       "Number of results to retrieve with each validation query")
      (OPTIONAL_ARG_FIELD(num_validation_queries),
       "Number of validation queries to execute")
      ("dimensions", po::value(&args.hnsw_options.dimensions),
       "Number of dimensions for automatically generated vectors. Required if no input file "
       "is specified.")
      (OPTIONAL_ARG_FIELD_WITH_LOWER_BOUND(report_num_keys, 1),
       "Report progress after each batch of this many keys is inserted. 0 to disable reporting.")
      (HNSW_OPTION_BOOL_ARG(extend_candidates),
       "Whether to extend the set of candidates with their neighbors before executing the "
       "neighborhood selection heuristic.")
      (HNSW_OPTION_BOOL_ARG(keep_pruned_connections),
       "Whether to keep the maximum number of discarded candidates with the minimum distance to "
       "the base element in the neighborhood selection heuristic.")
      (HNSW_OPTION_ARG(num_neighbors_per_vertex),
       "Number of neighbors for newly inserted vertices at levels other than 0 (base level).")
      (HNSW_OPTION_ARG(max_neighbors_per_vertex),
       "Maximum number of neighbors for vertices at levels other than 0 (base level).")
      (HNSW_OPTION_ARG(num_neighbors_per_vertex_base),
       "Number of neighbors for newly inserted vertices at levels 0 (base level).")
      (HNSW_OPTION_ARG(max_neighbors_per_vertex_base),
       "Maximum number of neighbors for vertices at level 0 (base level).")
      (HNSW_OPTION_ARG(ml),
       "The scaling factor used to randomly select the level for a newly added vertex. "
       "Setting this to 1 / log(2), or ~1.44, results in the average number of points at every "
       "level being half of the number of points at the level below it. Higher values of this ")
      (HNSW_OPTION_ARG(ef_construction),
       "The number of closest neighbors at each level that are used to determine the candidates "
       "used for constructing the neighborhood of a newly added vertex. Higher values result in "
       "a higher quality graph at the cost of higher indexing time.")
      (HNSW_OPTION_ARG(robust_prune_alpha),
       "The parameter inspired by DiskANN that controls the neighborhood pruning procedure. "
       "Higher values result in fewer candidates being pruned. Typically between 1.0 and 1.6.")
      (HNSW_OPTION_ARG(distance_kind),
       distance_kind_help.c_str())
      (OPTIONAL_ARG_FIELD(num_threads),
       "Number of threads to use for indexing and validation. Defaults to the number of CPU "
       "cores.")
      (OPTIONAL_ARG_FIELD(num_indexing_threads),
       "Number of threads to use for indexing. Defaults to num_threads.")
      (OPTIONAL_ARG_FIELD(num_query_threads),
       "Number of threads to use for validation. Defaults to num_threads.")
      (BOOL_SWITCH_ARG_FIELD(validate_ground_truth),
       "Validate the ground truth data provided in a file by recomputing our own ground truth "
       "result sets using brute-force precise nearest neighbor search. Could be slow.")
      (OPTIONAL_ARG_FIELD(max_memory_for_loading_vectors_mb),
       "Maximum amount of memory to use for loading raw input vectors. Used to avoid memory "
       "overflow on large datasets. Specify 0 to disable.")
      (OPTIONAL_ARG_FIELD_WITH_LOWER_BOUND(num_index_shards, 1),
       "For experiments that try to take advantage of a large number of cores, this allows to "
       "create multiple instances of the vector index and insert into them concurrently.");

#undef OPTIONAL_ARG_FIELD
#undef BOOL_SWITCH_ARG_FIELD
#undef HNSW_OPTION_ARG
#undef HNSW_OPTION_BOOL_ARG

  return result;
}

Result<CoordinateKind> DetermineCoordinateKind(BenchmarkArguments& args) {
  std::optional<CoordinateKind> coordinate_kind;
  for (const std::string& file_path : {args.build_vecs_path, args.query_vecs_path}) {
    if (!file_path.empty()) {
      auto file_coordinate_kind = VERIFY_RESULT(GetCoordinateKindFromVecsFileName(file_path));
      if (coordinate_kind.has_value() &&
          *coordinate_kind != file_coordinate_kind) {
        return STATUS_FORMAT(
            InvalidArgument,
            "Indexed vectors and query vectors must use the same coordinate type.");
      }
      coordinate_kind = file_coordinate_kind;
    }
  }
  if (coordinate_kind.has_value()) {
    args.coordinate_kind = *coordinate_kind;
  }
  return args.coordinate_kind;
}

std::unique_ptr<FloatVectorSource> CreateRandomFloatVectorSource(
    size_t num_vectors, size_t dimensions) {
  return CreateUniformRandomVectorSource(num_vectors, dimensions, 0.0f, 1.0f);
}

// We instantiate this template as soon as we determine what coordinate type and distance result
// type we are working with.
//
// Because in some cases the input coordinate type is not supoprted by the index implementation,
// we are using separate "input" and "indexed" vector types and distance result types.
template<IndexableVectorType InputVector,
         ValidDistanceResultType InputDistanceResult,
         IndexableVectorType IndexedVector,
         ValidDistanceResultType IndexedDistanceResult>
class BenchmarkTool {
 public:
  explicit BenchmarkTool(
      const BenchmarkArguments& args,
      std::unique_ptr<VectorIndexFactory<IndexedVector, IndexedDistanceResult>> index_factory)
      : args_(args),
        index_factory_(std::move(index_factory)) {
  }

  Status Execute() {
    SCHECK_EQ(args_.coordinate_kind,
              CoordinateTypeTraits<typename InputVector::value_type>::kKind,
              RuntimeError,
              "InputVector template argument does not match the inferred coordinate type");

    LOG(INFO) << "Using ANN method: " << args_.ann_method;
    LOG(INFO) << "Using input file coordinate type: " << args_.coordinate_kind;
    LOG(INFO) << "Vector index internally uses the coordinate type: "
              << CoordinateTypeTraits<typename IndexedVector::value_type>::kKind;

    LOG(INFO) << "Using distance result type in the input data: "
              << CoordinateTypeTraits<InputDistanceResult>::kKind;
    LOG(INFO) << "Using distance result type in the index implementation: "
              << CoordinateTypeTraits<IndexedDistanceResult>::kKind;
    if (args_.num_index_shards > 1) {
      LOG(INFO) << "Using " << args_.num_index_shards << " index shards";
    }
    indexed_vector_source_ = VERIFY_RESULT(CreateVectorSource(
        args_.build_vecs_path, "vectors to build index on", args_.num_vectors_to_insert));
    query_vector_source_ = VERIFY_RESULT(CreateVectorSource(
        args_.query_vecs_path, "vectors to query", args_.num_validation_queries));
    RETURN_NOT_OK(LoadPrecomputedGroundTruth());

    RETURN_NOT_OK(PrepareInputVectors());

    if (indexed_vector_source_->is_file() != query_vector_source_->is_file()) {
      return STATUS(
          InvalidArgument,
          "We must either load vectors to index from a file and load validation quries from "
          "another file, or randomly generate all the input data. Please either specify both "
          "--build_vecs_path and --query_vecs_path arguments or neither.");
    }

    PrintConfiguration();

    if (args_.num_index_shards > 1) {
      index_factory_ =
          std::make_unique<ShardedVectorIndexFactory<IndexedVector, IndexedDistanceResult>>(
              args_.num_index_shards, std::move(index_factory_));
    }

    index_factory_->SetOptions(hnsw_options());
    vector_index_ = index_factory_->Create();

    RETURN_NOT_OK(BuildIndex());

    RETURN_NOT_OK(Validate());

    // Print the configuration once again after a lot of output from the benchmark.
    PrintConfiguration();

    return Status::OK();
  }

 private:
  void PrintConfiguration() {
    LOG(INFO) << "Benchmark settings: " << args_.ToString();
  }

  Status SetDimensions(size_t dimensions) {
    SCHECK_GE(dimensions, static_cast<size_t>(0),
              InvalidArgument, "The number of dimensions must be at least 1");
    auto& current_dimensions = hnsw_options().dimensions;
    if (current_dimensions == 0) {
      current_dimensions = dimensions;
    } else if (current_dimensions != dimensions) {
      return STATUS_FORMAT(
          IllegalState,
          "The number of dimensions was already set to $0 but we are trying to set it to $1",
          current_dimensions, dimensions);
    }
    return Status::OK();
  }

  size_t dimensions() const {
    return hnsw_options().dimensions;
  }

  Result<std::unique_ptr<VectorSource<InputVector>>> CreateVectorSource(
      const std::string& vectors_file_path,
      const std::string& description,
      size_t num_vectors_to_use) {
    if (!vectors_file_path.empty()) {
      auto vec_reader = VERIFY_RESULT(OpenVecsFile<InputVector>(vectors_file_path, description));
      RETURN_NOT_OK(vec_reader->Open());
      RETURN_NOT_OK(SetDimensions(vec_reader->dimensions()));
      return vec_reader;
    }

    if (num_vectors_to_use > 0) {
      if constexpr (std::is_same<InputVector, FloatVector>::value) {
        return CreateRandomFloatVectorSource(args_.num_validation_queries, dimensions());
      }
      return STATUS(InvalidArgument,
                    "Random vector generation is currently only supported for Float32");
    }

    return STATUS_FORMAT(
        InvalidArgument,
        "Could not determine $0", description);
  }

  Status LoadPrecomputedGroundTruth() {
    if (args_.ground_truth_path.empty()) {
      return Status::OK();
    }
    CHECK_NOTNULL(query_vector_source_);
    if (!query_vector_source_->is_file()) {
      return STATUS(
          InvalidArgument,
          "Loading ground truth from file is only allowed when the queries are also loaded from "
          "a file.");
    }
    // The ground truth file contains result sets composed of 0-based vector indexes in the
    // corresponding "base" input file. Convert them to our vertex ids.
    auto ground_truth_by_index = VERIFY_RESULT(
        LoadVecsFile<Int32Vector>(args_.ground_truth_path, "precomputed ground truth file"));
    if (ground_truth_by_index.size() != query_vector_source_->num_points()) {
      return STATUS_FORMAT(
          IllegalState,
          "The number of provided ground truth records $0 does not match the number of provided "
          "queries $1.",
          ground_truth_by_index.size(),
          query_vector_source_->num_points());
    }
    loaded_ground_truth_.clear();
    loaded_ground_truth_.reserve(ground_truth_by_index.size());
    std::unordered_set<int32_t> used_indexes;
    for (const auto& ground_truth_vec : ground_truth_by_index) {
      if (ground_truth_vec.size() != args_.k) {
        return STATUS_FORMAT(
            IllegalState,
            "Provided ground truth vector has size of $0 but the configured number k of top "
            "results is $1",
            ground_truth_vec.size(), args_.k);
      }
      loaded_ground_truth_.emplace_back();
      auto& correct_top_k_vertex_ids = loaded_ground_truth_.back();
      correct_top_k_vertex_ids.reserve(ground_truth_vec.size());
      used_indexes.clear();
      size_t total_num_vectors = indexed_vector_source_->num_points();
      for (auto idx : ground_truth_vec) {
        static const char* kInvalidMsg =
            "0-based vector index in a ground truth file is out of range";
        SCHECK_GE(idx, 0, IllegalState, kInvalidMsg);
        SCHECK_LT(idx, total_num_vectors, IllegalState, kInvalidMsg);
        correct_top_k_vertex_ids.push_back(InputVectorIndexToVertexId(idx));
        auto [_, inserted] = used_indexes.insert(idx);
        if (!inserted) {
          return STATUS(
              IllegalState, "The same index is used multiple times in a ground truth result list");
        }
      }
    }

    return Status::OK();
  }

  Status Validate() {
    std::vector<InputVector> query_vectors = VERIFY_RESULT(query_vector_source_->LoadVectors());

    auto distance_fn = GetDistanceFunction<InputVector, InputDistanceResult>(
        args_.hnsw_options.distance_kind);

    auto vertex_id_to_query_distance_fn =
      [this, &distance_fn](VertexId vertex_id, const InputVector& v) -> InputDistanceResult {
        // Avoid vector_cast on the critical path of the brute force search here.
        return distance_fn(input_vectors_[VertexIdToInputVectorIndex(vertex_id)], v);
      };

    VectorIndexReaderIf<InputVector, InputDistanceResult>* reader;
    using Adapter = VectorIndexReaderAdapter<
        IndexedVector, IndexedDistanceResult, InputVector, InputDistanceResult>;
    std::optional<Adapter> adapter;

    if constexpr (std::is_same_v<InputVector, IndexedVector>) {
      reader = vector_index_.get();
    } else {
      // In case the index uses a different vector type, create an adapter to map the results from
      // the indexed type back to the input type.
      adapter.emplace(*vector_index_.get());
      reader = &adapter.value();
    }
    // The ground truth evaluation is always done in the input coordinate type.
    GroundTruth<InputVector, InputDistanceResult> ground_truth(
        vertex_id_to_query_distance_fn,
        args_.k,
        query_vectors,
        loaded_ground_truth_,
        args_.validate_ground_truth,
        *reader,
        // The set of vertex ids to recompute ground truth with.
        //
        // In case ground truth is specified as an input file, it must have been computed using all
        // input vectors, so we must use all vectors to validate them. Otherwise, use the set of
        // vectors we've inserted.
        loaded_ground_truth_.empty() ? vertex_ids_to_insert_ : all_vertex_ids_);

    LOG(INFO) << "Validating with " << query_vectors.size() << " queries using "
              << args_.num_query_threads << " threads "
              << (args_.validate_ground_truth ? "(also recomputing and validating provided"
                                                " ground truth)"
                                              : "");

    auto start_time = MonoTime::Now();
    auto result = VERIFY_RESULT(ground_truth.EvaluateRecall(args_.num_query_threads));
    auto elapsed_time = MonoTime::Now() - start_time;
    LOG(INFO) << "Validation finished in " << elapsed_time;
    for (size_t j = 0; j < result.size(); ++j) {
      LOG(INFO) << (j + 1) << "-recall @ " << args_.k << ": " << StringPrintf("%.10f", result[j]);
    }
    return Status::OK();
  }

  size_t max_num_vectors_to_insert() {
    auto n = indexed_vector_source_->num_points();
    if (args_.num_vectors_to_insert) {
      n = std::min(args_.num_vectors_to_insert, n);
    }
    return n;
  }

  void ReportIndexingProgress(MonoTime load_start_time, size_t num_inserted, bool force = false) {
    if (!force &&
        (args_.report_num_keys == 0 ||
         num_inserted == 0 ||
         num_inserted % args_.report_num_keys != 0)) {
      return;
    }
    auto last_report_count = last_progress_report_count_.exchange(num_inserted);
    if (last_report_count == num_inserted) {
      // Already reported progress with this exact total count. Could happen at the end of load.
      // "force" does not affect this -- we avoid duplicate reports anyway.
      return;
    }
    auto elapsed_usec = (MonoTime::Now() - load_start_time).ToMicroseconds();
    double n_log_n_constant = elapsed_usec * 1.0 / num_inserted / log(num_inserted);
    double elapsed_time_sec = elapsed_usec / 1000000.0;
    size_t remaining_points = max_num_vectors_to_insert() - num_inserted;
    auto keys_per_sec = num_inserted / elapsed_time_sec;
    LOG(INFO) << "n: " << num_inserted << ", "
              << "elapsed time: " << StringPrintf("%.1f", elapsed_time_sec) << " sec, "
              << "O(n*log(n)) constant: " << n_log_n_constant << ", "
              << "remaining points: " << remaining_points << ", "
              << "keys per second: " << static_cast<size_t>(keys_per_sec) << ", "
              << "time remaining: "
              << StringPrintf("%.1f", keys_per_sec > 0 ? remaining_points / keys_per_sec : 0)
              << " sec";
  }

  Status PrepareInputVectors() {
    size_t num_vectors_to_load = max_num_vectors_to_insert();
    double total_mem_required_mb =
        num_vectors_to_load * sizeof(typename InputVector::value_type) *
        dimensions() / 1024.0 / 1024;
    if (args_.max_memory_for_loading_vectors_mb != 0 &&
        total_mem_required_mb > args_.max_memory_for_loading_vectors_mb) {
      return STATUS_FORMAT(
          IllegalState,
          "Decided that we should load $0 vectors, but the amount of memory required would be "
          "$1 MiB, which is more than the $2 MiB limit.",
          num_vectors_to_load, total_mem_required_mb, args_.max_memory_for_loading_vectors_mb);
    }

    input_vectors_ = VERIFY_RESULT(indexed_vector_source_->LoadVectors(num_vectors_to_load));
    size_t num_points_used = 0;
    const size_t max_to_insert = max_num_vectors_to_insert();

    for (size_t i = 0; i < max_to_insert; ++i) {
      vertex_ids_to_insert_.push_back(InputVectorIndexToVertexId(i));
      num_points_used++;
    }

    all_vertex_ids_.reserve(input_vectors_.size());
    for (size_t i = 0; i < input_vectors_.size(); ++i) {
      all_vertex_ids_.push_back(InputVectorIndexToVertexId(i));
    }

    if (num_points_used == 0) {
      return STATUS(IllegalState, "Did not find any vectors to add to the index");
    }
    return Status::OK();
  }

  Status InsertOneVector(VertexId vertex_id, MonoTime load_start_time) {
    const auto& v = GetVectorByVertexId(vertex_id);
    Status s = vector_index_->Insert(vertex_id, vector_cast<IndexedVector>(v));
    if (s.ok()) {
      auto new_num_inserted = num_vectors_inserted_.fetch_add(1, std::memory_order_acq_rel) + 1;
      ReportIndexingProgress(load_start_time, new_num_inserted);
    }
    return s;
  }

  Status InsertVectors() {
    const auto load_start_time = MonoTime::Now();
    num_vectors_inserted_ = 0;
    last_progress_report_count_ = 0;

    LOG(INFO) << "Inserting " << num_points_to_insert() << " vectors using " << args_.num_threads
              << " threads.";
    RETURN_NOT_OK(ProcessInParallel(
        args_.num_indexing_threads,
        /* start_index= */ static_cast<size_t>(0),
        /* end_index_exclusive= */ vertex_ids_to_insert_.size(),
        [this, load_start_time](size_t id_idx_to_insert) -> Status {
          return InsertOneVector(vertex_ids_to_insert_[id_idx_to_insert], load_start_time);
        }));

    auto num_inserted = num_vectors_inserted_.load();
    if (num_inserted == 0) {
      return STATUS(IllegalState, "Failed to insert any vectors at all");
    }
    auto load_elapsed_usec = (MonoTime::Now() - load_start_time).ToMicroseconds();
    ReportIndexingProgress(load_start_time, num_inserted, /* force= */ true);
    LOG(INFO) << "Inserted " << num_inserted << " vectors with " << dimensions()
              << " dimensions in " << (load_elapsed_usec / 1000.0) << " ms using "
              << args_.num_threads << " threads";

    return Status::OK();
  }

  Status BuildIndex() {
    RETURN_NOT_OK(vector_index_->Reserve(num_points_to_insert()));
    return InsertVectors();
  }

  HNSWOptions& hnsw_options() {
    return args_.hnsw_options;
  }

  const HNSWOptions& hnsw_options() const {
    return args_.hnsw_options;
  }

  size_t num_points_to_insert() const {
    return vertex_ids_to_insert_.size();
  }

  static constexpr VertexId kMinVertexId = 100;

  VertexId InputVectorIndexToVertexId(size_t input_vector_index) {
    CHECK_LE(input_vector_index, indexed_vector_source_->num_points());
    // Start from a small but round number to avoid making assumptions that the 0-based index of
    // a vector in the input file is the same as its vertex id.
    return input_vector_index + kMinVertexId;
  }

  size_t VertexIdToInputVectorIndex(VertexId vertex_id) {
    CHECK_GE(vertex_id, kMinVertexId);
    size_t index = vertex_id - kMinVertexId;
    CHECK_GE(index, 0);
    CHECK_LT(index, indexed_vector_source_->num_points());
    return index;
  }

  const InputVector& GetVectorByVertexId(VertexId vertex_id) {
    auto vector_index = VertexIdToInputVectorIndex(vertex_id);
    return input_vectors_[vector_index];
  }

  BenchmarkArguments args_;

  // Source from which we take vectors to build the index on.
  std::unique_ptr<VectorSource<InputVector>> indexed_vector_source_;

  // Source for vectors to run validation queries on.
  std::unique_ptr<VectorSource<InputVector>> query_vector_source_;

  std::unique_ptr<VectorIndexFactory<IndexedVector, IndexedDistanceResult>> index_factory_;
  std::unique_ptr<VectorIndexIf<IndexedVector, IndexedDistanceResult>> vector_index_;

  // Atomics used in multithreaded index construction.
  std::atomic<size_t> num_vectors_inserted_{0};  // Total # vectors inserted.
  std::atomic<size_t> last_progress_report_count_{0};  // Last reported progress at this # vectors.

  // Ground truth data loaded from a provided file, not computed on the fly.
  std::vector<std::vector<VertexId>> loaded_ground_truth_;

  std::vector<VertexId> vertex_ids_to_insert_;
  std::vector<VertexId> all_vertex_ids_;

  // Raw input vectors in the order they appeared in the input file.
  std::vector<InputVector> input_vectors_;
};

template<ANNMethodKind ann_method_kind,
         DistanceKind distance_kind,
         IndexableVectorType InputVector,
         IndexableVectorType IndexedVector>
std::optional<Status> BenchmarkExecuteHelper(
    const BenchmarkArguments& args,
    CoordinateKind input_coordinate_kind) {
  using InputDistanceResult = typename DistanceTraits<InputVector, distance_kind>::Result;
  using IndexedDistanceResult = typename DistanceTraits<IndexedVector, distance_kind>::Result;
  if (args.ann_method == ann_method_kind &&
      args.hnsw_options.distance_kind == distance_kind &&
      input_coordinate_kind == CoordinateTypeTraits<typename InputVector::value_type>::kKind) {
    using IndexFactory = typename ANNMethodTraits<ann_method_kind>::template IndexFactory<
        IndexedVector,
        typename DistanceTraits<IndexedVector, distance_kind>::Result>;
    return BenchmarkTool<InputVector, InputDistanceResult, IndexedVector, IndexedDistanceResult>(
        args,
        std::make_unique<IndexFactory>()
    ).Execute();
  }
  return std::nullopt;
}

Status BenchmarkExecute(const BenchmarkArguments& args) {
  auto args_copy = args;
  args_copy.FinalizeDefaults();

  LOG(INFO) << "Distance kind: " << args_copy.hnsw_options.distance_kind;

  // The input coordinate type is based on input file extensions.
  auto input_coordinate_kind = VERIFY_RESULT(DetermineCoordinateKind(args_copy));

  // Determining the right template arguments is a bit tricky. We have a few supported combinations
  // of the ANN method, distance function, input vector type, and the indexed vector type that the
  // method has to use in case the ANN method doesn't support the input vector type. To avoid
  // error-prone code duplication, we use a macro that expands to a bunch of if statements.

#define YB_VECTOR_INDEX_BENCHMARK_SUPPORTED_CASES      \
    /* method, distance,   input type, indexed type */  \
    /* Euclidean distance */                           \
    ((Usearch, L2Squared,    float,      float  ))     \
    ((Usearch, L2Squared,    uint8_t,    float  ))     \
    ((Hnswlib, L2Squared,    float,      float  ))     \
    ((Hnswlib, L2Squared,    uint8_t,    uint8_t))     \
    /* Cosine similarity */                            \
    ((Usearch, Cosine,       float,      float  ))     \
    ((Usearch, Cosine,       uint8_t,    float  ))     \
    /* Inner product */                                \
    ((Usearch, InnerProduct, float,      float  ))     \
    ((Usearch, InnerProduct, uint8_t,    float  ))     \
    ((Hnswlib, InnerProduct, float,      float  ))     \
    ((Hnswlib, InnerProduct, uint8_t,    uint8_t))

#define YB_VECTOR_INDEX_BENCHMARK_HELPER(method, distance_enum_element, input_type, indexed_type) \
    if (auto status = BenchmarkExecuteHelper< \
            ANNMethodKind::BOOST_PP_CAT(k, method), \
            distance_enum_element, \
            std::vector<input_type>, \
            std::vector<indexed_type>>(args_copy, input_coordinate_kind); status.has_value()) { \
        return *status; \
      }

#define YB_VECTOR_INDEX_BENCHMARK_FOR_EACH_HELPER(r, data, elem) \
    YB_VECTOR_INDEX_BENCHMARK_HELPER( \
        BOOST_PP_TUPLE_ELEM(4, 0, elem), \
        DistanceKind::BOOST_PP_CAT(k, BOOST_PP_TUPLE_ELEM(4, 1, elem)), \
        BOOST_PP_TUPLE_ELEM(4, 2, elem), \
        BOOST_PP_TUPLE_ELEM(4, 3, elem))

  BOOST_PP_SEQ_FOR_EACH(YB_VECTOR_INDEX_BENCHMARK_FOR_EACH_HELPER, _,
      YB_VECTOR_INDEX_BENCHMARK_SUPPORTED_CASES)

  return STATUS_FORMAT(
      InvalidArgument,
      "Unsupported combination of ANN method $0, distance kind $1, and input coordinate type $2",
      args_copy.ann_method,
      args_copy.hnsw_options.distance_kind,
      input_coordinate_kind);

  return Status::OK();
}

YB_TOOL_ARGUMENTS(HnswAction, HNSW_ACTIONS);

}  // namespace yb::tools

int main(int argc, char** argv) {
  yb::InitGoogleLoggingSafeBasic(argv[0]);
  return yb::tools::ExecuteTool<yb::tools::HnswAction>(argc, argv);
}

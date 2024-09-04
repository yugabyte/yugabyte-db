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

#include "yb/vector/hnsw_options.h"
#include "yb/vector/benchmark_data.h"
#include "yb/vector/ann_validation.h"
#include "yb/vector/graph_repr_defs.h"
#include "yb/vector/usearch_wrapper.h"
#include "yb/vector/distance.h"
#include "yb/vector/hnsw_util.h"

#include "yb/tools/tool_arguments.h"

namespace po = boost::program_options;

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
  size_t report_num_keys = 1000;
  std::string build_vecs_path;
  std::string ground_truth_path;
  std::string query_vecs_path;

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

#define BOOL_SWITCH_ARG_FIELD(field_name) \
    BOOST_PP_STRINGIZE(field_name), po::bool_switch(&args.field_name)

#define HNSW_OPTION_ARG(field_name) \
    BOOST_PP_STRINGIZE(field_name), \
    po::value(&args.hnsw_options.field_name)->default_value(default_hnsw_options.field_name)

// TODO: we only support bool options where default values are false.
#define HNSW_OPTION_BOOL_ARG(field_name) \
    BOOST_PP_STRINGIZE(field_name), po::bool_switch(&args.hnsw_options.field_name)

  result->desc.add_options()
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
      ("input_file_name_fvec", po::value(&args.num_vectors_to_insert),
       "Number of randomly generated vectors to add")
      (OPTIONAL_ARG_FIELD(k),
       "Number of results to retrieve with each validation query")
      (OPTIONAL_ARG_FIELD(num_validation_queries),
       "Number of validation queries to execute")
      ("dimensions", po::value(&args.hnsw_options.dimensions),
       "Number of dimensions for automatically generated vectors. Required if no input file "
       "is specified.")
      ("report_num_keys",
       po::value(&args.report_num_keys)->notifier(OptionLowerBound("report_num_keys", 1)),
       "Report progress after each batch of this many keys is inserted. 0 to disable reporting.")
      (HNSW_OPTION_BOOL_ARG(extend_candidates),
       "Whether to extend the set of candidates with their neighbors before executing the "
       "neihgborhood selection heuristic.")
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
       "overflow on large datasets. Specify 0 to disable.");

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
  return vectorindex::CreateUniformRandomVectorSource(num_vectors, dimensions, 0.0f, 1.0f);
}

// We instantiate this template as soon as we determine what coordinate type we are working with.
template<IndexableVectorType Vector>
class BenchmarkTool {
 public:
  // Usearch HNSW currently does not support other types of vectors, so we cast the input vectors to
  // float for now. See also: https://github.com/unum-cloud/usearch/issues/469
  using HNSWVectorType = FloatVector;
  using HNSWImpl = UsearchIndex<HNSWVectorType>;

  explicit BenchmarkTool(const BenchmarkArguments& args) : args_(args) {}

  Status Execute() {
    LOG(INFO) << "Uisng input file coordinate type: " << args_.coordinate_kind;

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

    hnsw_ = std::make_unique<HNSWImpl>(hnsw_options());

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

  Result<std::unique_ptr<VectorSource<Vector>>> CreateVectorSource(
      const std::string& vectors_file_path,
      const std::string& description,
      size_t num_vectors_to_use) {
    if (!vectors_file_path.empty()) {
      auto vec_reader = VERIFY_RESULT(OpenVecsFile<Vector>(vectors_file_path, description));
      RETURN_NOT_OK(vec_reader->Open());
      RETURN_NOT_OK(SetDimensions(vec_reader->dimensions()));
      return vec_reader;
    }

    if (num_vectors_to_use > 0) {
      if constexpr (std::is_same<Vector, FloatVector>::value) {
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
            "Provided ground truth vector has $0 dimensions but the configured number k of top "
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
    std::vector<Vector> query_vectors;
    for (;;) {
      auto query = VERIFY_RESULT(query_vector_source_->Next());
      if (query.empty()) {
        break;
      }
      query_vectors.push_back(query);
    }

    std::vector<FloatVector> float_query_vectors = ToFloatVectorOfVectors(query_vectors);

    vectorindex::GroundTruth<FloatVector> ground_truth(
        [this](VertexId vertex_id, const FloatVector& v) -> float {
          const auto& vertex_v = input_vectors_[VertexIdToInputVectorIndex(vertex_id)];
          return distance::DistanceL2Squared<Vector, FloatVector>(vertex_v, v);
        },
        args_.k,
        float_query_vectors,
        loaded_ground_truth_,
        args_.validate_ground_truth,
        *hnsw_,
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
    LOG(INFO) << "n: " << num_inserted << ", "
              << "elapsed time (seconds): " << elapsed_time_sec << ", "
              << "O(n*log(n)) constant: " << n_log_n_constant << ", "
              << "keys per second: " << (num_inserted / elapsed_time_sec);
  }

  Status PrepareInputVectors() {
    size_t num_vectors_to_load = max_num_vectors_to_insert();
    double total_mem_required_mb =
        num_vectors_to_load * sizeof(typename Vector::value_type) * dimensions() / 1024.0 / 1024;
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
    Status s;
    if constexpr (std::is_same<HNSWVectorType, Vector>::value) {
      s = hnsw_->Insert(vertex_id, v);
    } else {
      s = hnsw_->Insert(vertex_id, ToFloatVector(v));
    }
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
    hnsw_->Reserve(num_points_to_insert());
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

  const Vector& GetVectorByVertexId(VertexId vertex_id) {
    auto vector_index = VertexIdToInputVectorIndex(vertex_id);
    return input_vectors_[vector_index];
  }

  BenchmarkArguments args_;

  // Source from which we take vectors to build the index on.
  std::unique_ptr<VectorSource<Vector>> indexed_vector_source_;

  // Source for vectors to run validation queries on.
  std::unique_ptr<VectorSource<Vector>> query_vector_source_;

  std::unique_ptr<HNSWImpl> hnsw_;

  // Atomics used in multithreaded index construction.
  std::atomic<size_t> num_vectors_inserted_{0};  // Total # vectors inserted.
  std::atomic<size_t> last_progress_report_count_{0};  // Last reported progress at this # vectors.

  // Ground truth data loaded from a provided file, not computed on the fly.
  std::vector<std::vector<VertexId>> loaded_ground_truth_;

  std::vector<VertexId> vertex_ids_to_insert_;
  std::vector<VertexId> all_vertex_ids_;

  // Raw input vectors in the order they appeared in the input file.
  std::vector<Vector> input_vectors_;
};

Status BenchmarkExecute(const BenchmarkArguments& args) {
  auto args_copy = args;
  args_copy.FinalizeDefaults();
  auto coordinate_kind = VERIFY_RESULT(DetermineCoordinateKind(args_copy));
  switch (coordinate_kind) {
    case CoordinateKind::kFloat32:
      return BenchmarkTool<std::vector<float>>(args_copy).Execute();
    case CoordinateKind::kUInt8:
      return BenchmarkTool<std::vector<uint8_t>>(args_copy).Execute();
    default:
      return STATUS_FORMAT(
          InvalidArgument,
          "Input files with coordinate type $0 are not supported", coordinate_kind);
  }
}

YB_TOOL_ARGUMENTS(HnswAction, HNSW_ACTIONS);

}  // namespace yb::tools

int main(int argc, char** argv) {
  yb::InitGoogleLoggingSafeBasic(argv[0]);
  return yb::tools::ExecuteTool<yb::tools::HnswAction>(argc, argv);
}

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
  std::string build_vecs_path;
  std::string ground_truth_path;
  HNSWOptions hnsw_options;
  size_t k = 0;
  size_t num_indexing_threads = 0;
  size_t num_query_threads = 0;
  size_t num_threads = 0;
  size_t num_validation_queries = 1000;
  size_t num_vectors_to_insert = 0;
  std::string query_vecs_path;
  size_t report_num_keys = 1000;
  bool validate_ground_truth = false;

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
};

std::unique_ptr<OptionsDescription> BenchmarkOptions() {
  auto result = std::make_unique<OptionsDescriptionImpl<BenchmarkArguments>>(kBenchmarkDescription);
  auto& args = result->args;
  const HNSWOptions default_hnsw_options;

#define OPTIONAL_ARG_FIELD(field_name) \
    BOOST_PP_STRINGIZE(field_name), po::value(&args.field_name)

#define HNSW_OPTION_ARG(field_name) \
    BOOST_PP_STRINGIZE(field_name), \
    po::value(&args.hnsw_options.field_name)->default_value(default_hnsw_options.field_name)

// TODO: we only support bool options where default values are false.
#define HNSW_OPTION_BOOL_ARG(field_name) \
    BOOST_PP_STRINGIZE(field_name), \
    po::bool_switch(&args.hnsw_options.field_name)

  result->desc.add_options()
      (OPTIONAL_ARG_FIELD(num_vectors_to_insert),
       "Number of vectors to use for building the index. This is used if no input file is "
       "specified.")
      (OPTIONAL_ARG_FIELD(build_vecs_path),
       "Input file containing vectors to build the index on, in the fvecs/bvecs format.")
      (OPTIONAL_ARG_FIELD(query_vecs_path),
       "Input file containing vectors to query the dataset with, in the fvecs/bvecs format.")
      (OPTIONAL_ARG_FIELD(ground_truth_path),
       "Input file containing integer vectors of correct nearest neighbor vector identifiers "
       "(0-based in the input dataset) for each query.")
      ("input_file_name_fvec", po::value(&args.num_vectors_to_insert),
       "Number of randomly generated vectors to add")
      (OPTIONAL_ARG_FIELD(k),
       "Number of results to retrieve with each validatino query")
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
      (OPTIONAL_ARG_FIELD(validate_ground_truth),
       "Validate the ground truth data provided in a file by recomputing our own ground truth "
       "result sets using brute-force precise nearest neighbor search. Could be slow.");

#undef HNSW_OPTION_ARG
#undef HNSW_OPTION_BOOL_ARG
#undef OPTIONAL_ARG_FIELD

  return result;
}

class BenchmarkTool {
 public:
  explicit BenchmarkTool(const BenchmarkArguments& args) : args_(args) {}

  Status Execute() {
    if (args_.num_threads == 0) {
      args_.num_threads = std::thread::hardware_concurrency();
    }
    if (args_.num_indexing_threads == 0) {
      args_.num_indexing_threads = args_.num_threads;
    }
    if (args_.num_query_threads == 0) {
      args_.num_query_threads = args_.num_threads;
    }
    distance_fn_ = vectorindex::GetDistanceImpl(hnsw_options().distance_type);

    RETURN_NOT_OK(InitIndexingVectorSource());
    RETURN_NOT_OK(InitQueryVectorSource());
    RETURN_NOT_OK(PrepareInputVectors());
    RETURN_NOT_OK(LoadPrecomputedGroundTruth());

    // The booleans indexed_from_file_ and queries_from_file_ are set based on the presence of
    // build_vecs_path and query_vecs_path command-line arguments. Those two command-line arguments
    // specify separate file paths, so it is impractical to try to reduce both of them to a single
    // command-line argument.
    if (indexed_from_file_ != queries_from_file_) {
      return STATUS(
          InvalidArgument,
          "We must either load vectors to index from a file and load validation quries from "
          "another file, or randomly generate all the input data. Please either specify both "
          "--build_vecs_path and --query_vecs_path arguments or neither.");
    }

    PrintConfiguration();

    hnsw_ = std::make_unique<UsearchIndex>(hnsw_options());

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

  void SetDimensions(size_t dimensions) {
    hnsw_options().dimensions = dimensions;
  }

  size_t dimensions() const {
    return hnsw_options().dimensions;
  }

  Status InitIndexingVectorSource() {
    indexed_from_file_ = false;
    if (!args_.build_vecs_path.empty()) {
      auto fvec_reader = std::make_unique<FvecsFileReader>(args_.build_vecs_path);
      RETURN_NOT_OK(fvec_reader->Open());
      SetDimensions(fvec_reader->dimensions());
      LOG(INFO) << "Opened indexed dataset: " << fvec_reader->ToString();
      size_t num_points = fvec_reader->num_points();
      if (args_.num_vectors_to_insert) {
        if (num_points < args_.num_vectors_to_insert) {
          LOG(INFO) << "Number of vectors to insert from the command line: "
                    << num_points << ", but the file only contains " << num_points
                    << "vectors, ignoring the specified value.";
        } else if (num_points > args_.num_vectors_to_insert) {
          LOG(INFO) << "Will only use the first " << args_.num_vectors_to_insert << " vectors out "
                    << "of the " << num_points << " available in the file.";
        }
      }
      indexed_vector_source_ = std::move(fvec_reader);
      indexed_from_file_ = true;
    } else if (args_.num_vectors_to_insert > 0) {
      indexed_vector_source_ = vectorindex::CreateUniformRandomVectorSource(
          args_.num_vectors_to_insert, dimensions(), 0.0f, 1.0f);
    } else {
      return STATUS(
          InvalidArgument,
          "Neither an input file name nor the number of random vectors to generate is specified.");
    }
    return Status::OK();
  }

  Status InitQueryVectorSource() {
    queries_from_file_ = false;
    if (!args_.query_vecs_path.empty()) {
      auto fvec_reader = std::make_unique<FvecsFileReader>(args_.query_vecs_path);
      RETURN_NOT_OK(fvec_reader->Open());
      SetDimensions(fvec_reader->dimensions());
      LOG(INFO) << "Opened query dataset: " << fvec_reader->ToString();
      query_vector_source_ = std::move(fvec_reader);
      // We ignore args_.num_validation_queries in this case.
      queries_from_file_ = true;
    } else if (args_.num_validation_queries > 0) {
      query_vector_source_ = vectorindex::CreateUniformRandomVectorSource(
          args_.num_validation_queries, dimensions(), 0.0f, 1.0f);
    } else {
      return STATUS(
          InvalidArgument,
          "Could not determine what queries to use for validation");
    }
    return Status::OK();
  }

  Status LoadPrecomputedGroundTruth() {
    if (args_.ground_truth_path.empty()) {
      return Status::OK();
    }
    if (!queries_from_file_) {
      return STATUS(
          InvalidArgument,
          "Loading ground truth from file is only allowed when the queries are also loaded from "
          "a file.");
    }
    // The ground truth file contains result sets composed of 0-based vector indexes in the
    // corresponding "base" input file. Convert them to our vertex ids.
    auto ground_truth_by_index = VERIFY_RESULT(
        LoadVecsFile<int32_t>(args_.ground_truth_path, "precomputed ground truth file"));
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
      for (auto idx : ground_truth_vec) {
        static const char* kInvalidMsg =
            "0-based vector index in a ground truth file is out of range";
        SCHECK_GE(idx, 0, IllegalState, kInvalidMsg);
        SCHECK_LT(idx, input_vectors_.size(), IllegalState, kInvalidMsg);
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
    std::vector<FloatVector> query_vectors;
    for (;;) {
      auto query = VERIFY_RESULT(query_vector_source_->Next());
      if (query.empty()) {
        break;
      }
      query_vectors.push_back(query);
    }

    vectorindex::GroundTruth ground_truth(
        [this](VertexId vertex_id, const FloatVector& v) {
          const auto& vertex_v = input_vectors_[VertexIdToInputVectorIndex(vertex_id)];
          return distance_fn_(vertex_v, v);
        },
        args_.k,
        query_vectors,
        loaded_ground_truth_,
        args_.validate_ground_truth,
        *hnsw_,
        // The set of vertex ids to recompute ground truth with.
        //
        // In case ground truth is specified as an input file, it must have been computed using all
        // input vectors, so we must use all vectors to validate them. Otherwise, use the set of
        // vectors we've inserted.
        loaded_ground_truth_.empty() ? vertex_ids_to_insert_ : all_vertex_ids_);

    LOG(INFO) << "Validating of " << query_vectors.size() << " queries using "
              << args_.num_query_threads << " threads";

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
    input_vectors_ = VERIFY_RESULT(indexed_vector_source_->LoadAllVectors());
    size_t num_points_used = 0;
    const size_t max_to_insert = max_num_vectors_to_insert();
    for (size_t i = 0; i < input_vectors_.size(); ++i) {
      if (num_points_used >= max_to_insert) {
        LOG(INFO) << "Stopping after inserting " << max_to_insert << " input vectors";
        break;
      }
      auto vertex_id = InputVectorIndexToVertexId(i);
      vertex_ids_to_insert_.push_back(vertex_id);
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
    Status s = hnsw_->Insert(vertex_id, v);
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
    CHECK_LE(input_vector_index, input_vectors_.size());
    // Start from a small but round number to avoid making assumptions that the 0-based index of
    // a vector in the input file is the same as its vertex id.
    return input_vector_index + kMinVertexId;
  }

  size_t VertexIdToInputVectorIndex(VertexId vertex_id) {
    CHECK_GE(vertex_id, kMinVertexId);
    size_t index = vertex_id - kMinVertexId;
    CHECK_GE(index, 0);
    CHECK_LT(index, input_vectors_.size());
    return index;
  }

  const FloatVector& GetVectorByVertexId(VertexId vertex_id) {
    auto vector_index = VertexIdToInputVectorIndex(vertex_id);
    return input_vectors_[vector_index];
  }

  BenchmarkArguments args_;

  // Source from which we take vectors to build the index on.
  std::unique_ptr<FloatVectorSource> indexed_vector_source_;
  bool indexed_from_file_ = false;

  // Source for vectors to run validation queries on.
  std::unique_ptr<FloatVectorSource> query_vector_source_;
  bool queries_from_file_ = false;

  std::unique_ptr<UsearchIndex> hnsw_;

  DistanceFunction distance_fn_;

  // Atomics used in multithreaded index construction.
  std::atomic<size_t> num_vectors_inserted_{0};  // Total # vectors inserted.
  std::atomic<size_t> last_progress_report_count_{0};  // Last reported progress at this # vectors.

  // Ground truth data loaded from a provided file, not computed on the fly.
  std::vector<std::vector<VertexId>> loaded_ground_truth_;

  std::vector<VertexId> vertex_ids_to_insert_;
  std::vector<VertexId> all_vertex_ids_;

  // Raw input vectors in the order they appeared in the input file.
  std::vector<FloatVector> input_vectors_;
};

Status BenchmarkExecute(const BenchmarkArguments& args) {
  BenchmarkTool benchmark_tool(args);
  return benchmark_tool.Execute();
}

YB_TOOL_ARGUMENTS(HnswAction, HNSW_ACTIONS);

}  // namespace yb::tools

int main(int argc, char** argv) {
  yb::InitGoogleLoggingSafeBasic(argv[0]);
  return yb::tools::ExecuteTool<yb::tools::HnswAction>(argc, argv);
}

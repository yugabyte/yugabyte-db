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

#include <algorithm>
#include <atomic>
#include <iostream>
#include <map>
#include <numeric>
#include <ranges>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/ann_methods/ann_methods.h"
#include "yb/ann_methods/hnswlib_wrapper.h"
#include "yb/ann_methods/usearch_wrapper.h"

#include "yb/tools/pgvector_wrapper.h"

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/strip.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/hnsw/hnsw_block_cache.h"

#include "yb/rocksdb/cache.h"

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_util.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/ann_validation.h"
#include "yb/vector_index/benchmark_data.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnsw_options.h"
#include "yb/vector_index/hnsw_util.h"
#include "yb/vector_index/sharded_index.h"
#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/vector_index_wrapper_util.h"

#include "yb/tools/tool_arguments.h"

DECLARE_bool(vector_index_enable_block_cache_reservation);

namespace po = boost::program_options;

using namespace std::literals;
using namespace yb::size_literals;

namespace yb::tools {

// Rather than constantly adding needed identifiers from the vector_index namespace here, it seem to
// be reasonable to import the whole namespace in this vector index focused tool.
using namespace yb::vector_index;  // NOLINT

#define HNSW_ACTIONS (Help)(Benchmark)

YB_DEFINE_ENUM(HnswAction, HNSW_ACTIONS);

// --ann_method values, parsed case-insensitively with an optional `k` prefix.
YB_DEFINE_ENUM(BenchmarkAnnMethod, (kUsearch)(kHnswlib)(kPgVector));

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

  std::string k_values_comma_sep = "10";
  size_t num_indexing_threads = 0;
  size_t num_query_threads = 0;
  size_t num_threads = 0;
  size_t num_validation_queries = 0;
  size_t num_vectors_to_insert = 0;
  size_t report_num_keys = 0;
  size_t report_interval_ms = 1000;
  size_t num_index_shards = 1;
  std::string build_vecs_path;
  std::string ground_truth_path;
  std::string query_vecs_path;
  BenchmarkAnnMethod ann_method;
  std::string yb_hnsw_dir;
  std::string yb_hnsw_variants = "yb_hnsw_reorder_vectors=false;yb_hnsw_reorder_vectors=true";
  size_t block_cache_size_mb = 16384;
  std::string ef_values_comma_sep;
  size_t num_search_repeats = 0;
  size_t search_time_ms = 1000;

  // Used only with --ann_method pgvector, from the --pg_* options.
  PgVectorConfig pg_vector;

  // Parsed version of k_values.
  std::set<size_t> k_values;
  size_t max_k = 0;

  // Parsed version of ef_values.
  std::set<size_t> ef_values;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        build_vecs_path,
        ground_truth_path,
        hnsw_options,
        k_values_comma_sep,
        num_indexing_threads,
        num_query_threads,
        num_threads,
        num_validation_queries,
        num_vectors_to_insert,
        query_vecs_path,
        report_num_keys,
        validate_ground_truth,
        yb_hnsw_dir,
        yb_hnsw_variants,
        block_cache_size_mb,
        ef_values_comma_sep,
        num_search_repeats,
        search_time_ms
    );
  }

  // Set default values of some options, potentially based on the values of other options.
  Status Finalize() {
    if (num_threads == 0) {
      num_threads = std::thread::hardware_concurrency();
    }
    if (num_indexing_threads == 0) {
      num_indexing_threads = num_threads;
    }
    if (num_query_threads == 0) {
      num_query_threads = num_threads;
    }
    auto k_values_result = ParseCommaSeparatedListOfNumbers<size_t, std::set<size_t>>(
        k_values_comma_sep, /* lower_bound= */ static_cast<size_t>(1));
    RETURN_NOT_OK(k_values_result);
    k_values = std::move(*k_values_result);
    if (k_values.empty()) {
      return STATUS(InvalidArgument, "No values of k (size of top results list) specified");
    }
    max_k = *k_values.rbegin();
    if (ef_values_comma_sep.empty()) {
      ef_values = {hnsw_options.ef};
    } else {
      ef_values = VERIFY_RESULT((ParseCommaSeparatedListOfNumbers<size_t, std::set<size_t>>(
          ef_values_comma_sep, /* lower_bound= */ static_cast<size_t>(1))));
    }
    if (!yb_hnsw_dir.empty() && num_index_shards != 1) {
      return STATUS(InvalidArgument, "yb_hnsw_dir is not supported with multiple index shards");
    }
    if (ann_method == BenchmarkAnnMethod::kPgVector && num_index_shards != 1) {
      return STATUS(InvalidArgument, "pgvector is not supported with multiple index shards");
    }
    // A named index or bigint ids point at existing data, which must not be modified.
    pg_vector.read_only =
        !pg_vector.index.empty() || pg_vector.id_type == PgVectorIdType::kBigint;
    pg_vector.num_connections = std::max(num_indexing_threads, num_query_threads);
    return Status::OK();
  }
};

std::string VectorSourceVerbForLogging(const VectorSourceBase& vector_source, bool finished) {
  if (vector_source.is_file()) {
    return finished ? "Loaded" : "Loading";
  }
  return finished ? "Generating" : "Generated";
}

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

#define PG_VECTOR_ARG(field_name) \
    "pg_" BOOST_PP_STRINGIZE(field_name), \
    po::value(&args.pg_vector.field_name)->default_value(args.pg_vector.field_name)

#define HNSW_OPTION_ARG(field_name) \
    BOOST_PP_STRINGIZE(field_name), \
    po::value(&args.hnsw_options.field_name)->default_value(default_hnsw_options.field_name)

// TODO: we only support bool options where default values are false.
#define HNSW_OPTION_BOOL_ARG(field_name) \
    BOOST_PP_STRINGIZE(field_name), po::bool_switch(&args.hnsw_options.field_name)

  const auto ann_method_help =
      Format("Approximate nearest neighbor search method to use. Possible values: $0.",
             ValidEnumValuesCommaSeparatedForHelp<BenchmarkAnnMethod>());
  const auto distance_kind_help =
      Format("What kind of distance function (metric) to use. Possible values: $0." +
             ValidEnumValuesCommaSeparatedForHelp<DistanceKind>());
  const auto pg_id_type_help =
      Format("PgVector backend: type of the table's `id` column. Possible values: $0. Use Bigint "
             "for tables loaded by other tools whose ids are the input vector indexes. Like "
             "--pg_index, Bigint never modifies the cluster.",
             ValidEnumValuesCommaSeparatedForHelp<PgVectorIdType>());

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
      ("k", po::value(&args.k_values_comma_sep)->default_value(
          args.k_values_comma_sep),
       "A comma-separated list of the possible numbers of results k to retrieve with each "
       "validation query. A separate evaluation is performed for every value of k. If using "
       "precomputed ground truth data, the maximum specified value of k can't be higher than "
       "the precomputed result set size.")
      (OPTIONAL_ARG_FIELD(num_validation_queries),
       "Number of validation queries to execute")
      ("dimensions", po::value(&args.hnsw_options.dimensions),
       "Number of dimensions for automatically generated vectors. Required if no input file "
       "is specified.")
      (OPTIONAL_ARG_FIELD(report_num_keys),
       "Report progress after each batch of this many keys is inserted. 0 would mean that we "
       "should not trigger progress reporting based on the number of keys.")
      (OPTIONAL_ARG_FIELD(report_interval_ms),
       "Report progress after this many milliseconds. 0 would mean that we should not trigger "
       "progress reporting based on the amount of time.")
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
      (HNSW_OPTION_ARG(ef_construction),
       "The number of closest neighbors at each level that are used to determine the candidates "
       "used for constructing the neighborhood of a newly added vertex. Higher values result in "
       "a higher quality graph at the cost of higher indexing time.")
      (HNSW_OPTION_ARG(ef),
       "The expansion factor used during search. At least this many approximate nearest neighbors "
       "are obtained regardless of the user-specified value of k, and the result list is truncated "
       "to the user-specified length k if needed. Higher values of ef should result in higher "
       "recall at the cost of slower retrieval.")
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
      (PG_VECTOR_ARG(host),
       "PgVector backend: YSQL host.")
      (PG_VECTOR_ARG(port),
       "PgVector backend: YSQL port.")
      (PG_VECTOR_ARG(user),
       "PgVector backend: YSQL user.")
      (PG_VECTOR_ARG(password),
       "PgVector backend: YSQL password.")
      (PG_VECTOR_ARG(database),
       "PgVector backend: YSQL database.")
      (PG_VECTOR_ARG(table),
       "PgVector backend: table to load and query. It is reused when it has the expected row "
       "count, `id` and `embedding` column types, and a ready ybhnsw index with the same "
       "distance and m, m0 and ef_construction. Otherwise it is dropped and recreated.")
      (PG_VECTOR_ARG(index),
       "PgVector backend: name of an existing ybhnsw index on --pg_table. When set, the "
       "cluster is never modified and the tool fails if the index is missing or not ready.")
      (PG_VECTOR_ARG(id_type),
       pg_id_type_help.c_str() /* Boost copies the string internally */)
      (PG_VECTOR_ARG(batch_size)->notifier(OptionLowerBound("pg_batch_size", 1)),
       "PgVector backend: number of vectors per COPY batch.")
      (OPTIONAL_ARG_FIELD_WITH_LOWER_BOUND(num_index_shards, 1),
       "For experiments that try to take advantage of a large number of cores, this allows to "
       "create multiple instances of the vector index and insert into them concurrently.")
      (OPTIONAL_ARG_FIELD(yb_hnsw_dir),
       "Directory to save the built index to as YbHnsw files, one per yb_hnsw_variants entry. "
       "Saved indexes are benchmarked by the search benchmark and removed at the end.")
      (OPTIONAL_ARG_FIELD(yb_hnsw_variants),
       "A semicolon-separated list of YbHnsw variants. Each variant is a comma-separated list of "
       "flag=value assignments applied while the variant is saved.")
      (OPTIONAL_ARG_FIELD(block_cache_size_mb),
       "Capacity of the block cache used by YbHnsw indexes, in megabytes. The in-memory index "
       "does not reserve space in it.")
      ("ef_values", po::value(&args.ef_values_comma_sep)->default_value(
          args.ef_values_comma_sep),
       "A comma-separated list of ef values used by the search benchmark. Defaults to ef.")
      (OPTIONAL_ARG_FIELD(num_search_repeats),
       "Number of times every index is measured by the search benchmark, in rotating order. "
       "Reported values are medians across repeats. 0 disables the search benchmark.")
      (OPTIONAL_ARG_FIELD(search_time_ms),
       "Duration of a single search benchmark measurement.");

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

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
using PreVectorIndexFactory = std::function<VectorIndexTraitsPtr<Vector, DistanceResult>(
    const HNSWOptions& options, const hnsw::BlockCachePtr& block_cache)>;

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
      PreVectorIndexFactory<IndexedVector, IndexedDistanceResult> index_factory)
      : args_(args),
        index_pre_factory_(std::move(index_factory)) {
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
      index_pre_factory_ = [pre_factory = index_pre_factory_, num_shards = args_.num_index_shards](
          const HNSWOptions& options, const hnsw::BlockCachePtr& block_cache)
          -> VectorIndexTraitsPtr<IndexedVector, IndexedDistanceResult> {
        return std::make_shared<ShardedVectorIndexTraits<IndexedVector, IndexedDistanceResult>>(
            pre_factory(options, block_cache), num_shards);
      };
    }

    if (!args_.yb_hnsw_dir.empty()) {
      RETURN_NOT_OK(CreateBlockCache());
    }

    vector_index_ = index_pre_factory_(hnsw_options(), block_cache_)->Create(
        FactoryMode::kCreate, StoreVectorPayload::kFalse);

    RETURN_NOT_OK(vector_index_->Reserve(
        num_points_to_insert(),
        std::thread::hardware_concurrency(),
        std::thread::hardware_concurrency(),
        rocksdb::Cache::ReservationMode::kAlways));

    // The pgvector backend may find the dataset already loaded.
    auto* pg_vector_index_info = dynamic_cast<PgVectorIndexInfo*>(vector_index_.get());
    if (pg_vector_index_info && pg_vector_index_info->DataAlreadyExists()) {
      LOG(INFO) << "Skipping build/insert phase (backend reports data is already loaded)";
    } else {
      RETURN_NOT_OK(BuildIndex());
    }

    RETURN_NOT_OK(Validate());

    if (!args_.yb_hnsw_dir.empty()) {
      RETURN_NOT_OK(SaveYbHnswVariants());
    }

    if (args_.num_search_repeats) {
      // With yb_hnsw_dir the index was saved above, so it is immutable and searched lock-free,
      // like the YbHnsw variants.
      benchmark_indexes_.emplace_back("memory", vector_index_);
      RETURN_NOT_OK(BenchmarkSearches());
    }

    RETURN_NOT_OK(RemoveYbHnswVariants());

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
        return CreateRandomFloatVectorSource(num_vectors_to_use, dimensions());
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
    std::optional<size_t> num_provided_top_results;
    for (const auto& ground_truth_vec : ground_truth_by_index) {
      if (!num_provided_top_results.has_value()) {
        num_provided_top_results = ground_truth_vec.size();
        if (*num_provided_top_results < args_.max_k) {
          return STATUS_FORMAT(
              IllegalState,
              "The number of top results provided in the precomputed ground truth ($0) is "
              "less than the maximum number k of results to use for evaluation ($1)",
              *num_provided_top_results, args_.max_k);
        }
      } else if (*num_provided_top_results != ground_truth_vec.size()) {
        return STATUS_FORMAT(
            IllegalState,
            "Inconsistent number of precomputed top results for different queries: $0 vs $1",
            *num_provided_top_results, ground_truth_vec.size());
      }
      loaded_ground_truth_.emplace_back();
      auto& correct_top_k_vertex_ids = loaded_ground_truth_.back();
      correct_top_k_vertex_ids.reserve(args_.max_k);
      used_indexes.clear();
      size_t total_num_vectors = indexed_vector_source_->num_points();
      for (auto idx : ground_truth_vec) {
        static const char* kInvalidMsg =
            "0-based vector index in a ground truth file is out of range";
        SCHECK_GE(idx, 0, IllegalState, kInvalidMsg);
        SCHECK_LT(idx, total_num_vectors, IllegalState, kInvalidMsg);
        if (correct_top_k_vertex_ids.size() < args_.max_k) {
          // Only keep the first k items of the provided list.
          correct_top_k_vertex_ids.push_back(InputVectorIndexToVectorId(idx));
        }
        auto [_, inserted] = used_indexes.insert(idx);
        if (!inserted) {
          return STATUS(
              IllegalState, "The same index is used multiple times in a ground truth result list");
        }
      }
    }
    if (!num_provided_top_results.has_value()) {
      return STATUS(IllegalState, "Could not determine the number of provided top results");
    }

    return Status::OK();
  }

  Status Validate() {
    LOG(INFO) << "Index stats:\n" << vector_index_->IndexStatsStr();
    query_vectors_ = VERIFY_RESULT(query_vector_source_->LoadVectors());

    auto distance_fn = GetDistanceFunction<InputVector, InputDistanceResult>(
        args_.hnsw_options.distance_kind);

    auto vertex_id_to_query_distance_fn =
      [this, &distance_fn](VectorId vertex_id, const InputVector& v) -> InputDistanceResult {
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
    for (auto k : args_.k_values) {
      // The ground truth evaluation is always done in the input coordinate type.
      GroundTruth<InputVector, InputDistanceResult> ground_truth(
          vertex_id_to_query_distance_fn,
          k,
          query_vectors_,
          loaded_ground_truth_,
          args_.validate_ground_truth,
          *reader,
          // The set of vertex ids to recompute ground truth with.
          //
          // In case ground truth is specified as an input file, it must have been computed using
          // all input vectors, so we must use all vectors to validate them. Otherwise, use the set
          // of vectors we've inserted.
          loaded_ground_truth_.empty() ? vertex_ids_to_insert_ : all_vertex_ids_);

      LOG(INFO) << "Validating top k=" << k << " results with " << query_vectors_.size()
                << " queries using " << args_.num_query_threads << " threads "
                << (args_.validate_ground_truth ? "(also recomputing and validating provided"
                                                  " ground truth)"
                                                : "");

      auto start_time = MonoTime::Now();
      auto result = VERIFY_RESULT(ground_truth.EvaluateRecall(args_.num_query_threads));
      auto elapsed_time = MonoTime::Now() - start_time;
      LOG(INFO) << "Validation finished in " << elapsed_time;
      for (size_t j = 0; j < result.size(); ++j) {
        LOG(INFO) << (j + 1) << "-recall @ " << k << ": " << FixedPoint(result[j], 10);
      }
    }
    return Status::OK();
  }

  Status CreateBlockCache() {
    // The cache is for YbHnsw blocks only, so the in-memory index must not reserve space in it.
    RETURN_NOT_OK(SET_FLAG(vector_index_enable_block_cache_reservation, false));
    rocksdb_cache_ = rocksdb::NewLRUCache(
        args_.block_cache_size_mb * 1_MB, /* num_shard_bits= */ 6);
    metric_entity_ = METRIC_ENTITY_server.Instantiate(&metric_registry_, "hnsw_tool");
    block_cache_ = std::make_shared<hnsw::BlockCache>(
        *Env::Default(), MemTracker::FindOrCreateTracker("vector_index_block_cache"),
        metric_entity_, *rocksdb_cache_);
    return Status::OK();
  }

  // Saves the same graph as YbHnsw with each variant, so variants differ only in the layout.
  Status SaveYbHnswVariants() {
    RETURN_NOT_OK(Env::Default()->CreateDirs(args_.yb_hnsw_dir));
    for (const auto& variant : StringSplit(args_.yb_hnsw_variants, ';')) {
      auto path = JoinPathSegments(args_.yb_hnsw_dir, Format("variant$0", yb_hnsw_paths_.size()));
      std::vector<std::pair<std::string, std::string>> old_values;
      auto se = ScopeExit([&old_values] {
        for (const auto& [name, value] : std::views::reverse(old_values)) {
          std::string unused, output_msg;
          flags_internal::SetFlag(
              name, value, flags_internal::SetFlagForce::kTrue, &unused, &output_msg);
        }
      });
      for (const auto& assignment : StringSplit(variant, ',')) {
        auto pos = assignment.find('=');
        SCHECK_NE(pos, std::string::npos, InvalidArgument, Format("Bad flag: $0", assignment));
        auto name = assignment.substr(0, pos);
        std::string old_value, output_msg;
        auto result = flags_internal::SetFlag(
            name, assignment.substr(pos + 1), flags_internal::SetFlagForce::kTrue, &old_value,
            &output_msg);
        SCHECK_EQ(result, flags_internal::SetFlagResult::SUCCESS, InvalidArgument, output_msg);
        old_values.emplace_back(std::move(name), std::move(old_value));
      }
      auto start_time = MonoTime::Now();
      auto index = VERIFY_RESULT(vector_index_->SaveToFile(path));
      SCHECK(index, IllegalState, "Index was not converted to YbHnsw");
      LOG(INFO) << "Saved variant " << variant << " to " << path << " in "
                << MonoTime::Now() - start_time;
      yb_hnsw_paths_.push_back(std::move(path));
      benchmark_indexes_.emplace_back(variant, std::move(index));
    }
    return Status::OK();
  }

  Status RemoveYbHnswVariants() {
    benchmark_indexes_.clear();
    for (const auto& path : yb_hnsw_paths_) {
      RETURN_NOT_OK(Env::Default()->DeleteFile(path));
    }
    yb_hnsw_paths_.clear();
    return Status::OK();
  }

  struct SearchSample {
    double qps;
    double mean_us;
    double p50_us;
    double p99_us;
    double blocks;
    uint64_t read_bytes;
  };

  // Every repeat measures all indexes for each ef and thread count, rotating the order of indexes,
  // so drift of the machine state affects all indexes equally.
  Status BenchmarkSearches() {
    queries_.reserve(query_vectors_.size());
    for (const auto& query : query_vectors_) {
      queries_.push_back(vector_cast<IndexedVector>(query));
    }
    const std::set<size_t> thread_counts{1, args_.num_query_threads};
    const auto num_indexes = benchmark_indexes_.size();

    // The recall pass also warms up the block cache.
    std::map<std::pair<size_t, size_t>, std::string> recalls;
    for (size_t i = 0; i != num_indexes; ++i) {
      for (auto ef : args_.ef_values) {
        recalls[{i, ef}] = VERIFY_RESULT(Recall(*benchmark_indexes_[i].second, ef));
      }
    }

    std::map<std::tuple<size_t, size_t, size_t>, std::vector<SearchSample>> samples;
    for (size_t repeat = 0; repeat != args_.num_search_repeats; ++repeat) {
      for (auto ef : args_.ef_values) {
        for (auto num_threads : thread_counts) {
          for (size_t j = 0; j != num_indexes; ++j) {
            auto i = (j + repeat) % num_indexes;
            auto sample = VERIFY_RESULT(MeasureSearch(
                *benchmark_indexes_[i].second, ef, num_threads));
            VLOG(1) << "Search sample: repeat: " << repeat << ", ef: " << ef << ", threads: "
                    << num_threads << ", mean us: " << sample.mean_us << ", index: "
                    << benchmark_indexes_[i].first;
            samples[{i, ef, num_threads}].push_back(sample);
          }
        }
      }
    }

    std::vector<std::string> lines;
    for (size_t i = 0; i != num_indexes; ++i) {
      for (auto ef : args_.ef_values) {
        for (auto num_threads : thread_counts) {
          const auto& index_samples = samples[{i, ef, num_threads}];
          auto median = [&index_samples](auto field) {
            std::vector<double> values;
            for (const auto& sample : index_samples) {
              values.push_back(sample.*field);
            }
            std::ranges::sort(values);
            return values[values.size() / 2];
          };
          auto [min_mean, max_mean] = std::ranges::minmax(
              index_samples | std::views::transform(&SearchSample::mean_us));
          uint64_t read_bytes = 0;
          for (const auto& sample : index_samples) {
            read_bytes += sample.read_bytes;
          }
          lines.push_back(Format(
              "ef: $0, threads: $1, recall@$2: $3, qps: $4, search us mean/p50/p99: $5, "
              "blocks per search: $6, read MB: $7, index: $8",
              ef, num_threads, args_.max_k, recalls[{i, ef}],
              static_cast<size_t>(median(&SearchSample::qps)),
              Format("$0/$1/$2, mean us min/max: $3/$4",
                     FixedPoint(median(&SearchSample::mean_us), 1),
                     FixedPoint(median(&SearchSample::p50_us), 1),
                     FixedPoint(median(&SearchSample::p99_us), 1),
                     FixedPoint(min_mean, 1), FixedPoint(max_mean, 1)),
              FixedPoint(median(&SearchSample::blocks), 1), read_bytes / 1_MB,
              benchmark_indexes_[i].first));
        }
      }
    }
    LOG(INFO) << "Search benchmark summary, medians of " << args_.num_search_repeats
              << " repeats:\n" << JoinStrings(lines, "\n");
    return Status::OK();
  }

  Result<std::string> Recall(
      const VectorIndexReaderIf<IndexedVector, IndexedDistanceResult>& index, size_t ef) {
    const auto k = args_.max_k;
    const SearchOptions options { .max_num_results = k, .ef = ef };
    size_t overlap = 0;
    for (size_t i = 0; i != queries_.size(); ++i) {
      auto result = VERIFY_RESULT(index.Search(queries_[i], options));
      if (loaded_ground_truth_.empty()) {
        continue;
      }
      const auto& expected = loaded_ground_truth_[i];
      std::unordered_set<VectorId> expected_set(expected.begin(), expected.begin() + k);
      for (const auto& entry : result) {
        overlap += expected_set.contains(entry.vector_id);
      }
    }
    if (loaded_ground_truth_.empty()) {
      return "n/a";
    }
    return FixedPoint(overlap * 1.0 / (queries_.size() * k), 4);
  }

  // Searches with num_threads threads for search_time_ms.
  Result<SearchSample> MeasureSearch(
      const VectorIndexReaderIf<IndexedVector, IndexedDistanceResult>& index, size_t ef,
      size_t num_threads) {
    const SearchOptions options { .max_num_results = args_.max_k, .ef = ef };
    std::vector<std::vector<int64_t>> thread_search_ns(num_threads);
    std::atomic<size_t> next_query{0};
    auto blocks_before = block_cache_ ? block_cache_->metrics().query->value() : 0;
    auto read_before = block_cache_ ? block_cache_->metrics().read->value() : 0;
    auto start_time = MonoTime::Now();
    auto deadline = start_time + MonoDelta::FromMilliseconds(args_.search_time_ms);
    // One item per thread, each searching until the deadline.
    RETURN_NOT_OK(ProcessInParallel(
        num_threads, static_cast<size_t>(0), num_threads,
        [this, &index, &options, &thread_search_ns, &next_query, deadline](size_t t) -> Status {
          auto& search_ns = thread_search_ns[t];
          for (auto now = MonoTime::Now(); now < deadline;) {
            const auto& query =
                queries_[next_query.fetch_add(1, std::memory_order_relaxed) % queries_.size()];
            RETURN_NOT_OK(index.Search(query, options));
            auto finish = MonoTime::Now();
            search_ns.push_back((finish - now).ToNanoseconds());
            now = finish;
          }
          return Status::OK();
        }));
    auto elapsed = MonoTime::Now() - start_time;
    auto blocks = block_cache_ ? block_cache_->metrics().query->value() - blocks_before : 0;
    auto read = block_cache_ ? block_cache_->metrics().read->value() - read_before : 0;
    std::vector<int64_t> search_ns;
    for (const auto& thread_ns : thread_search_ns) {
      search_ns.insert(search_ns.end(), thread_ns.begin(), thread_ns.end());
    }
    SCHECK(!search_ns.empty(), IllegalState, "No searches were executed");
    std::ranges::sort(search_ns);
    const auto n = search_ns.size();
    return SearchSample {
      .qps = n / elapsed.ToSeconds(),
      .mean_us = std::accumulate(search_ns.begin(), search_ns.end(), 0.0) / n / 1000.0,
      .p50_us = search_ns[n / 2] / 1000.0,
      .p99_us = search_ns[n * 99 / 100] / 1000.0,
      .blocks = blocks * 1.0 / n,
      .read_bytes = static_cast<uint64_t>(read),
    };
  }

  size_t max_num_vectors_to_insert() {
    auto n = indexed_vector_source_->num_points();
    if (args_.num_vectors_to_insert) {
      n = std::min(args_.num_vectors_to_insert, n);
    }
    return n;
  }

  void ReportIndexingProgress(MonoTime load_start_time, size_t num_inserted, bool force = false) {
    int64_t elapsed_usec = (MonoTime::Now() - load_start_time).ToMicroseconds();
    bool report_based_on_time = false;
    if (args_.report_interval_ms > 0) {
      auto prev_elapsed_usec = prev_elapsed_usec_.load();
      auto next_report_time = prev_elapsed_usec + make_signed(1000 * args_.report_interval_ms);
      report_based_on_time =
          next_report_time < elapsed_usec &&
          prev_elapsed_usec_.compare_exchange_strong(prev_elapsed_usec, elapsed_usec);
    }
    if (!force && !report_based_on_time &&
        (args_.report_num_keys == 0 || num_inserted == 0 ||
         num_inserted % args_.report_num_keys != 0)) {
      return;
    }
    auto last_report_count = last_progress_report_count_.exchange(num_inserted);
    if (last_report_count == num_inserted) {
      // Already reported progress with this exact total count. Could happen at the end of load.
      // "force" does not affect this -- we avoid duplicate reports anyway.
      return;
    }
    double n_log_n_constant = elapsed_usec * 1.0 / num_inserted / log(num_inserted);
    double elapsed_time_sec = elapsed_usec / 1000000.0;
    size_t remaining_points = max_num_vectors_to_insert() - num_inserted;
    auto keys_per_sec = num_inserted / elapsed_time_sec;
    LOG(INFO) << "n: " << num_inserted << ", "
              << "elapsed time: " << FixedPoint(elapsed_time_sec, 1) << " sec, "
              << "O(n*log(n)) constant: " << n_log_n_constant << ", "
              << "remaining points: " << remaining_points << ", "
              << "keys per second: " << static_cast<size_t>(keys_per_sec) << ", "
              << "time remaining: "
              << FixedPoint(keys_per_sec > 0 ? remaining_points / keys_per_sec : 0, 1)
              << " sec";
    prev_elapsed_usec_ = elapsed_usec;
  }

  Status PrepareInputVectors() {
    size_t num_vectors_to_load = max_num_vectors_to_insert();

    MonoTime load_start_time = MonoTime::Now();
    LOG(INFO) << "Loading vectors from " << indexed_vector_source_->file_path() << "...";
    input_vectors_ = VERIFY_RESULT(indexed_vector_source_->LoadVectors(num_vectors_to_load));
    LOG(INFO) << "Loaded " << input_vectors_.size() << " vectors in "
              << (MonoTime::Now() - load_start_time);

    size_t num_points_used = 0;
    const size_t max_to_insert = max_num_vectors_to_insert();

    for (size_t i = 0; i < max_to_insert; ++i) {
      vertex_ids_to_insert_.push_back(InputVectorIndexToVectorId(i));
      num_points_used++;
    }

    all_vertex_ids_.reserve(input_vectors_.size());
    for (size_t i = 0; i < input_vectors_.size(); ++i) {
      all_vertex_ids_.push_back(InputVectorIndexToVectorId(i));
    }

    if (num_points_used == 0) {
      return STATUS(IllegalState, "Did not find any vectors to add to the index");
    }
    return Status::OK();
  }

  Status InsertOneVector(VectorId vertex_id, MonoTime load_start_time) {
    const auto& v = GetVectorByVertexId(vertex_id);
    Status s = vector_index_->Insert(vertex_id, vector_cast<IndexedVector>(v), Slice());
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

  // For simplicity, vector id is based on the index of input vector.
  static constexpr size_t kMinUintVectorId = 100;

  size_t vertex_id_offset() const {
    // Bigint pgvector tables are keyed by the input vector index itself.
    if (args_.ann_method == BenchmarkAnnMethod::kPgVector &&
        args_.pg_vector.id_type == PgVectorIdType::kBigint) {
      return 0;
    }
    // Start from a small but round number to avoid making assumptions that the 0-based index of
    // a vector in the input file is the same as its vertex id.
    return kMinUintVectorId;
  }

  VectorId InputVectorIndexToVectorId(size_t input_vector_index) {
    CHECK_LE(input_vector_index, indexed_vector_source_->num_points());
    return UIntToVectorId(input_vector_index + vertex_id_offset());
  }

  size_t VertexIdToInputVectorIndex(VectorId vertex_id) {
    auto uint_vertex_id = VectorIdToUInt(vertex_id);
    const auto offset = vertex_id_offset();
    CHECK_GE(uint_vertex_id, offset);
    size_t index = uint_vertex_id - offset;
    CHECK_GE(index, 0);
    CHECK_LT(index, indexed_vector_source_->num_points());
    return index;
  }

  const InputVector& GetVectorByVertexId(VectorId vertex_id) {
    auto vector_index = VertexIdToInputVectorIndex(vertex_id);
    return input_vectors_[vector_index];
  }

  BenchmarkArguments args_;

  // Source from which we take vectors to build the index on.
  std::unique_ptr<VectorSource<InputVector>> indexed_vector_source_;

  // Source for vectors to run validation queries on.
  std::unique_ptr<VectorSource<InputVector>> query_vector_source_;
  std::vector<InputVector> query_vectors_;

  PreVectorIndexFactory<IndexedVector, IndexedDistanceResult> index_pre_factory_;

  // Block cache for YbHnsw indexes, only created when yb_hnsw_dir is specified.
  MetricRegistry metric_registry_;
  MetricEntityPtr metric_entity_;
  std::shared_ptr<rocksdb::Cache> rocksdb_cache_;
  hnsw::BlockCachePtr block_cache_;

  VectorIndexIfPtr<IndexedVector, IndexedDistanceResult> vector_index_;

  // Search benchmark state.
  std::vector<IndexedVector> queries_;
  std::vector<std::pair<std::string, VectorIndexIfPtr<IndexedVector, IndexedDistanceResult>>>
      benchmark_indexes_;
  std::vector<std::string> yb_hnsw_paths_;

  // Atomics used in multithreaded index construction.
  std::atomic<size_t> num_vectors_inserted_{0};  // Total # vectors inserted.
  std::atomic<int64_t> prev_elapsed_usec_{0};  // Previously reported elapsed time in microseconds.
  std::atomic<size_t> last_progress_report_count_{0};  // Last reported progress at this # vectors.

  // Ground truth data loaded from a provided file, not computed on the fly.
  std::vector<std::vector<VectorId>> loaded_ground_truth_;

  std::vector<VectorId> vertex_ids_to_insert_;
  std::vector<VectorId> all_vertex_ids_;

  // Raw input vectors in the order they appeared in the input file.
  std::vector<InputVector> input_vectors_;
};

// Maps the --ann_method values implemented in ann_methods/ to their ANNMethodKind.
template<BenchmarkAnnMethod method>
struct ANNMethodKindFor;

template<>
struct ANNMethodKindFor<BenchmarkAnnMethod::kUsearch> {
  static constexpr auto kValue = ann_methods::ANNMethodKind::kUsearch;
};

template<>
struct ANNMethodKindFor<BenchmarkAnnMethod::kHnswlib> {
  static constexpr auto kValue = ann_methods::ANNMethodKind::kHnswlib;
};

template<BenchmarkAnnMethod ann_method_kind,
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
    PreVectorIndexFactory<IndexedVector, IndexedDistanceResult> pre_index_factory =
        [pg_config = args.pg_vector](
            const HNSWOptions& options, const hnsw::BlockCachePtr& block_cache)
            -> VectorIndexTraitsPtr<IndexedVector, IndexedDistanceResult> {
      if constexpr (ann_method_kind == BenchmarkAnnMethod::kPgVector) {
        return CreatePgVectorIndexTraits<IndexedVector, IndexedDistanceResult>(options, pg_config);
      } else {
        return CHECK_RESULT((ann_methods::ANNMethodTraits<
            ANNMethodKindFor<ann_method_kind>::kValue>::template
                CreateIndexTraits<IndexedVector, IndexedDistanceResult>(block_cache, options)));
      }
    };
    return BenchmarkTool<InputVector, InputDistanceResult, IndexedVector, IndexedDistanceResult>(
        args, pre_index_factory).Execute();
  }
  return std::nullopt;
}

Status BenchmarkExecute(const BenchmarkArguments& args) {
  auto args_copy = args;
  RETURN_NOT_OK(args_copy.Finalize());

  LOG(INFO) << "Distance kind: " << args_copy.hnsw_options.distance_kind;

  // The input coordinate type is based on input file extensions.
  auto input_coordinate_kind = VERIFY_RESULT(DetermineCoordinateKind(args_copy));

  // Determining the right template arguments is a bit tricky. We have a few supported combinations
  // of the ANN method, distance function, input vector type, and the indexed vector type that the
  // method has to use in case the ANN method doesn't support the input vector type. To avoid
  // error-prone code duplication, we use a macro that expands to a bunch of if statements.

#define YB_VECTOR_INDEX_BENCHMARK_SUPPORTED_CASES         \
    /* method, distance,     input type, indexed type */  \
    /* Euclidean distance */                              \
    ((Usearch, L2Squared,    float,      float  ))        \
    ((Usearch, L2Squared,    uint8_t,    float  ))        \
    ((Hnswlib, L2Squared,    float,      float  ))        \
    ((Hnswlib, L2Squared,    uint8_t,    float))          \
    ((PgVector, L2Squared,   float,      float  ))        \
    /* Cosine similarity */                               \
    ((Usearch, Cosine,       float,      float  ))        \
    ((Usearch, Cosine,       uint8_t,    float  ))        \
    ((PgVector, Cosine,      float,      float  ))        \
    /* Inner product */                                   \
    ((Usearch, InnerProduct, float,      float  ))        \
    ((Usearch, InnerProduct, uint8_t,    float  ))        \
    ((Hnswlib, InnerProduct, float,      float  ))        \
    ((Hnswlib, InnerProduct, uint8_t,    float  ))        \
    ((PgVector, InnerProduct, float,     float  ))

#define YB_VECTOR_INDEX_BENCHMARK_HELPER(method, distance_enum_element, input_type, indexed_type) \
    if (auto status = BenchmarkExecuteHelper< \
            BenchmarkAnnMethod::BOOST_PP_CAT(k, method), \
            distance_enum_element, \
            std::vector<input_type>, \
            std::vector<indexed_type>>(args_copy, input_coordinate_kind); \
        status.has_value()) { \
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

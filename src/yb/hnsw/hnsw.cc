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

#include "yb/hnsw/hnsw.h"

#include "usearch/index.hpp"

#include "yb/hnsw/block_writer.h"
#include "yb/hnsw/hnsw_block_cache.h"

#include "yb/util/cast.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"

#include "yb/vector_index/vector_index_if.h"

using namespace yb::size_literals;

DEFINE_RUNTIME_uint64(yb_hnsw_max_block_size, 64_KB,
    "Individual block max size in YbHnsw vector index");

DEFINE_RUNTIME_bool(yb_hnsw_keep_new_blocks_in_cache, false,
    "Whether to keep new generated blocks in cache after YbHnsw index is built.");

#define YB_MISALIGNED_STORE(ptr, type, field, value) \
  MisalignedAssign<decltype(type::field)>(ptr, offsetof(type, field), value)

#define YB_MISALIGNED_PTR(ptr, type, field) \
  MakeMisalignedPtr<decltype(type::field)>(ptr, offsetof(type, field))

DECLARE_bool(TEST_usearch_exact);

namespace yb::hnsw {

struct YbHnswVectorData {
  uint32_t base_layer_neighbors_block;
  uint16_t base_layer_neighbors_begin;
  uint16_t base_layer_neighbors_end;
  uint32_t aux_data_block;
  uint32_t aux_data_begin;
  uint32_t aux_data_end;
  std::byte coordinates[];
};

using VectorNoPtr = MisalignedPtr<const VectorNo>;
using NeighborsType = std::ranges::subrange<VectorNoPtr, VectorNoPtr>;

class YbHnswIndexAdapter {
 public:
  virtual size_t Size() = 0;
  virtual size_t Entry() = 0;
  virtual Header MakeHeader() = 0;

  virtual int16_t NodeLevel(size_t index) = 0;
  virtual vector_index::VectorId NodeKey(size_t index) = 0;
  virtual void NodeCoordinates(size_t index, void* out) = 0;
  virtual NeighborsType Neighbors(size_t index, size_t level) = 0;
  virtual NeighborsType NeighborsBase(size_t index) = 0;

  virtual ~YbHnswIndexAdapter() = default;
};

namespace {

using VectorData = YbHnswVectorData;

template <class Type>
void MisalignedAssign(void* ptr, size_t offset, const Type& value) {
  memcpy(pointer_cast<std::byte*>(ptr) + offset, &value, sizeof(value));
}

template<class T>
MisalignedPtr<const T> MakeMisalignedPtr(const std::byte* ptr, size_t offset) {
  return MisalignedPtr<const T>(ptr + offset);
}

template<class T, class S>
MisalignedPtr<const T> MakeMisalignedPtr(MisalignedPtr<S> ptr, size_t offset) {
  return MisalignedPtr<const T>(ptr.raw() + offset);
}

using NeighborsRefType = uint16_t;

constexpr auto kNeighborsRefSize = sizeof(NeighborsRefType);
constexpr auto kNeighborSize = sizeof(VectorNo);

size_t CalcMaxVectorsPerLayerBlock(size_t max_block_size, size_t connectivity) {
  size_t max_vector_size = kNeighborsRefSize + connectivity * kNeighborSize;
  auto result = std::min<size_t>(
      std::numeric_limits<NeighborsRefType>::max() + 1ULL,
      max_block_size / max_vector_size);
  VLOG_WITH_FUNC(4)
      << "max_block_size: " << max_block_size << ", connectivity: " << connectivity
      << ", max_vector_size: " << max_vector_size << ", result: " << result;
  return result;
}

class YbHnswBuilder {
 public:
  using CoordinateType = float;

  YbHnswBuilder(
      YbHnswIndexAdapter& inspector,
      BlockCache& block_cache, const std::string& path)
      : inspector_(inspector), block_cache_(block_cache), path_(path) {}

  Result<std::pair<FileBlockCachePtr, Header>> Build() {
    header_ = inspector_.MakeHeader();
    PrepareVectors();
    VLOG_WITH_FUNC(4) << "Size: " << inspector_.Size() << ", header: " << header_.ToString();

    auto tmp_path = path_ + ".tmp";
    RETURN_NOT_OK(block_cache_.env().NewWritableFile(tmp_path, &out_));
    RETURN_NOT_OK(BuildAuxilaryDataBlocks());
    RETURN_NOT_OK(BuildNonBaseLayers());
    RETURN_NOT_OK(BuildBaseLayer());
    RETURN_NOT_OK(BuildVectorDataBlocks());
    RETURN_NOT_OK(FlushBlock());
    RETURN_NOT_OK(WriteFooter());
    RETURN_NOT_OK(out_->Close());
    out_.reset();
    RETURN_NOT_OK(block_cache_.env().RenameFile(tmp_path, path_));
    std::unique_ptr<RandomAccessFile> file;
    RETURN_NOT_OK(block_cache_.env().NewRandomAccessFile(path_, &file));
    auto file_block_cache = std::make_unique<FileBlockCache>(
        block_cache_, std::move(file), &builder_);
    return std::pair(std::move(file_block_cache), header_);
  }

 private:
  Status WriteFooter() {
    auto buffer = builder_.MakeFooter(header_);
    return out_->Append(buffer.AsSlice());
  }

  void PrepareVectors() {
    header_.layers.resize(header_.max_level + 1);
    vectors_.reserve(inspector_.Size());
    size_ = inspector_.Size();
    for (size_t index : std::views::iota(static_cast<size_t>(0), inspector_.Size())) {
      const auto level = inspector_.NodeLevel(index);
      DCHECK_BETWEEN(level, 0, header_.layers.size() - 1);
      vectors_.push_back(BuildingVectorData {
        .height = make_unsigned(level),
        .index = index,
        .data = {},
      });
      ++header_.layers[level].size;
    }
    for (size_t level = header_.max_level; level > 0; --level) {
      header_.layers[level - 1].size += header_.layers[level].size;
    }
    // TODO(vector_index) Actually we could do it in O(n)
    std::sort(vectors_.begin(), vectors_.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.height > rhs.height;
    });
  }

  Status BuildVectorDataBlocks() {
    BlockWriter writer;
    size_t left_vectors_in_block = 0;
    size_t left_vectors = vectors_.size();
    header_.vector_data_block = NextBlockId();
    for (auto& v  : vectors_) {
      if (left_vectors_in_block == 0) {
        left_vectors_in_block = std::min(header_.vector_data_amount_per_block, left_vectors);
        left_vectors -= left_vectors_in_block;
        writer = VERIFY_RESULT(StartNewBlock(left_vectors_in_block * header_.vector_data_size));
      }
      --left_vectors_in_block;
      auto* data = writer.Prepare(header_.vector_data_size);
      memcpy(data, &v.data, sizeof(VectorData));
      inspector_.NodeCoordinates(v.index, data + offsetof(VectorData, coordinates));
    }
    return Status::OK();
  }

  Status BuildAuxilaryDataBlocks() {
    slot_map_.resize(size_);

    std::vector<size_t> aux_data_block_sizes;
    {
      VectorNo idx = 0;
      size_t aux_data_size = 0;
      size_t aux_data_block = NextBlockId();
      for (auto& v  : vectors_) {
        size_t vector_aux_data_size = sizeof(vector_index::VectorId);
        if (aux_data_size + vector_aux_data_size > header_.max_block_size) {
          CHECK_GT(aux_data_size, 0);
          aux_data_block_sizes.push_back(aux_data_size);
          aux_data_size = 0;
          ++aux_data_block;
        }

        v.data.aux_data_block = narrow_cast<uint32_t>(aux_data_block);
        v.data.aux_data_begin = narrow_cast<uint32_t>(aux_data_size);
        aux_data_size += vector_aux_data_size;
        v.data.aux_data_end = narrow_cast<uint32_t>(aux_data_size);

        slot_map_[v.index] = idx++;
      }
      aux_data_block_sizes.push_back(aux_data_size);
    }

    header_.entry = slot_map_[inspector_.Entry()];

    VLOG_WITH_FUNC(4) << "aux_data_block_sizes: " << AsString(aux_data_block_sizes);

    size_t current_aux_data_block = 0;
    BlockWriter writer;

    for (const auto& v : vectors_) {
      if (!writer.SpaceLeft()) {
        writer = VERIFY_RESULT(StartNewBlock(aux_data_block_sizes[current_aux_data_block++]));
      }
      vector_index::VectorId key = inspector_.NodeKey(v.index);
      writer.AppendBytes(key.data(), sizeof(vector_index::VectorId));
    }

    return Status::OK();
  }

  Status BuildNonBaseLayers() {
    for (size_t level = header_.max_level; level > 0; --level) {
      auto max_vectors_per_block = header_.max_vectors_per_non_base_block;
      auto& layer = header_.layers[level];

      VLOG_WITH_FUNC(4)
          << "level: " << level << ", max_vectors_per_block: " << max_vectors_per_block
          << ", layer: " << AsString(layer);

      size_t block_neighbors = 0;
      size_t block_start = 0;
      size_t block_stop = std::min(max_vectors_per_block, layer.size);
      layer.block = NextBlockId();
      for (size_t i = 0;;) {
        block_neighbors += inspector_.Neighbors(vectors_[i].index, level).size();
        ++i;
        if (i != block_stop) {
          continue;
        }
        VLOG_WITH_FUNC(4)
            << "level: " << level << ", block_start: " << block_start << ", block_stop: "
            << block_stop << ", block_neighbors: " << block_neighbors;

        auto refs_size = (block_stop - block_start) * kNeighborsRefSize;
        auto refs_writer = VERIFY_RESULT(StartNewBlock(
            refs_size + block_neighbors * kNeighborSize));
        auto neighbors_writer = refs_writer.Split(refs_size);
        for (size_t j = block_start; j != block_stop; ++j) {
          auto vector_neighbors = inspector_.Neighbors(vectors_[j].index, level);

          for (auto neighbor : vector_neighbors) {
            DCHECK_LT(neighbor, size_);;
            neighbors_writer.Append(slot_map_[neighbor]);
          }

          refs_writer.Append<NeighborsRefType>(
              (neighbors_writer.out() - refs_writer.end()) / kNeighborSize);
        }

        if (block_stop == layer.size) {
          layer.last_block_vectors_amount = block_stop - block_start;
          break;
        }
        ++layer.last_block_index;
        block_start = block_stop;
        block_stop = std::min(block_stop + max_vectors_per_block, layer.size);
        block_neighbors = 0;
      }
    }

    return Status::OK();
  }

  Status BuildBaseLayer() {
    auto& layer = header_.layers[0];

    VLOG_WITH_FUNC(4) << AsString(layer);

    layer.block = NextBlockId();
    VectorNo block_start = 0;
    size_t block_neighbors = 0;
    for (VectorNo i = 0; i != layer.size; ++i) {
      size_t vector_neighbors = inspector_.NeighborsBase(vectors_[i].index).size();
      block_neighbors += vector_neighbors;
      if (block_neighbors * kNeighborSize > header_.max_block_size) {
        RETURN_NOT_OK(FlushBaseLayer(block_start, i, block_neighbors - vector_neighbors));
        block_neighbors = vector_neighbors;
        block_start = i;
      }
    }
    return FlushBaseLayer(block_start, narrow_cast<VectorNo>(layer.size), block_neighbors);
  }

  Status FlushBaseLayer(VectorNo start, VectorNo stop, size_t block_neighbors) {
    DCHECK_NE(start, stop); // block should not be empty.
    auto block_id = NextBlockId();
    auto writer = VERIFY_RESULT(StartNewBlock(
        std::max<size_t>(block_neighbors, 1) * kNeighborSize));
    size_t neighbors_offset = 0;
    for (size_t j = start; j != stop; ++j) {
      auto neighbors = inspector_.NeighborsBase(vectors_[j].index);
      auto& data = vectors_[j].data;
      data.base_layer_neighbors_block = narrow_cast<uint32_t>(block_id);
      data.base_layer_neighbors_begin = neighbors_offset;
      neighbors_offset += neighbors.size();
      data.base_layer_neighbors_end = neighbors_offset;
      for (auto neighbor : neighbors) {
        DCHECK_LT(neighbor, size_);;
        writer.Append(slot_map_[neighbor]);
      }
    }
    if (block_neighbors == 0) {
      writer.Append(static_cast<VectorNo>(0));
    }
    return Status::OK();
  }

  size_t NextBlockId() const {
    return num_blocks_;
  }

  Result<BlockWriter> StartNewBlock(size_t size) {
    RETURN_NOT_OK(FlushBlock());
    current_block_.Allocate(size);
    ++num_blocks_;
    return BlockWriter(current_block_);
  }

  Status FlushBlock() {
    if (num_blocks_ == 0) {
      return Status::OK();
    }
    RETURN_NOT_OK(out_->Append(Slice(current_block_.AsSlice())));
    if (!FLAGS_yb_hnsw_keep_new_blocks_in_cache) {
      current_block_.data.reset();
    }
    builder_.Add(std::move(current_block_));
    return Status::OK();
  }

  struct BuildingVectorData {
    size_t height;
    size_t index;
    YbHnswVectorData data;
  };

  YbHnswIndexAdapter& inspector_;
  BlockCache& block_cache_;
  const std::string path_;
  Header header_;
  size_t num_blocks_ = 0;
  DataBlock current_block_;
  FileBlockCacheBuilder builder_;
  std::unique_ptr<WritableFile> out_;
  size_t size_ = 0;
  std::vector<BuildingVectorData> vectors_;
  std::vector<VectorNo> slot_map_;
};

void InitVectorDataAmountPerBlock(Header& header, size_t size) {
  // Approximate calculation. Goal is even distribution of vectors across blocks.
  size_t vector_headers_blocks = ceil_div(header.vector_data_size * size, header.max_block_size);
  header.vector_data_amount_per_block = std::max<size_t>(1, size / vector_headers_blocks);
  while (header.vector_data_amount_per_block > 1 &&
         header.vector_data_amount_per_block * header.vector_data_size > header.max_block_size) {
    --header.vector_data_amount_per_block;
  }
}

} // namespace

class YbHnswUsearchIndexAdapter : public YbHnswIndexAdapter {
 public:
  explicit YbHnswUsearchIndexAdapter(
      std::reference_wrapper<const unum::usearch::index_dense_gt<vector_index::VectorId>> index)
      : index_(index) {}

  size_t Size() override {
    return index_.size();
  }

  size_t Entry() override {
    return index_.impl().entry_slot();
  }

  Header MakeHeader() override {
    Header result;
    result.max_block_size = FLAGS_yb_hnsw_max_block_size;
    result.dimensions = index_.dimensions();
    result.vector_data_size = index_.bytes_per_vector() + sizeof(VectorData);
    result.entry = narrow_cast<VectorNo>(index_.impl().entry_slot());
    InitVectorDataAmountPerBlock(result, index_.size());
    result.max_level = index_.impl().max_level();
    const auto& index_config = index_.config();
    result.config.connectivity_base = index_config.connectivity_base;
    result.config.connectivity = index_config.connectivity;

    result.max_vectors_per_non_base_block = CalcMaxVectorsPerLayerBlock(
        result.max_block_size, result.config.connectivity);

    return result;
  }

  int16_t NodeLevel(size_t index) override {
    return index_.impl().node_at_(index).level();
  }

  vector_index::VectorId NodeKey(size_t index) override {
    return index_.impl().node_at_(index).key();
  }

  void NodeCoordinates(size_t index, void* out) override {
    index_.get(NodeKey(index), pointer_cast<float*>(out));
  }

  NeighborsType Neighbors(size_t index, size_t level) override {
    auto neighbors = index_.impl().neighbors_(index_.impl().node_at_(index), level);
    return NeighborsType(NodePtr(neighbors.cbegin()), NodePtr(neighbors.cend()));
  }

  NeighborsType NeighborsBase(size_t index) override {
    auto neighbors = index_.impl().neighbors_base_(index_.impl().node_at_(index));
    return NeighborsType(NodePtr(neighbors.cbegin()), NodePtr(neighbors.cend()));
  }

 private:
  static VectorNoPtr NodePtr(
      unum::usearch::misaligned_ptr_gt<const unum::usearch::default_slot_t> ptr) {
    return VectorNoPtr(pointer_cast<const std::byte*>((*ptr).ptr()));
  }

  const unum::usearch::index_dense_gt<vector_index::VectorId>& index_;
};

SearchCacheScope::SearchCacheScope(SearchCache& cache, const YbHnsw& hnsw) : cache_(cache) {
  cache.Bind(hnsw.header_, *hnsw.file_block_cache_);
}

YbHnsw::YbHnsw(MetricPtr&& metric, BlockCachePtr block_cache)
    : metric_(std::move(metric)), block_cache_(std::move(block_cache)) {
}

YbHnsw::~YbHnsw() = default;

Status YbHnsw::Import(
    const unum::usearch::index_dense_gt<vector_index::VectorId>& index, const std::string& path) {
  YbHnswUsearchIndexAdapter inspector(index);
  YbHnswBuilder builder(inspector, *block_cache_, path);
  std::tie(file_block_cache_, header_) = VERIFY_RESULT(builder.Build());
  return Status::OK();
}

class YbHnswHnswlibIndexAdapter : public YbHnswIndexAdapter {
 public:
  explicit YbHnswHnswlibIndexAdapter(
      std::reference_wrapper<const HnswlibIndex<YbHnsw::DistanceType>> index)
      : index_(index) {}

  size_t Size() override {
    return index_.getCurrentElementCount();
  }

  size_t Entry() override {
    return index_.enterpoint_node_;
  }

  Header MakeHeader() override {
    Header result;
    result.max_block_size = FLAGS_yb_hnsw_max_block_size;
    result.dimensions = *static_cast<size_t*>(index_.dist_func_param_);
    result.vector_data_size = index_.data_size_ + sizeof(VectorData);
    InitVectorDataAmountPerBlock(result, index_.getCurrentElementCount());
    result.max_level = index_.getMaxLevel();
    result.config.connectivity_base = index_.maxM_;
    result.config.connectivity = index_.maxM0_;

    result.max_vectors_per_non_base_block = CalcMaxVectorsPerLayerBlock(
        result.max_block_size, result.config.connectivity);

    return result;
  }

  int16_t NodeLevel(size_t index) override {
    return index_.element_levels_[index];
  }

  vector_index::VectorId NodeKey(size_t index) override {
    return index_.getExternalLabel(CastIndex(index));
  }

  void NodeCoordinates(size_t index, void* out) override {
    memcpy(out, index_.getDataByInternalId(CastIndex(index)), index_.data_size_);
  }

  NeighborsType Neighbors(size_t index, size_t level) override {
    return MakeNeighbors(index_.get_linklist(CastIndex(index), narrow_cast<int>(level)));
  }

  NeighborsType NeighborsBase(size_t index) override {
    return MakeNeighbors(index_.get_linklist0(CastIndex(index)));
  }

 private:
  static hnswlib::tableint CastIndex(size_t index) {
    return narrow_cast<hnswlib::tableint>(index);
  }

  static NeighborsType MakeNeighbors(const hnswlib::linklistsizeint* list) {
    // Hnswlib stores list as a size in 2 bytes + list content with offset of 4 bytes.
    auto size = *pointer_cast<const uint16_t*>(list);
    auto start = pointer_cast<const std::byte*>(list + 1);
    return NeighborsType(VectorNoPtr(start), VectorNoPtr(start + size * sizeof(VectorNo)));
  }

  const HnswlibIndex<YbHnsw::DistanceType>& index_;
};

Status YbHnsw::Import(
    const HnswlibIndex<DistanceType>& index,
    const std::string& path) {
  YbHnswHnswlibIndexAdapter inspector(index);
  YbHnswBuilder builder(inspector, *block_cache_, path);
  std::tie(file_block_cache_, header_) = VERIFY_RESULT(builder.Build());
  return Status::OK();
}

Status YbHnsw::Init(const std::string& path) {
  std::unique_ptr<RandomAccessFile> file;
  RETURN_NOT_OK(block_cache_->env().NewRandomAccessFile(path, &file));
  file_block_cache_ = std::make_unique<FileBlockCache>(*block_cache_, std::move(file));
  header_ = VERIFY_RESULT(file_block_cache_->Load());
  return Status::OK();
}

YbHnsw::SearchResult YbHnsw::Search(
    const std::byte* query_vector, const vector_index::SearchOptions& options,
    YbHnswSearchContext& context) const {
  SearchCacheScope scs(context.search_cache, *this);
  context.search_cache.Bind(header_, *file_block_cache_);
  auto se = ScopeExit([&context] { context.search_cache.Release(); });
  if (PREDICT_FALSE(FLAGS_TEST_usearch_exact)) {
    SearchExact(query_vector, options, context);
  } else {
    auto [best_vector, best_dist] = SearchInNonBaseLayers(
        query_vector, context.search_cache);
    SearchInBaseLayer(query_vector, best_vector, best_dist, options, context);
  }
  return MakeResult(options.max_num_results, context);
}

YbHnsw::SearchResult YbHnsw::MakeResult(size_t max_results, YbHnswSearchContext& context) const {
  auto& top = context.top.data();
  std::sort(top.begin(), top.end());
  top.resize(std::min(top.size(), max_results));

  SearchResult result;
  result.reserve(top.size());
  for (auto [distance, vector] : top) {
    result.emplace_back(context.search_cache.GetVectorData(vector), distance);
  }
  return result;
}

std::pair<VectorNo, YbHnsw::DistanceType> YbHnsw::SearchInNonBaseLayers(
    const std::byte* query_vector, SearchCache& cache) const {
  auto best_vector = header_.entry;
  auto best_dist = Distance(query_vector, best_vector, cache);
  VLOG_WITH_FUNC(4) << "best_vector: " << best_vector << ", best_dist: " << best_dist;

  for (auto level = header_.max_level; level > 0;) {
    auto updated = false;
    VLOG_WITH_FUNC(4)
        << "level: " << level << ", best_vector: " << best_vector << ", best_dist: " << best_dist;
    for (auto neighbor : cache.GetNeighborsInNonBaseLayer(level, best_vector)) {
      auto neighbor_dist = Distance(query_vector, neighbor, cache);
      VLOG_WITH_FUNC(4)
          << "level: " << level << ", neighbor: " << neighbor << ", neighbor_dist: "
          << neighbor_dist;
      if (neighbor_dist < best_dist) {
        best_vector = neighbor;
        best_dist = neighbor_dist;
        updated = true;
      }
    }
    if (!updated) {
      --level;
    }
  }
  return {best_vector, best_dist};
}

void YbHnsw::SearchInBaseLayer(
    const std::byte* query_vector, VectorNo best_vector, DistanceType best_dist,
    const vector_index::SearchOptions& options, YbHnswSearchContext& context) const {
  auto& top = context.top;
  top.clear();
  auto& extra_top = context.extra_top;
  extra_top.clear();
  auto& visited = context.visited;
  visited.clear();
  auto& next = context.next;
  next.clear();
  auto& cache = context.search_cache;

  // We will visit at least entry vector and its neighbors.
  // So could use the following as initial capacity for visited.
  visited.reserve(header_.config.connectivity_base + 1u);

  auto top_limit = options.max_num_results;
  auto extra_top_limit = std::max<size_t>(
      options.ef, options.max_num_results) - options.max_num_results;
  next.push({best_dist, best_vector});
  if (!options.filter || options.filter(cache.GetVectorData(best_vector))) {
    top.push({best_dist, best_vector});
  }
  visited.set(best_vector);

  while (!next.empty()) {
    auto [dist, vector] = next.top();
    VLOG_WITH_FUNC(4) << "vector: " << vector << ", dist: " << dist << ", best_dist: " << best_dist;
    if (dist > best_dist && extra_top.size() == extra_top_limit && top.size() == top_limit) {
      break;
    }
    next.pop();
    auto neighbors = cache.GetNeighborsInBaseLayer(vector);
    visited.reserve(visited.size() + std::ranges::size(neighbors));

    for (auto neighbor : neighbors) {
      if (visited.set(neighbor)) {
        continue;
      }
      auto neighbor_dist = Distance(query_vector, neighbor, cache);

      if (top.size() < top_limit || extra_top.size() < extra_top_limit ||
          neighbor_dist < best_dist) {
        next.push({neighbor_dist, neighbor});
        if (!options.filter || options.filter(cache.GetVectorData(neighbor))) {
          if (top.size() == top_limit) {
            auto extra_push = top.top().first;
            if (neighbor_dist < extra_push) {
              top.replace_top({neighbor_dist, neighbor});
            } else {
              extra_push = neighbor_dist;
            }
            if (extra_top.size() < extra_top_limit) {
              extra_top.push(extra_push);
            } else if (extra_top_limit && extra_push < extra_top.top()) {
              extra_top.replace_top(extra_push);
            }
          } else {
            top.push({neighbor_dist, neighbor});
          }
          best_dist = extra_top.empty() ? top.top().first : extra_top.top();
        }
      }
    }
  }
}

void YbHnsw::SearchExact(
    const std::byte* query_vector, const vector_index::SearchOptions& options,
    YbHnswSearchContext& context) const {
  auto& cache = context.search_cache;
  auto& top = context.top;
  top.clear();
  for (size_t i = 0, size = header_.layers.front().size; i != size; ++i) {
    if (options.filter && !options.filter(cache.GetVectorData(i))) {
      continue;
    }
    auto dist = Distance(query_vector, i, cache);
    if (top.size() < options.max_num_results) {
      top.push({dist, i});
    } else if (dist < top.top().first) {
      top.replace_top({dist, i});
    }
  }
}

YbHnsw::DistanceType YbHnsw::Distance(const std::byte* lhs, const std::byte* rhs) const {
  return metric_->Distance(lhs, rhs);
}

YbHnsw::DistanceType YbHnsw::Distance(
    const std::byte* lhs, size_t vector, SearchCache& cache) const {
  return Distance(lhs, cache.CoordinatesPtr(vector));
}

boost::iterator_range<MisalignedPtr<const YbHnsw::CoordinateType>> YbHnsw::MakeCoordinates(
    const std::byte* ptr) const {
  auto start = MisalignedPtr<const CoordinateType>(ptr);
  return boost::make_iterator_range(start, start + header_.dimensions);
}

boost::iterator_range<MisalignedPtr<const YbHnsw::CoordinateType>> YbHnsw::Coordinates(
    size_t vector, SearchCache& cache) const {
  return MakeCoordinates(cache.CoordinatesPtr(vector));
}

const Header& YbHnsw::header() const {
  return header_;
}

const std::byte* SearchCache::Data(size_t index) {
  auto& block = blocks_[index];
  if (block) {
    return block;
  }
  auto data = CHECK_RESULT(file_block_cache_->Take(index));
  used_blocks_.push_back(index);
  return block = data;
}

void SearchCache::Bind(std::reference_wrapper<const Header> header, FileBlockCache& cache) {
  DCHECK(used_blocks_.empty());
  header_ = &header.get();
  file_block_cache_ = &cache;
  blocks_.resize(cache.size());
}

void SearchCache::Release() {
  for (auto block : used_blocks_) {
    blocks_[block] = nullptr;
    file_block_cache_->Release(block);
  }
  used_blocks_.clear();
}

boost::iterator_range<MisalignedPtr<const VectorNo>> SearchCache::GetNeighborsInBaseLayer(
    size_t vector) {
  auto vector_data = VectorHeader(vector);
  auto base_ptr = Data(*YB_MISALIGNED_PTR(vector_data, VectorData, base_layer_neighbors_block));
  auto begin = *YB_MISALIGNED_PTR(vector_data, VectorData, base_layer_neighbors_begin);
  auto end = *YB_MISALIGNED_PTR(vector_data, VectorData, base_layer_neighbors_end);
  return boost::make_iterator_range(
      MisalignedPtr<const VectorNo>(base_ptr + begin * kNeighborSize),
      MisalignedPtr<const VectorNo>(base_ptr + end * kNeighborSize));
}

MisalignedPtr<const VectorData> SearchCache::VectorHeader(size_t vector) {
  return MisalignedPtr<const VectorData>(BlockPtr(
      header_->vector_data_block, header_->vector_data_amount_per_block, vector,
      header_->vector_data_size));
}

boost::iterator_range<MisalignedPtr<const VectorNo>> SearchCache::GetNeighborsInNonBaseLayer(
    size_t level, size_t vector) {
  auto max_vectors_per_block = header_->max_vectors_per_non_base_block;
  auto block_index = vector / max_vectors_per_block;
  vector %= max_vectors_per_block;
  auto& layer = header_->layers[level];
  auto base_ptr = Data(layer.block + block_index);
  auto finish = Load<NeighborsRefType, HnswEndian>(base_ptr + vector * kNeighborsRefSize);
  auto start = vector > 0
      ? Load<NeighborsRefType, HnswEndian>(base_ptr + (vector - 1) * kNeighborsRefSize) : 0;
  size_t refs_count;
  if (block_index == layer.last_block_index) {
    refs_count = layer.last_block_vectors_amount;
  } else {
    refs_count = max_vectors_per_block;
  }
  base_ptr += refs_count * kNeighborsRefSize;

  auto result = boost::make_iterator_range(
      MisalignedPtr<const VectorNo>(base_ptr + start * kNeighborSize),
      MisalignedPtr<const VectorNo>(base_ptr + finish * kNeighborSize));
  VLOG_WITH_FUNC(4) << AsString(result);
  return result;
}

const std::byte* SearchCache::BlockPtr(
    size_t block, size_t entries_per_block, size_t entry, size_t entry_size) {
  block += entry / entries_per_block;
  entry %= entries_per_block;
  return Data(block) + entry * entry_size;
}

Slice SearchCache::GetVectorDataSlice(size_t vector) {
  auto vector_data = VectorHeader(vector);
  auto base_ptr = Data(
      *YB_MISALIGNED_PTR(vector_data, VectorData, aux_data_block));
  auto begin = *YB_MISALIGNED_PTR(vector_data, VectorData, aux_data_begin);
  auto end = *YB_MISALIGNED_PTR(vector_data, VectorData, aux_data_end);
  return Slice(base_ptr + begin, base_ptr + end);
}

vector_index::VectorId SearchCache::GetVectorData(size_t vector) {
  return vector_index::TryFullyDecodeVectorId(GetVectorDataSlice(vector));
}

const std::byte* SearchCache::CoordinatesPtr(size_t vector) {
  return VectorHeader(vector).raw() + offsetof(VectorData, coordinates);
}

HnswDistanceType UsearchMetric::Distance(
    const std::byte* lhs, const std::byte* rhs) {
  using unum::usearch::byte_t;
  return impl_(pointer_cast<const byte_t*>(lhs), pointer_cast<const byte_t*>(rhs));
}

HnswDistanceType HnswlibMetric::Distance(
    const std::byte* lhs, const std::byte* rhs) {
  return func_(lhs, rhs, &dimensions_);
}

}  // namespace yb::hnsw


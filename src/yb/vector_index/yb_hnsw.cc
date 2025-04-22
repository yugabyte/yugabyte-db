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

#include "yb/vector_index/yb_hnsw.h"

#include "usearch/index.hpp"

#include "yb/util/cast.h"
#include "yb/util/flags.h"
#include "yb/util/size_literals.h"

using namespace yb::size_literals;

DEFINE_RUNTIME_uint64(yb_hnsw_max_block_size, 64_KB,
    "Individual block max size in YbHnsw vector index");

#define YB_MISALIGNED_STORE(ptr, type, field, value) \
  MisalignedAssign<decltype(type::field)>(ptr, offsetof(type, field), value)

#define YB_MISALIGNED_PTR(ptr, type, field) \
  MakeMisalignedPtr<decltype(type::field)>(ptr, offsetof(type, field))

namespace yb::vector_index {

struct YbHnswVectorData {
  uint32_t base_layer_neighbors_block;
  uint16_t base_layer_neighbors_begin;
  uint16_t base_layer_neighbors_end;
  uint32_t aux_data_block;
  uint32_t aux_data_begin;
  uint32_t aux_data_end;
  std::byte coordinates[];
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
constexpr auto kNeighborSize = sizeof(VectorIndex);

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

class BlockWriter {
 public:
  BlockWriter() = default;
  BlockWriter(std::byte* out, std::byte* end) : out_(out), end_(end) {
    DCHECK_LE(out_, end_);
  }

  explicit BlockWriter(YbHnswDataBlock& block)
      : BlockWriter(block.data(), block.data() + block.size()) {}

  void operator=(BlockWriter&& rhs) {
    DCHECK_EQ(out_, end_);
    out_ = rhs.out_;
    end_ = rhs.end_;
    rhs.out_ = rhs.end_;
  }

  ~BlockWriter() {
    DCHECK_EQ(out_, end_);
  }

  std::byte* out() const {
    return out_;
  }

  std::byte* end() const {
    return end_;
  }

  size_t SpaceLeft() const {
    return end_ - out_;
  }

  BlockWriter Split(size_t size) {
    auto old_end = end_;
    end_ = out_ + size;
    DCHECK_LE(end_, old_end);
    return BlockWriter(end_, old_end);
  }

  std::byte* Prepare(size_t size) {
    auto result = out_;
    out_ += size;
    DCHECK_LE(out_, end_);
    return result;
  }

  template <class Value>
  void Append(Value value) {
    Store<Value, YbHnswEndian>(out_, value);
    out_ += sizeof(Value);
  }

  template <class... Args>
  void AppendBytes(Args&&... args) {
    Slice slice(std::forward<Args>(args)...);
    memcpy(out_, slice.data(), slice.size());
    out_ += slice.size();
  }

 private:
  std::byte* out_ = nullptr;
  std::byte* end_ = nullptr;
};

class YbHnswBuilder : public YbHnswStorage {
 public:
  explicit YbHnswBuilder(const unum::usearch::index_dense_gt<VectorId>& index) : index_(index) {}

  void Build() {
    header_.Init(index_);
    PrepareVectors();
    VLOG_WITH_FUNC(4)
        << "Size: " << index_.size() << ", max level: " << header_.max_level << ", dimensions: "
        << header_.dimensions << ", connectivity: " << header_.config.connectivity
        << ", connectivity_base: " << header_.config.connectivity_base
        << ", expansion_search: " << header_.config.expansion_search << ", layers: "
        << AsString(header_.layers);

    BuildVectorDataBlocksAndBuildAuxilaryDataBlocks();
    BuildNonBaseLayers();
    BuildBaseLayer();
  }
 private:
  auto& impl() {
    return index_.impl();
  }

  void PrepareVectors() {
    header_.layers.resize(header_.max_level + 1);
    vectors_.reserve(index_.size());
    max_slot_ = 0;
    for (const auto& item : boost::make_iterator_range(index_.cbegin(), index_.cend())) {
      auto slot = get_slot(item);
      auto node = impl().node_at_(slot);
      std::int16_t level = node.level();
      vectors_.emplace_back(level, slot);
      max_slot_ = std::max(max_slot_, slot);
      ++header_.layers[level].size;
    }
    for (size_t level = header_.max_level; level > 0; --level) {
      header_.layers[level - 1].size += header_.layers[level].size;
    }
    // TODO(vector_index) Actually we could do it in O(n)
    std::sort(vectors_.begin(), vectors_.end(), std::greater<void>());
  }

  void BuildVectorDataBlocksAndBuildAuxilaryDataBlocks() {
    slot_map_.resize(max_slot_ + 1);

    std::vector<size_t> aux_data_block_sizes;
    {
      BlockWriter writer;
      VectorIndex idx = 0;
      size_t left_vectors_in_block = 0;
      size_t left_vectors = vectors_.size();
      header_.vector_data_block = NextBlockId();
      size_t aux_data_size = 0;
      size_t aux_data_block =
          header_.vector_data_block +
          ceil_div(vectors_.size(), header_.vector_data_amount_per_block);
      // Please note that term height comes from HNSW.
      // So node height is max level where it is present.
      for (auto [height, slot] : vectors_) {
        if (left_vectors_in_block == 0) {
          left_vectors_in_block = std::min(header_.vector_data_amount_per_block, left_vectors);
          left_vectors -= left_vectors_in_block;
          writer = AllocateBlock(left_vectors_in_block * header_.vector_data_size);
        }
        --left_vectors_in_block;
        auto node = impl().node_at_(slot);
        VectorId key = node.key();

        size_t vector_aux_data_size = sizeof(VectorId);
        if (aux_data_size + vector_aux_data_size > header_.max_block_size) {
          CHECK_GT(aux_data_size, 0);
          aux_data_block_sizes.push_back(aux_data_size);
          aux_data_size = 0;
          ++aux_data_block;
        }

        auto* data = writer.Prepare(header_.vector_data_size);
        YB_MISALIGNED_STORE(
            data, VectorData, aux_data_block, narrow_cast<uint32_t>(aux_data_block));
        YB_MISALIGNED_STORE(data, VectorData, aux_data_begin, narrow_cast<uint32_t>(aux_data_size));
        aux_data_size += vector_aux_data_size;
        YB_MISALIGNED_STORE(data, VectorData, aux_data_end, narrow_cast<uint32_t>(aux_data_size));

        index_.get(key, pointer_cast<CoordinateType*>(data + offsetof(VectorData, coordinates)));
        slot_map_[slot] = idx++;
      }
      aux_data_block_sizes.push_back(aux_data_size);
    }

    header_.entry = slot_map_[impl().entry_slot()];

    VLOG_WITH_FUNC(4) << "aux_data_block_sizes: " << AsString(aux_data_block_sizes);

    size_t current_aux_data_block = 0;
    BlockWriter writer;

    for (auto [_, slot] : vectors_) {
      if (!writer.SpaceLeft()) {
        writer = AllocateBlock(aux_data_block_sizes[current_aux_data_block++]);
      }
      VectorId key = impl().node_at_(slot).key();
      writer.AppendBytes(key.data(), sizeof(VectorId));
    }
  }

  void BuildNonBaseLayers() {
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
        block_neighbors += impl().neighbors_(impl().node_at_(vectors_[i].second), level).size();
        ++i;
        if (i != block_stop) {
          continue;
        }
        VLOG_WITH_FUNC(4)
            << "level: " << level << ", block_start: " << block_start << ", block_stop: "
            << block_stop << ", block_neighbors: " << block_neighbors;

        auto refs_size = (block_stop - block_start) * kNeighborsRefSize;
        auto refs_writer = AllocateBlock(refs_size + block_neighbors * kNeighborSize);
        auto neighbors_writer = refs_writer.Split(refs_size);
        for (size_t j = block_start; j != block_stop; ++j) {
          auto node = impl().node_at_(vectors_[j].second);
          auto vector_neighbors = impl().neighbors_(node, level);

          for (auto neighbor : vector_neighbors) {
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
  }

  void BuildBaseLayer() {
    auto& layer = header_.layers[0];
    layer.block = NextBlockId();
    VectorIndex block_start = 0;
    size_t block_neighbors = 0;
    for (VectorIndex i = 0; i != layer.size; ++i) {
      size_t vector_neighbors = impl().neighbors_base_(impl().node_at_(vectors_[i].second)).size();
      block_neighbors += vector_neighbors;
      if (block_neighbors * kNeighborSize > header_.max_block_size) {
        FlushBaseLayer(block_start, i, block_neighbors - vector_neighbors);
        block_neighbors = vector_neighbors;
        block_start = i;
      }
    }
    FlushBaseLayer(block_start, narrow_cast<VectorIndex>(layer.size), block_neighbors);
  }

  void FlushBaseLayer(VectorIndex start, VectorIndex stop, size_t block_neighbors) {
    DCHECK_NE(start, stop); // block should not be empty.
    auto block_id = NextBlockId();
    auto writer = AllocateBlock(block_neighbors * kNeighborSize);
    size_t neighbors_offset = 0;
    for (size_t j = start; j != stop; ++j) {
      auto block_index = header_.vector_data_block + j / header_.vector_data_amount_per_block;
      size_t vector_offset = (j % header_.vector_data_amount_per_block) * header_.vector_data_size;
      MisalignedPtr<VectorData> vector_data(blocks_[block_index].data() + vector_offset);
      auto neighbors = impl().neighbors_base_(impl().node_at_(vectors_[j].second));
      YB_MISALIGNED_STORE(
          vector_data.raw(), VectorData, base_layer_neighbors_block,
          static_cast<uint32_t>(block_id));
      YB_MISALIGNED_STORE(
          vector_data.raw(), VectorData, base_layer_neighbors_begin, neighbors_offset);
      neighbors_offset += neighbors.size();
      YB_MISALIGNED_STORE(
          vector_data.raw(), VectorData, base_layer_neighbors_end, neighbors_offset);
      for (auto neighbor : neighbors) {
        writer.Append(slot_map_[neighbor]);
      }
    }
  }

  size_t NextBlockId() const {
    return blocks_.size();
  }

  BlockWriter AllocateBlock(size_t size) {
    blocks_.emplace_back(size);
    return BlockWriter(blocks_.back());
  }

  std::byte* BlockData(size_t index) {
    return blocks_[index].data();
  }

  const unum::usearch::index_dense_gt<VectorId>& index_;
  size_t max_slot_ = 0;
  std::vector<std::pair<size_t, size_t>> vectors_;
  std::vector<VectorIndex> slot_map_;
};

} // namespace

std::string YbHnswLayerInfo::ToString() const {
  return YB_STRUCT_TO_STRING(size, block, last_block_index, last_block_vectors_amount);
}

void YbHnswHeader::Init(const unum::usearch::index_dense_gt<VectorId>& index) {
  max_block_size = FLAGS_yb_hnsw_max_block_size;
  dimensions = index.dimensions();
  vector_data_size = index.bytes_per_vector() + sizeof(VectorData);
  size_t vector_headers_blocks = ceil_div(vector_data_size * index.size(), max_block_size);
  // Approximate calculation. Goal is even distribution of vectors across blocks.
  vector_data_amount_per_block = std::max<size_t>(1, index.size() / vector_headers_blocks);
  while (vector_data_amount_per_block > 1 &&
         vector_data_amount_per_block * vector_data_size > max_block_size) {
    --vector_data_amount_per_block;
  }
  max_level = index.impl().max_level();
  config = index.config();

  max_vectors_per_non_base_block = CalcMaxVectorsPerLayerBlock(max_block_size, config.connectivity);
}

using YbHnswDataBlock = std::vector<std::byte>;
using YbHnswEndian = LittleEndian;
using YbHnswDistanceType = float;

void YbHnsw::Import(const unum::usearch::index_dense_gt<VectorId>& index) {
  YbHnswBuilder builder(index);
  builder.Build();
  Take(std::move(builder));
}

YbHnsw::SearchResult YbHnsw::Search(
    const std::byte* query_vector, size_t max_results, const VectorFilter& filter,
    YbHnswSearchContext& context) const {
  auto [best_vector, best_dist] = SearchInNonBaseLayers(query_vector);
  SearchInBaseLayer(query_vector, best_vector, best_dist, max_results, filter, context);
  return MakeResult(max_results, context);
}

YbHnsw::SearchResult YbHnsw::MakeResult(size_t max_results, YbHnswSearchContext& context) const {
  auto& top = context.top.data();
  std::sort(top.begin(), top.end());
  top.resize(std::min(top.size(), max_results));

  SearchResult result;
  result.reserve(top.size());
  for (auto [distance, vector] : top) {
    result.emplace_back(GetVectorData(vector), distance);
  }
  return result;
}

std::pair<VectorIndex, YbHnsw::DistanceType> YbHnsw::SearchInNonBaseLayers(
    const std::byte* query_vector) const {
  auto best_vector = header_.entry;
  auto best_dist = Distance(query_vector, best_vector);
  VLOG_WITH_FUNC(4) << "best_vector: " << best_vector << ", best_dist: " << best_dist;

  for (auto level = header_.max_level; level > 0;) {
    auto updated = false;
    VLOG_WITH_FUNC(4)
        << "level: " << level << ", best_vector: " << best_vector << ", best_dist: " << best_dist;
    for (auto neighbor : GetNeighborsInNonBaseLayer(level, best_vector)) {
      auto neighbor_dist = Distance(query_vector, neighbor);
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
    const std::byte* query_vector, VectorIndex best_vector, DistanceType best_dist,
    size_t max_results, const VectorFilter& filter, YbHnswSearchContext& context) const {
  auto& top = context.top;
  top.clear();
  auto& extra_top = context.extra_top;
  extra_top.clear();
  auto& visited = context.visited;
  visited.clear();
  auto& next = context.next;
  next.clear();

  // We will visit at least entry vector and its neighbors.
  // So could use the following as initial capacity for visited.
  visited.reserve(header_.config.connectivity_base + 1u);

  auto top_limit = max_results;
  auto extra_top_limit = std::max(header_.config.expansion_search, max_results) - max_results;
  next.push({best_dist, best_vector});
  if (!filter || filter(GetVectorData(best_vector))) {
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
    auto neighbors = GetNeighborsInBaseLayer(vector);
    visited.reserve(visited.size() + std::ranges::size(neighbors));

    for (auto neighbor : neighbors) {
      if (visited.set(neighbor)) {
        continue;
      }
      auto neighbor_dist = Distance(query_vector, neighbor);

      if (top.size() < top_limit || extra_top.size() < extra_top_limit ||
          neighbor_dist < best_dist) {
        next.push({neighbor_dist, neighbor});
        if (!filter || filter(GetVectorData(neighbor))) {
          if (top.size() == top_limit) {
            auto extra_push = top.top().first;
            if (neighbor_dist < extra_push) {
              top.replace_top({neighbor_dist, neighbor});
            } else {
              extra_push = neighbor_dist;
            }
            if (extra_top.size() < extra_top_limit) {
              extra_top.push(extra_push);
            } else if (extra_push < extra_top.top()) {
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

boost::iterator_range<MisalignedPtr<const VectorIndex>> YbHnsw::GetNeighborsInBaseLayer(
    size_t vector) const {
  auto vector_data = VectorHeader(vector);
  auto base_ptr = blocks_[*YB_MISALIGNED_PTR(
      vector_data, VectorData, base_layer_neighbors_block)].data();
  auto begin = *YB_MISALIGNED_PTR(vector_data, VectorData, base_layer_neighbors_begin);
  auto end = *YB_MISALIGNED_PTR(vector_data, VectorData, base_layer_neighbors_end);
  return boost::make_iterator_range(
      MisalignedPtr<const VectorIndex>(base_ptr + begin * kNeighborSize),
      MisalignedPtr<const VectorIndex>(base_ptr + end * kNeighborSize));
}

boost::iterator_range<MisalignedPtr<const VectorIndex>> YbHnsw::GetNeighborsInNonBaseLayer(
    size_t level, size_t vector) const {
  auto max_vectors_per_block = header_.max_vectors_per_non_base_block;
  auto block_index = vector / max_vectors_per_block;
  vector %= max_vectors_per_block;
  auto& layer = header_.layers[level];
  auto base_ptr = blocks_[layer.block + block_index].data();
  auto finish = Load<NeighborsRefType, YbHnswEndian>(base_ptr + vector * kNeighborsRefSize);
  auto start = vector > 0
      ? Load<NeighborsRefType, YbHnswEndian>(base_ptr + (vector - 1) * kNeighborsRefSize) : 0;
  size_t refs_count;
  if (block_index == layer.last_block_index) {
    refs_count = layer.last_block_vectors_amount;
  } else {
    refs_count = max_vectors_per_block;
  }
  base_ptr += refs_count * kNeighborsRefSize;

  auto result = boost::make_iterator_range(
      MisalignedPtr<const VectorIndex>(base_ptr + start * kNeighborSize),
      MisalignedPtr<const VectorIndex>(base_ptr + finish * kNeighborSize));
  VLOG_WITH_FUNC(4) << AsString(result);
  return result;
}

const std::byte* YbHnsw::BlockPtr(
    size_t block, size_t entries_per_block, size_t entry, size_t entry_size) const {
  block += entry / entries_per_block;
  entry %= entries_per_block;
  return blocks_[block].data() + entry * entry_size;
}

Slice YbHnsw::GetVectorDataSlice(size_t vector) const {
  auto vector_data = VectorHeader(vector);
  auto base_ptr = blocks_[*YB_MISALIGNED_PTR(vector_data, VectorData, aux_data_block)].data();
  auto begin = *YB_MISALIGNED_PTR(vector_data, VectorData, aux_data_begin);
  auto end = *YB_MISALIGNED_PTR(vector_data, VectorData, aux_data_end);
  return Slice(base_ptr + begin, base_ptr + end);
}

VectorId YbHnsw::GetVectorData(size_t vector) const {
  return TryFullyDecodeVectorId(GetVectorDataSlice(vector));
}

YbHnsw::DistanceType YbHnsw::Distance(const std::byte* lhs, const std::byte* rhs) const {
  using unum::usearch::byte_t;
  return metric_(pointer_cast<const byte_t*>(lhs), pointer_cast<const byte_t*>(rhs));
}

YbHnsw::DistanceType YbHnsw::Distance(const std::byte* lhs, size_t vector) const {
  return Distance(lhs, CoordinatesPtr(vector));
}

MisalignedPtr<const VectorData> YbHnsw::VectorHeader(size_t vector) const {
  return MisalignedPtr<const VectorData>(BlockPtr(
      header_.vector_data_block, header_.vector_data_amount_per_block, vector,
      header_.vector_data_size));
}

const std::byte* YbHnsw::CoordinatesPtr(size_t vector) const {
  return VectorHeader(vector).raw() + offsetof(VectorData, coordinates);
}

boost::iterator_range<MisalignedPtr<const YbHnsw::CoordinateType>> YbHnsw::MakeCoordinates(
    const std::byte* ptr) const {
  auto start = MisalignedPtr<const CoordinateType>(ptr);
  return boost::make_iterator_range(start, start + header_.dimensions);
}

boost::iterator_range<MisalignedPtr<const YbHnsw::CoordinateType>> YbHnsw::Coordinates(
    size_t vector) const {
  return MakeCoordinates(CoordinatesPtr(vector));
}

}  // namespace yb::vector_index

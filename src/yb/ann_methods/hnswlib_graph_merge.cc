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

#include "yb/ann_methods/hnswlib_graph_merge.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <queue>
#include <span>
#include <vector>

#include "yb/gutil/casts.h"

#include "yb/util/cast.h"

namespace yb::ann_methods {

namespace {

constexpr uint32_t kNoEntry = std::numeric_limits<uint32_t>::max();

// Neighbor selection alpha used by HNSW-Merger. Not used for inner product distance, that could
// be negative, so alpha would prune more instead of less.
float PruneAlpha(vector_index::DistanceKind distance_kind) {
  return distance_kind == vector_index::DistanceKind::kInnerProduct ? 1.0f : 1.05f;
}

template <vector_index::ValidDistanceResultType DistanceResult>
class HnswlibGraphMerger {
 public:
  using HNSW = hnswlib::HierarchicalNSW<DistanceResult, vector_index::VectorId>;
  using Candidate = std::pair<DistanceResult, uint32_t>;
  using Candidates = std::vector<Candidate>;
  using Heap = std::priority_queue<
      std::pair<DistanceResult, hnswlib::tableint>,
      std::vector<std::pair<DistanceResult, hnswlib::tableint>>,
      typename HNSW::CompareByFirst>;

  HnswlibGraphMerger(HNSW& hnsw, float prune_alpha, uint32_t offset, size_t ef)
      : hnsw_(hnsw), prune_alpha_(prune_alpha), offset_(offset), ef_(ef) {}

  Status Execute(uint32_t source_entry, const vector_index::ParallelForFunc& parallel_for) {
    const auto end = narrow_cast<uint32_t>(hnsw_.getCurrentElementCount());
    const int old_max_level = hnsw_.maxlevel_;
    const uint32_t old_entry = hnsw_.enterpoint_node_;
    const int source_max_level = hnsw_.element_levels_[source_entry];
    // Layers present in only one of the graphs keep their links.
    const int top_layer = std::min(old_max_level, source_max_level);

    std::vector<std::vector<uint32_t>> layer_vectors(top_layer + 1);
    for (auto vector = offset_; vector != end; ++vector) {
      for (int layer = std::min(hnsw_.element_levels_[vector], top_layer); layer >= 0; --layer) {
        layer_vectors[layer].push_back(vector);
      }
    }

    // Forward search on all layers precedes the backward links, so it sees only the original
    // links of this graph. Searches skip the appended vectors, whose links are being replaced.
    std::vector<std::vector<Candidates>> found(top_layer + 1);
    std::vector<uint32_t> entries(end - offset_, kNoEntry);
    for (int layer = top_layer; layer >= 0; --layer) {
      const auto& vectors = layer_vectors[layer];
      auto& layer_found = found[layer];
      layer_found.resize(vectors.size());
      RETURN_NOT_OK(parallel_for(vectors.size(), [&](size_t i) {
        auto vector = vectors[i];
        auto& entry = entries[vector - offset_];
        int higher_layer = layer;
        if (entry == kNoEntry) {
          entry = old_entry;
          higher_layer = old_max_level;
        }
        layer_found[i] = SearchBelow(vector, entry, higher_layer, layer);
        AddLinks(vector, layer, layer_found[i]);
        return Status::OK();
      }));
    }

    // Lazy backward links: each found vector gets links to the source vectors that found it.
    std::vector<uint32_t> starts(offset_ + 1);
    std::vector<uint32_t> targets;
    for (int layer = top_layer; layer >= 0; --layer) {
      std::fill(starts.begin(), starts.end(), 0);
      for (const auto& candidates : found[layer]) {
        for (const auto& [distance, vector] : candidates) {
          ++starts[vector + 1];
        }
      }
      std::partial_sum(starts.begin(), starts.end(), starts.begin());
      Candidates backward(starts.back());
      auto positions = starts;
      for (size_t i = 0; i != found[layer].size(); ++i) {
        for (const auto& [distance, vector] : found[layer][i]) {
          backward[positions[vector]++] = {distance, layer_vectors[layer][i]};
        }
      }
      found[layer] = {};
      targets.clear();
      for (uint32_t vector = 0; vector != offset_; ++vector) {
        if (starts[vector + 1] != starts[vector]) {
          targets.push_back(vector);
        }
      }
      RETURN_NOT_OK(parallel_for(targets.size(), [&](size_t i) {
        auto vector = targets[i];
        AddLinks(vector, layer, std::span(
            backward.data() + starts[vector], backward.data() + starts[vector + 1]));
        return Status::OK();
      }));
    }

    if (source_max_level > old_max_level) {
      hnsw_.setEntryPoint(source_entry);
    }
    return Status::OK();
  }

 private:
  DistanceResult Distance(uint32_t lhs, uint32_t rhs) const {
    return hnsw_.fstdistfunc_(
        hnsw_.getDataByInternalId(lhs), hnsw_.getDataByInternalId(rhs), hnsw_.dist_func_param_);
  }

  // Greedy search from entry on layers from higher_layer down to layer + 1, then search with beam
  // width ef on the layer. Only vectors before offset are visited. Sets entry to the closest vector
  // found on the layer.
  Candidates SearchBelow(uint32_t vector, uint32_t& entry, int higher_layer, int layer) {
    auto current = entry;
    auto current_distance = Distance(vector, current);
    for (int upper_layer = higher_layer; upper_layer > layer; --upper_layer) {
      for (bool changed = true; changed;) {
        changed = false;
        auto* list = hnsw_.get_linklist_at_level(current, upper_layer);
        auto* links = pointer_cast<const hnswlib::tableint*>(list + 1);
        for (size_t i = 0, count = hnsw_.getListCount(list); i != count; ++i) {
          if (links[i] >= offset_) {
            continue;
          }
          auto distance = Distance(vector, links[i]);
          if (distance < current_distance) {
            current_distance = distance;
            current = links[i];
            changed = true;
          }
        }
      }
    }
    auto top = hnsw_.searchBaseLayer(
        current, hnsw_.getDataByInternalId(vector), layer, ef_, offset_);
    Candidates result;
    result.reserve(top.size());
    for (; !top.empty(); top.pop()) {
      result.push_back(top.top());
    }
    // The heap pops the farthest vector first.
    entry = result.empty() ? current : result.back().second;
    return result;
  }

  // Adds candidates to the links of the vector on the layer, selecting neighbors with the
  // heuristic when there are too many.
  void AddLinks(uint32_t vector, int layer, std::span<const Candidate> candidates) {
    auto* list = hnsw_.get_linklist_at_level(vector, layer);
    auto* links = pointer_cast<hnswlib::tableint*>(list + 1);
    size_t count = hnsw_.getListCount(list);
    const size_t max_links = layer ? hnsw_.maxM_ : hnsw_.maxM0_;
    if (count + candidates.size() <= max_links) {
      for (const auto& [distance, candidate] : candidates) {
        links[count++] = candidate;
      }
      hnsw_.setListCount(list, count);
      return;
    }
    Heap heap;
    for (const auto& candidate : candidates) {
      heap.push(candidate);
    }
    for (size_t i = 0; i != count; ++i) {
      heap.emplace(Distance(vector, links[i]), links[i]);
    }
    hnsw_.getNeighborsByHeuristic2(heap, max_links, prune_alpha_);
    for (count = 0; !heap.empty(); heap.pop()) {
      links[count++] = heap.top().second;
    }
    hnsw_.setListCount(list, count);
  }

  HNSW& hnsw_;
  const float prune_alpha_;
  const uint32_t offset_;
  const size_t ef_;
};

} // namespace

template <vector_index::ValidDistanceResultType DistanceResult>
Status MergeHnswlibGraph(
    hnswlib::HierarchicalNSW<DistanceResult, vector_index::VectorId>& hnsw,
    vector_index::DistanceKind distance_kind, uint32_t offset, uint32_t source_entry, size_t ef,
    const vector_index::ParallelForFunc& parallel_for) {
  return HnswlibGraphMerger<DistanceResult>(hnsw, PruneAlpha(distance_kind), offset, ef).Execute(
      source_entry, parallel_for);
}

template Status MergeHnswlibGraph<float>(
    hnswlib::HierarchicalNSW<float, vector_index::VectorId>& hnsw,
    vector_index::DistanceKind distance_kind, uint32_t offset, uint32_t source_entry, size_t ef,
    const vector_index::ParallelForFunc& parallel_for);

}  // namespace yb::ann_methods

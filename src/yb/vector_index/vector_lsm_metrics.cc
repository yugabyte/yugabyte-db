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

#include "yb/util/metrics.h"

#include "yb/vector_index/vector_lsm_metrics.h"

METRIC_DEFINE_counter(table, vector_index_compact_write_bytes,
    "Vector index compaction write bytes", yb::MetricUnit::kBytes,
    "Number of bytes written during vector index compaction");

METRIC_DEFINE_counter(table, vector_index_compact_read_bytes,
    "Vector index compaction read bytes", yb::MetricUnit::kBytes,
    "Number of bytes read during vector index compaction");

METRIC_DEFINE_event_stats(table, vector_index_num_chunks,
    "Vector index lookup chunks", yb::MetricUnit::kEntries,
    "Number of chunks used during vector index search.");
METRIC_DEFINE_event_stats(table, vector_index_total_found_entries,
    "Vector index total found entries", yb::MetricUnit::kEntries,
    "Number of entries found by vector index search in all sources.");
METRIC_DEFINE_event_stats(table, vector_index_insert_registry_entries,
    "Vector index insert registry found entries", yb::MetricUnit::kEntries,
    "Number of entries found by vector index search in insert registry.");
METRIC_DEFINE_event_stats(table, vector_index_insert_registry_search_us,
    "Vector index insert registry lookup time", yb::MetricUnit::kMicroseconds,
    "Time (microseconds) that vector index search spent in insert registry.");
METRIC_DEFINE_event_stats(table, vector_index_chunks_search_us,
    "Vector index insert chunks lookup time", yb::MetricUnit::kMicroseconds,
    "Time (microseconds) that vector index search spent in chunks.");

namespace yb::vector_index {

VectorLSMMetrics::VectorLSMMetrics(const MetricEntityPtr& entity)
    : compact_write_bytes(METRIC_vector_index_compact_write_bytes.Instantiate(entity)),
      compact_read_bytes(METRIC_vector_index_compact_read_bytes.Instantiate(entity)),
      num_chunks(METRIC_vector_index_num_chunks.Instantiate(entity)),
      total_found_entries(METRIC_vector_index_total_found_entries.Instantiate(entity)),
      insert_registry_entries(METRIC_vector_index_insert_registry_entries.Instantiate(entity)),
      insert_registry_search_us(METRIC_vector_index_insert_registry_search_us.Instantiate(entity)),
      chunks_search_us(METRIC_vector_index_chunks_search_us.Instantiate(entity)) {
}

}  // namespace yb::vector_index

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

METRIC_DEFINE_counter(
    vector_index,
    vector_index_compact_write_bytes,
    "Vector index compaction write bytes",
    yb::MetricUnit::kBytes,
    "Number of bytes written during vector index compaction");

namespace yb::vector_index {

VectorLSMMetrics::VectorLSMMetrics(const MetricEntityPtr& entity)
    : compact_write_bytes(METRIC_vector_index_compact_write_bytes.Instantiate(entity)) {
}

}  // namespace yb::vector_index

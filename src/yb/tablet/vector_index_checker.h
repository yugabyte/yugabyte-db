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

#pragma once

#include "yb/docdb/docdb_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

namespace yb::tablet {

// Checks that the vector index agrees with the indexed table and the reverse mapping: every row
// with a vector has its vector in the index and a live reverse mapping entry, and every live
// reverse mapping entry points to a row which references its vector. Invisible vectors, see
// VectorIndexCheckResultPB::num_invisible_vectors, are not inconsistencies. Works on any replica.
// Flushes the vector index first. Reports at most max_inconsistencies entries, a default when it
// is 0.
Result<VectorIndexCheckResultPB> CheckVectorIndex(
    Tablet& tablet, docdb::DocVectorIndex& vector_index, size_t max_inconsistencies,
    CoarseTimePoint deadline);

}  // namespace yb::tablet

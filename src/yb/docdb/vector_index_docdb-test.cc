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

#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/vector_index_update.h"

#include "yb/util/range.h"

namespace yb::docdb {

class VectorIndexDocDBTest : public DocDBTestBase {
  Schema CreateSchema() override {
    return Schema();
  }
};

TEST_F(VectorIndexDocDBTest, Update) {
  const HybridTime hybrid_time = HybridTime::FromMicros(1000);
  constexpr int kNumNodes = 3;
  const auto kNodes = Range(1, kNumNodes + 1);
  rocksdb::WriteBatch write_batch;
  FloatVectorIndexUpdate update(hybrid_time, write_batch);
  for (int i : kNodes) {
    update.AddVector(i, {static_cast<float>(M_E * i), static_cast<float>(M_PI * i)});
  }
  for (int i : kNodes) {
    update.SetNeighbors(i, /* level= */ 0, Range(i + 1, kNumNodes + 1).ToContainer());
  }
  for (int i : kNodes) {
    update.AddDirectedEdge(i, (i % kNumNodes) + 1, i * 10);
  }

  update.DeleteDirectedEdge(2, 3, 20);
  update.DeleteVector(3);

  ASSERT_OK(rocksdb()->Write(write_options(), &write_batch));

  AssertDocDbDebugDumpStrEq(R"#(
    // The vector 1 itself.
    SubDocKey(DocKey([], [1]), [HT{ physical: 1000 }]) -> [2.71828174591064, 3.14159274101257]
    // The neighbors of the vector 1 in level 0.
    SubDocKey(DocKey([], [1]), [0; HT{ physical: 1000 w: 3 }]) -> [2, 3]
    // The added edge from vector 1 to vector 2 in level 10.
    SubDocKey(DocKey([], [1]), [10, 2; HT{ physical: 1000 w: 6 }]) -> null
    // The same for remaining vectors.
    SubDocKey(DocKey([], [2]), [HT{ physical: 1000 w: 1 }]) -> [5.43656349182129, 6.28318548202515]
    SubDocKey(DocKey([], [2]), [0; HT{ physical: 1000 w: 4 }]) -> [3]
    // Delete the edge from vector 2 to vector 3 in level 20.
    SubDocKey(DocKey([], [2]), [20, 3; HT{ physical: 1000 w: 9 }]) -> DEL
    SubDocKey(DocKey([], [2]), [20, 3; HT{ physical: 1000 w: 7 }]) -> null
    // Delete the vector 3.
    SubDocKey(DocKey([], [3]), [HT{ physical: 1000 w: 10 }]) -> DEL
    SubDocKey(DocKey([], [3]), [HT{ physical: 1000 w: 2 }]) -> [8.15484523773193, 9.42477798461914]
    SubDocKey(DocKey([], [3]), [0; HT{ physical: 1000 w: 5 }]) -> []
    SubDocKey(DocKey([], [3]), [30, 1; HT{ physical: 1000 w: 8 }]) -> null
  )#");
}

}  // namespace yb::docdb

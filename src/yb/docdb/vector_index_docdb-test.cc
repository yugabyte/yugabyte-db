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
#include "yb/docdb/vector_index_read.h"
#include "yb/docdb/vector_index_update.h"

#include "yb/util/range.h"

namespace yb::docdb {

using VITypes = VectorIndexTypes<float>;

class VectorIndexDocDBTest : public DocDBTestBase {
 protected:
  Schema CreateSchema() override {
    return Schema();
  }

  void WriteSimple();
};

VITypes::IndexedVector GenVector(VertexId id) {
  return {static_cast<float>(M_E * id), static_cast<float>(M_PI * id)};
}

void VectorIndexDocDBTest::WriteSimple() {
  const HybridTime kHybridTime = HybridTime::FromMicros(1000);
  constexpr int kNumNodes = 3;
  const auto kNodes = Range(1, kNumNodes + 1);
  FloatVectorIndexStorage storage(doc_db());
  rocksdb::WriteBatch write_batch;
  FloatVectorIndexUpdate update(kHybridTime, write_batch, storage);
  for (int i : kNodes) {
    update.AddVector(i, GenVector(i));
  }
  for (int i : kNodes) {
    update.SetNeighbors(i, /* level= */ 0, Range(i + 1, kNumNodes + 1).ToContainer());
  }
  for (int i : kNodes) {
    update.AddDirectedEdge(i, (i % kNumNodes) + 1, i * 10);
  }

  update.DeleteDirectedEdge(2, 3, 20);
  update.DeleteVector(3);

  update.AddVector(4, GenVector(4));
  update.SetNeighbors(4, /* level= */ 0, Range(1, 2).ToContainer());
  update.SetNeighbors(4, /* level= */ 0, Range(1, 3).ToContainer());

  update.AddVector(5, GenVector(5));
  update.SetNeighbors(5, /* level= */ 0, Range(1, 3).ToContainer());
  update.DeleteDirectedEdge(5, 2, 0);
  update.AddDirectedEdge(5, 10, 0);
  update.SetNeighbors(5, /* level= */ 0, Range(1, 4).ToContainer());

  ASSERT_OK(rocksdb()->Write(write_options(), &write_batch));
}

TEST_F(VectorIndexDocDBTest, Update) {
  WriteSimple();

  // Please note that in real system we would never get such set of data.
  // But it is controlled by higher levels of the system.
  // So storage layer just perform its job and store/load requested information.
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
    // Vector 4
    SubDocKey(DocKey([], [4]), [HT{ physical: 1000 w: 11 }]) -> [10.8731269836426, 12.5663709640503]
    // Overwrite vector 4 neighbors in level 0.
    SubDocKey(DocKey([], [4]), [0; HT{ physical: 1000 w: 13 }]) -> [1, 2]
    SubDocKey(DocKey([], [4]), [0; HT{ physical: 1000 w: 12 }]) -> [1]
    // Vector 5
    SubDocKey(DocKey([], [5]), [HT{ physical: 1000 w: 14 }]) -> [13.5914087295532, 15.7079629898071]
    // Overwrite vector 4 neighbors in level 0. Including overwrite to updates.
    SubDocKey(DocKey([], [5]), [0; HT{ physical: 1000 w: 18 }]) -> [1, 2, 3]
    SubDocKey(DocKey([], [5]), [0; HT{ physical: 1000 w: 15 }]) -> [1, 2]
    SubDocKey(DocKey([], [5]), [0, 2; HT{ physical: 1000 w: 16 }]) -> DEL
    SubDocKey(DocKey([], [5]), [0, 10; HT{ physical: 1000 w: 17 }]) -> null
  )#");
}

TEST_F(VectorIndexDocDBTest, Read) {
  WriteSimple();

  const HybridTime kReadHybridTime = HybridTime::FromMicros(1001);

  ReadOperationData read_operation_data = {
    .deadline = CoarseTimePoint::max(),
    .read_time = ReadHybridTime::SingleTime(kReadHybridTime),
  };

  FloatVectorIndexStorage storage(doc_db());

  auto vector = ASSERT_RESULT(storage.GetVector(read_operation_data, 1));
  ASSERT_EQ(vector, GenVector(1));
  auto neighbors = ASSERT_RESULT(storage.GetNeighbors(read_operation_data, 1, 0));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({2, 3}));
  neighbors = ASSERT_RESULT(storage.GetNeighbors(read_operation_data, 1, 10));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({2}));

  vector = ASSERT_RESULT(storage.GetVector(read_operation_data, 2));
  ASSERT_EQ(vector, GenVector(2));
  neighbors = ASSERT_RESULT(storage.GetNeighbors(read_operation_data, 2, 0));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({3}));
  neighbors = ASSERT_RESULT(storage.GetNeighbors(read_operation_data, 2, 20));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({}));

  vector = ASSERT_RESULT(storage.GetVector(read_operation_data, 3));
  ASSERT_TRUE(vector.empty()); // Vector not found

  vector = ASSERT_RESULT(storage.GetVector(read_operation_data, 4));
  ASSERT_EQ(vector, GenVector(4));
  neighbors = ASSERT_RESULT(storage.GetNeighbors(read_operation_data, 4, 0));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({1, 2}));

  vector = ASSERT_RESULT(storage.GetVector(read_operation_data, 5));
  ASSERT_EQ(vector, GenVector(5));
  neighbors = ASSERT_RESULT(storage.GetNeighbors(read_operation_data, 5, 0));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({1, 2, 3}));
}

TEST_F(VectorIndexDocDBTest, ReadUpdate) {
  WriteSimple();

  const HybridTime kHybridTime = HybridTime::FromMicros(2000);

  ReadOperationData read_operation_data = {
    .deadline = CoarseTimePoint::max(),
    .read_time = ReadHybridTime::SingleTime(kHybridTime),
  };

  FloatVectorIndexStorage storage(doc_db());

  rocksdb::WriteBatch write_batch;
  FloatVectorIndexUpdate update(kHybridTime, write_batch, storage);

  update.AddVector(6, GenVector(6));
  update.DeleteVector(4);
  update.SetNeighbors(1, 0, {3, 4});
  update.AddDirectedEdge(1, 5, 0);
  update.DeleteDirectedEdge(1, 3, 0);
  update.SetNeighbors(1, 10, {2, 3, 4, 5, 6});
  update.AddDirectedEdge(2, 4, 0);
  update.DeleteDirectedEdge(5, 2, 0);

  for (int i = 1; i <= 6; ++i) {
    SCOPED_TRACE(Format("Vertex id: $0", i));
    auto vector = ASSERT_RESULT(update.GetVector(read_operation_data, i));
    if (i != 3 && i != 4) {
      ASSERT_EQ(vector, GenVector(i));
    } else {
      ASSERT_TRUE(vector.empty());
    }
  }

  auto neighbors = ASSERT_RESULT(update.GetNeighbors(read_operation_data, 1, 0));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({4, 5}));
  neighbors = ASSERT_RESULT(update.GetNeighbors(read_operation_data, 1, 10));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({2, 3, 4, 5, 6}));
  neighbors = ASSERT_RESULT(update.GetNeighbors(read_operation_data, 2, 0));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({3, 4}));
  neighbors = ASSERT_RESULT(update.GetNeighbors(read_operation_data, 5, 0));
  ASSERT_EQ(neighbors, VectorNodeNeighbors({1, 3}));
}

}  // namespace yb::docdb

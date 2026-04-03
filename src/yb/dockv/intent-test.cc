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

#include <gtest/gtest.h>

#include "yb/dockv/intent.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/value_type.h"
#include "yb/util/uuid.h"

namespace yb::dockv {

TEST(IntentTest, MakeWeak) {
  static const IntentTypeSet kWeakReadSet({IntentType::kWeakRead});
  static const IntentTypeSet kWeakWriteSet({IntentType::kWeakWrite});
  static const auto kWeakReadWriteSet = kWeakReadSet | kWeakWriteSet;
  static const IntentTypeSet kStrongReadSet({IntentType::kStrongRead});
  static const IntentTypeSet kStrongWriteSet({IntentType::kStrongWrite});
  static const auto kStrongReadWriteSet = kStrongReadSet | kStrongWriteSet;
  static const IntentTypeSet kEmptySet;

  ASSERT_EQ(kEmptySet, MakeWeak(kEmptySet));

  ASSERT_EQ(kWeakReadSet, MakeWeak(kWeakReadSet));
  ASSERT_EQ(kWeakReadSet, MakeWeak(kStrongReadSet));
  ASSERT_EQ(kWeakReadSet, MakeWeak(kStrongReadSet | kWeakReadSet));

  ASSERT_EQ(kWeakWriteSet, MakeWeak(kWeakWriteSet));
  ASSERT_EQ(kWeakWriteSet, MakeWeak(kStrongWriteSet));
  ASSERT_EQ(kWeakWriteSet, MakeWeak(kStrongWriteSet | kWeakWriteSet));

  ASSERT_EQ(kWeakReadWriteSet, MakeWeak(kWeakReadWriteSet));
  ASSERT_EQ(kWeakReadWriteSet, MakeWeak(kStrongReadSet | kWeakWriteSet));
  ASSERT_EQ(kWeakReadWriteSet, MakeWeak(kStrongReadSet | kWeakReadWriteSet));
  ASSERT_EQ(kWeakReadWriteSet, MakeWeak(kStrongWriteSet | kWeakReadSet));
  ASSERT_EQ(kWeakReadWriteSet, MakeWeak(kStrongWriteSet | kWeakReadWriteSet));
  ASSERT_EQ(kWeakReadWriteSet, MakeWeak(kStrongReadWriteSet));
  ASSERT_EQ(kWeakReadWriteSet, MakeWeak(kStrongReadWriteSet | kWeakReadSet));
  ASSERT_EQ(kWeakReadWriteSet, MakeWeak(kStrongReadWriteSet | kWeakWriteSet));
  ASSERT_EQ(kWeakReadWriteSet, MakeWeak(kStrongReadWriteSet | kWeakReadWriteSet));
}

TEST(IntentTest, IsTopLevelIntentKey) {
  const auto cotable_id = Uuid::Generate();
  const ColocationId colocation_id = 0x42;

  // Top-level keys (no hash/range components) should be recognized.
  EXPECT_TRUE(IsTopLevelIntentKey(DocKey().Encode().AsSlice()));
  EXPECT_TRUE(IsTopLevelIntentKey(DocKey(cotable_id).Encode().AsSlice()));
  EXPECT_TRUE(IsTopLevelIntentKey(DocKey(colocation_id).Encode().AsSlice()));

  const KeyEntryValues hash_vals({KeyEntryValue::Int32(1)});
  const KeyEntryValues range_vals({KeyEntryValue::Int32(2)});

  // Regular keys with hash/range components are not top-level.
  EXPECT_FALSE(IsTopLevelIntentKey(
      DocKey(0x1234, hash_vals).Encode().AsSlice()));
  EXPECT_FALSE(IsTopLevelIntentKey(
      DocKey(range_vals).Encode().AsSlice()));
  EXPECT_FALSE(IsTopLevelIntentKey(
      DocKey(0x1234, hash_vals, range_vals).Encode().AsSlice()));

  // Cotable keys with hash/range components are not top-level.
  EXPECT_FALSE(IsTopLevelIntentKey(
      DocKey(cotable_id, 0x1234, hash_vals).Encode().AsSlice()));
  {
    DocKey key(range_vals);
    key.set_cotable_id(cotable_id);
    EXPECT_FALSE(IsTopLevelIntentKey(key.Encode().AsSlice()));
  }
  EXPECT_FALSE(IsTopLevelIntentKey(
      DocKey(cotable_id, 0x1234, hash_vals, range_vals).Encode().AsSlice()));

  // Colocated keys with hash/range components are not top-level.
  EXPECT_FALSE(IsTopLevelIntentKey(
      DocKey(colocation_id, 0x1234, hash_vals).Encode().AsSlice()));
  {
    DocKey key(range_vals);
    key.set_colocation_id(colocation_id);
    EXPECT_FALSE(IsTopLevelIntentKey(key.Encode().AsSlice()));
  }
  EXPECT_FALSE(IsTopLevelIntentKey(
      DocKey(colocation_id, 0x1234, hash_vals, range_vals).Encode().AsSlice()));
}

} // namespace yb::dockv

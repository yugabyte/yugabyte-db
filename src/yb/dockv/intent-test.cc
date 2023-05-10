// Copyright (c) YugaByte, Inc.
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

} // namespace yb::dockv

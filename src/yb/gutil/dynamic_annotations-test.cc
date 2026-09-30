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
// Regression test for #31673. Ensures that gutil's dynamic_annotations.h can
// coexist in the same translation unit with absl headers that transitively
// pull in absl/base/dynamic_annotations.h, with no macro-redefinition or
// conflicting-prototype errors. The test deliberately includes a few
// commonly-used absl features (flat_hash_map, flat_hash_set, str_cat) that
// transitively bring in absl's annotation machinery.

#include <atomic>
#include <thread>

// Mix the include order on purpose: include some absl headers before gutil's
// dynamic_annotations.h, and one after. With -Werror, any macro redefinition
// triggered here would have failed the build.
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "yb/gutil/dynamic_annotations.h"

#include "absl/strings/str_cat.h"     // NOLINT(build/include_alpha)
#include "absl/strings/string_view.h"  // NOLINT(build/include_alpha)

#include "yb/util/test_util.h"

namespace yb {
namespace gutil {

class DynamicAnnotationsAbslCoexistenceTest : public YBTest {};

// Sanity: the gutil-only macros that we explicitly preserve must still work
// (they aren't provided by absl). If absl ever stopped letting these compile
// alongside it, we want to catch it here.
TEST_F(DynamicAnnotationsAbslCoexistenceTest, GutilOnlyMacrosCompile) {
  int counter = 0;
  ANNOTATE_UNPROTECTED_WRITE(counter) = 42;
  ASSERT_EQ(counter, 42);

  // ANNOTATE_HAPPENS_BEFORE / ANNOTATE_HAPPENS_AFTER are gutil-only and used
  // by GoogleOnce. Just exercise them across a thread boundary.
  std::atomic<int> ready{0};
  int payload = 0;
  std::thread producer([&] {
    payload = 7;
    ANNOTATE_HAPPENS_BEFORE(&ready);
    ready.store(1, std::memory_order_release);
  });
  while (ready.load(std::memory_order_acquire) == 0) {
    std::this_thread::yield();
  }
  ANNOTATE_HAPPENS_AFTER(&ready);
  producer.join();
  ASSERT_EQ(payload, 7);

  // ANNOTATE_IGNORE_SYNC_BEGIN/END are gutil-only and used in util/thread.cc.
  ANNOTATE_IGNORE_SYNC_BEGIN();
  ANNOTATE_IGNORE_SYNC_END();
}

// Sanity: macros that exist in BOTH gutil and absl must still expand to
// something legal. After our change these come from absl's definitions.
TEST_F(DynamicAnnotationsAbslCoexistenceTest, OverlappingMacrosCompile) {
  int x = 0;
  ANNOTATE_BENIGN_RACE(&x, "test benign race");
  ANNOTATE_BENIGN_RACE_SIZED(&x, sizeof(x), "test benign race sized");

  ANNOTATE_IGNORE_READS_BEGIN();
  int y = x;
  ANNOTATE_IGNORE_READS_END();
  ASSERT_EQ(y, 0);

  ANNOTATE_IGNORE_WRITES_BEGIN();
  x = 1;
  ANNOTATE_IGNORE_WRITES_END();
  ASSERT_EQ(x, 1);

  ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN();
  x = 2;
  y = x;
  ANNOTATE_IGNORE_READS_AND_WRITES_END();
  ASSERT_EQ(y, 2);

  int z = ANNOTATE_UNPROTECTED_READ(x);
  ASSERT_EQ(z, 2);

  ANNOTATE_THREAD_NAME("dyn_annot_coexist_test");
  ANNOTATE_ENABLE_RACE_DETECTION(1);
}

// Smoke test that the absl features we pulled in actually work in this TU.
// If the absl headers had been silently neutered by include-order issues we
// would notice here.
TEST_F(DynamicAnnotationsAbslCoexistenceTest, AbslFeaturesWork) {
  absl::flat_hash_map<std::string, int> counts;
  for (absl::string_view word : {"a", "b", "a", "c", "b", "a"}) {
    counts[std::string(word)] += 1;
  }
  ASSERT_EQ(counts.size(), 3u);
  ASSERT_EQ(counts["a"], 3);
  ASSERT_EQ(counts["b"], 2);
  ASSERT_EQ(counts["c"], 1);

  absl::flat_hash_set<int> seen;
  for (int i : {1, 2, 2, 3, 3, 3}) {
    seen.insert(i);
  }
  ASSERT_EQ(seen.size(), 3u);
  ASSERT_TRUE(seen.contains(1));
  ASSERT_TRUE(seen.contains(2));
  ASSERT_TRUE(seen.contains(3));

  std::string joined = absl::StrCat("foo", "_", 42, "_", true);
  ASSERT_EQ(joined, "foo_42_1");
}

// ASAN_* macros are also gutil-only (absl doesn't provide them under these
// names) and are used by faststring/arena/shmem. Smoke-check they expand to
// something legal when absl is in scope.
TEST_F(DynamicAnnotationsAbslCoexistenceTest, AsanMacrosCompile) {
  alignas(8) char buffer[64];
  ASAN_POISON_MEMORY_REGION(buffer, sizeof(buffer));
  ASAN_UNPOISON_MEMORY_REGION(buffer, sizeof(buffer));

  size_t shadow_scale = 0;
  size_t shadow_offset = 0;
  ASAN_GET_SHADOW_MAPPING(&shadow_scale, &shadow_offset);
  // Result intentionally unchecked: with no ASan in this build the macro
  // sets both to 0; with ASan it returns runtime-dependent values. We only
  // care that the macro compiles in a TU that also has absl in scope.
  (void)shadow_scale;
  (void)shadow_offset;
}

}  // namespace gutil
}  // namespace yb

//
// Copyright (c) YugaByte, Inc.
//

#include <gtest/gtest.h>

#include "yb/util/test_util.h"

#include "yb/util/memory/memory_context.h"
#include "yb/util/memory/mc_types.h"

namespace yb {

class MemoryContextTest : public YBTest {
};

// Updates external counter when object is created/destroyed.
// So one could check whether all objects is destoyed.
class Trackable {
 public:
  explicit Trackable(int* counter)
    : counter_(counter) {
    ++*counter_;
  }

  Trackable(const Trackable& rhs)
    : counter_(rhs.counter_) {
    ++*counter_;
  }

  Trackable& operator=(const Trackable& rhs) {
    --*counter_;
    counter_ = rhs.counter_;
    ++*counter_;
    return *this;
  }

  ~Trackable() {
    --*counter_;
  }

 private:
  int* counter_;
};

// Checks that counter is zero on destruction, so all objects is destroyed.
class CounterHolder {
 public:
  int counter = 0;

  ~CounterHolder() {
    CheckCounter();
  }
 private:
  void CheckCounter() {
    ASSERT_EQ(0, counter);
  }
};

class CountedMemoryContext : public CounterHolder, public MemoryContext {
};

TEST_F(MemoryContextTest, TestUniquePtr) {
  CountedMemoryContext mc;
  MCUniPtr<Trackable> trackable(mc.NewObject<Trackable>(&mc.counter));
  ASSERT_EQ(1, mc.counter);
}

TEST_F(MemoryContextTest, TestAllocateShared) {
  CountedMemoryContext mc;
  auto trackable = mc.AllocateShared<Trackable>(&mc.counter);
  ASSERT_EQ(1, mc.counter);
}

TEST_F(MemoryContextTest, TestToShared) {
  CountedMemoryContext mc;
  auto trackable = mc.ToShared(mc.NewObject<Trackable>(&mc.counter));
  ASSERT_EQ(1, mc.counter);
}

TEST_F(MemoryContextTest, TestVector) {
  CountedMemoryContext mc;
  MCVector<Trackable> vector(&mc);
  vector.emplace_back(&mc.counter);
  ASSERT_EQ(1, mc.counter);
}

TEST_F(MemoryContextTest, TestList) {
  CountedMemoryContext mc;
  MCList<Trackable> list(&mc);
  list.emplace_back(&mc.counter);
  ASSERT_EQ(1, mc.counter);
}

TEST_F(MemoryContextTest, TestMap) {
  CountedMemoryContext mc;
  MCMap<int, Trackable> map(&mc);
  map.emplace(1, Trackable(&mc.counter));
  ASSERT_EQ(1, mc.counter);
}

TEST_F(MemoryContextTest, TestString) {
  CountedMemoryContext mc;
  MCMap<MCString, Trackable> map(&mc);
  MCString one("1", &mc);
  MCString ten("10", &mc);
  map.emplace(one, Trackable(&mc.counter));
  ASSERT_EQ(1, mc.counter);

  // Check correctness of comparison operators.
  ASSERT_LT(one, ten);
  ASSERT_FALSE(one < one);
  ASSERT_FALSE(ten < one);

  ASSERT_LE(one, ten);
  ASSERT_LE(one, one);
  ASSERT_FALSE(ten <= one);

  ASSERT_GE(ten, one);
  ASSERT_GE(one, one);
  ASSERT_FALSE(one >= ten);

  ASSERT_GT(ten, one);
  ASSERT_FALSE(one > one);
  ASSERT_FALSE(one > ten);
}

} // namespace yb

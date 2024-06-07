//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/util/memory/arena.h"
#include "yb/util/memory/mc_types.h"
#include "yb/util/test_util.h"

namespace yb {

using std::string;

class MemoryContextTypesTest : public YBTest {
 public:
  //------------------------------------------------------------------------------------------------
  // Test datatypes.
  class MCObjectType : public MCBase {
   public:
    explicit MCObjectType(int id = 0) : id_(id) {
    }

    int id() {
      return id_;
    }

   private:
    int id_;
  };

  //------------------------------------------------------------------------------------------------
  // Test functions.
  MemoryContextTypesTest()
      : YBTest() {
  }

  Arena *arena() {
    return &arena_;
  }

 private:
  Arena arena_;
};

// Testing datatype "MCString" which behaves as std::string but is allocated by the custom
// allocator "MCAllocator<char>".
TEST_F(MemoryContextTypesTest, TestMCString) {
  Arena local_arena;

  MCString s1(arena());
  CHECK_EQ(s1, "");

  MCString s2(10, 'a', arena());
  CHECK_EQ(s2, "aaaaaaaaaa");
  const char *str2 = "New value of s2";
  s2.assign(str2);
  CHECK_EQ(s2, str2);

  MCString s3("This is a SQL string", arena());
  CHECK_EQ(s3, "This is a SQL string");
  s3 += " after being postfixed";
  CHECK_EQ(s3, "This is a SQL string after being postfixed");

  MCString s4("This is to-be-truncated string", 10, arena());
  CHECK_EQ(s4, "This is to");

  const char *str_to_be_compared = "Testing string comparison";
  s4.assign(str_to_be_compared);

  // Compare between strings of the same context.
  auto s5 = MCMakeShared<MCString>(arena(), str_to_be_compared);
  CHECK_EQ(s4, *s5);

  // Compare between strings of the same context.
  MCString s6(str_to_be_compared, arena());
  CHECK_EQ(s4, s6);

  // Compare between strings of different contexts.
  MCString s7(str_to_be_compared, &local_arena);
  CHECK_EQ(s4, s7);

  // Allocate from private memory_context().
  const char *str2_to_be_compared = "Testing negative string comparison";
  MCString s8(str2_to_be_compared, arena());
  CHECK_EQ(s8, str2_to_be_compared);

  // Allocate from local.
  MCString s9(str2_to_be_compared, &local_arena);
  CHECK_EQ(s9, str2_to_be_compared);
  auto s10 = MCMakeShared<MCString>(&local_arena, str2_to_be_compared);
  CHECK_EQ(*s10, str2_to_be_compared);
  CHECK_EQ(s9, *s10);

  // Negative comparisons.
  CHECK_NE(s4, s8);
  CHECK_NE(s4, s9);
  CHECK_NE(s4, *s10);
}

// Test MCVector and MCList.
TEST_F(MemoryContextTypesTest, TestMCStl) {
  int i;
  const int max_entry = 99;

  // Constructing vector.
  MCVector<int> ql_vec(arena());
  ql_vec.resize(max_entry);
  for (i = 0; i < max_entry; i++) {
    ql_vec[i] = i*7;
  }

  // Checking vector.
  i = 0;
  for (int entry : ql_vec) {
    CHECK_EQ(entry, i*7);
    i++;
  }

  // Constructing list.
  MCList<int> ql_list(arena());
  for (i = 0; i < max_entry; i++) {
    ql_list.push_back(i*3);
  }

  // Checking list.
  i = 0;
  for (int entry : ql_list) {
    CHECK_EQ(entry, i*3);
    i++;
  }

  MCList<int> list_copy = ql_list;
  // Checking list.
  i = 0;
  for (int entry : list_copy) {
    CHECK_EQ(entry, i*3);
    i++;
  }
}

// Test MCObject.
TEST_F(MemoryContextTypesTest, TestMCTreeNode) {
  int i;
  const int max_entry = 99;

  // Construct vector.
  MCVector<MCObjectType *> objects(arena());
  objects.resize(max_entry);
  for (i = 0; i < max_entry; i++) {
    // Make SQL objects that share the same pool with "objects".
    objects[i] = new(arena()) MCObjectType(i*9);
  }

  // Checking vector and delete entries.
  i = 0;
  for (MCObjectType *entry : objects) {
    CHECK_EQ(entry->id(), i*9);
    delete entry;
    i++;
  }

  // Construct list.
  MCList<MCObjectType *> object_list(arena());
  for (i = 0; i < max_entry; i++) {
    // Make SQL objects that share the same pool with "object_list".
    MCObjectType *entry = new(arena()) MCObjectType(i*9);
    object_list.push_back(entry);
  }

  // Checking list.
  i = 0;
  for (MCObjectType *entry : object_list) {
    CHECK_EQ(entry->id(), i*9);
    delete entry;
    i++;
  }
}

}  // namespace yb

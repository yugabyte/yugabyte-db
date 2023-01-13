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

#include "yb/rpc/lightweight_message.h"
#include "yb/rpc/rtest.messages.h"
#include "yb/rpc/rtest.pb.h"

#include "yb/util/faststring.h"
#include "yb/util/logging.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace rpc {

// Make sure LW protobuf skips unknown fields.
TEST(LWProtoTest, SkipsUnknownFields) {
  rpc_test::TestObjectPB pb;
  faststring buf;

  {
    pb.set_string1("test1");
    pb.set_string2("test2");
    pb.mutable_record()->set_text("record");
    pb.set_int32(14);
    pb.set_int32_2(15);
    pb.mutable_record2()->set_text("record2");

    LOG(INFO) << "Source proto: " << pb.ShortDebugString();

    AnyMessageConstPtr ptr(&pb);
    buf.resize(ptr.SerializedSize());
    ASSERT_OK(ptr.SerializeToArray(buf.data()));

    LOG(INFO) << "Binary dump: " << Slice(buf).ToDebugHexString();
  }

  {
    rpc_test::TestObjectPBv2 pb2;
    AnyMessagePtr ptr(&pb2);

    ASSERT_OK(ptr.ParseFromSlice(Slice(buf)));
    LOG(INFO) << "Read proto: " << pb2.ShortDebugString();

    ASSERT_TRUE(pb2.has_string1());
    ASSERT_TRUE(pb2.has_int32_2());
    ASSERT_FALSE(pb2.has_int32_3());
    ASSERT_TRUE(pb2.has_record2());
    ASSERT_EQ(pb.string1(), pb2.string1());
    ASSERT_EQ(pb.int32_2(), pb2.int32_2());
    ASSERT_TRUE(pb2.record2().has_text());
    ASSERT_EQ(pb.record2().text(), pb2.record2().text());
  }

  {
    ThreadSafeArena arena;
    rpc_test::LWTestObjectPBv2 lwpb2(&arena);
    AnyMessagePtr ptr(&lwpb2);

    ASSERT_OK(ptr.ParseFromSlice(Slice(buf)));
    LOG(INFO) << "Read lightweight proto: " << lwpb2.ShortDebugString();

    ASSERT_TRUE(lwpb2.has_string1());
    ASSERT_TRUE(lwpb2.has_int32_2());
    ASSERT_FALSE(lwpb2.has_int32_3());
    ASSERT_TRUE(lwpb2.has_record2());
    ASSERT_EQ(pb.string1(), lwpb2.string1());
    ASSERT_EQ(pb.int32_2(), lwpb2.int32_2());
    ASSERT_TRUE(lwpb2.record2().has_text());
    ASSERT_EQ(pb.record2().text(), lwpb2.record2().text());
  }
}

} // namespace rpc
} // namespace yb

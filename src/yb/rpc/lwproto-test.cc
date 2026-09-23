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

#include "yb/rpc/lightweight_message.h"
#include "yb/rpc/rtest.messages.h"
#include "yb/rpc/rtest.pb.h"

#include "yb/util/faststring.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"

DECLARE_uint32(protobuf_message_total_bytes_limit);
DECLARE_uint64(rpc_max_message_size);

namespace yb {
namespace rpc {

namespace {

template <typename PB>
Status SerializePB(PB& pb, faststring& buf) {
  LOG(INFO) << "Source proto: " << pb.ShortDebugString();

  AnyMessageConstPtr ptr(&pb);
  buf.resize(ptr.SerializedSize());
  RETURN_NOT_OK(ptr.SerializeToArray(buf.data()));

  LOG(INFO) << "Binary dump: " << Slice(buf).ToDebugHexString();

  return Status::OK();
}

} // namespace

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

    ASSERT_OK(SerializePB(pb, buf));
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

// Test a very large proto (rpc_max_message_size < proto size < protobuf_message_total_bytes_limit).
TEST(LWProtoTest, BigMessage) {
  faststring buf;
  rpc_test::TestObjectPB pb;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_max_message_size) = 4_MB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_protobuf_message_total_bytes_limit) = 8_MB;

  constexpr auto kPBSize = 6_MB;

  pb.set_string1(RandomHumanReadableString(kPBSize));
  ASSERT_OK(SerializePB(pb, buf));

  ThreadSafeArena arena;
  rpc_test::LWTestObjectPBv2 lwpb2(&arena);
  AnyMessagePtr ptr(&lwpb2);

  ASSERT_OK(ptr.ParseFromSlice(Slice(buf)));
  LOG(INFO) << "Read lightweight proto: " << lwpb2.ShortDebugString();

  ASSERT_TRUE(lwpb2.has_string1());
  ASSERT_EQ(pb.string1(), lwpb2.string1());
}


// Every trace-tagged field type the generator handles, in both the lightweight and the plain
// protobuf form: scalars, enum, bool, hex bytes, bytes_as_string, repeated scalars, nested and
// repeated nested messages, a pointer field, and recursion through a self-referential message.
// Fields not tagged (i32, pairs) must not appear; unset tagged fields must not appear.
TEST(LWProtoTest, TracingAttributes) {
  using Attrs = std::vector<std::pair<std::string, std::string>>;
  const Attrs expected = {
      {"req.u32", "7"},
      {"req.str", "hello"},
      {"req.bytes", "0102FF"},
      {"req.en", "TWO"},
      {"req.ru32.0", "1"},
      {"req.ru32.1", "2"},
      {"req.rstr.0", "a"},
      {"req.rstr.1", "b"},
      {"req.message.str", "sub"},
      {"req.message.rbytes.0", "x1"},
      {"req.message.rbytes.1", "x2"},
      {"req.message.cycle.str", "deep"},
      {"req.repeated_messages.0.str", "r0"},
      {"req.repeated_messages.1.str", "r1"},
      {"req.ptr_message.str", "ptr"},
      {"req.flag", "true"},
      {"req.bytes_str", "raw text"},
  };

  rpc_test::LightweightRequestPB pb;
  pb.set_i32(-1);
  pb.set_u32(7);
  pb.set_str("hello");
  pb.set_bytes("\x01\x02\xff");
  pb.set_en(rpc_test::TWO);
  pb.add_ru32(1);
  pb.add_ru32(2);
  pb.add_rstr("a");
  pb.add_rstr("b");
  pb.mutable_message()->set_str("sub");
  pb.mutable_message()->add_rbytes("x1");
  pb.mutable_message()->add_rbytes("x2");
  pb.mutable_message()->mutable_cycle()->set_str("deep");
  pb.add_repeated_messages()->set_str("r0");
  pb.add_repeated_messages()->set_str("r1");
  pb.add_pairs()->set_s1("untagged");
  pb.mutable_ptr_message()->set_str("ptr");
  pb.set_flag(true);
  pb.set_bytes_str("raw text");
  ASSERT_EQ(TracingAttributes(pb), expected);

  ThreadSafeArena arena;
  rpc_test::LWLightweightRequestPB lw(&arena);
  lw.set_i32(-1);
  lw.set_u32(7);
  lw.dup_str("hello");
  lw.dup_bytes(Slice("\x01\x02\xff", 3));
  lw.set_en(rpc_test::TWO);
  lw.add_ru32(1);
  lw.add_ru32(2);
  lw.add_dup_rstr("a");
  lw.add_dup_rstr("b");
  lw.mutable_message()->dup_str("sub");
  lw.mutable_message()->add_dup_rbytes("x1");
  lw.mutable_message()->add_dup_rbytes("x2");
  lw.mutable_message()->mutable_cycle()->dup_str("deep");
  lw.add_repeated_messages()->dup_str("r0");
  lw.add_repeated_messages()->dup_str("r1");
  lw.add_pairs()->dup_s1("untagged");
  lw.mutable_ptr_message()->dup_str("ptr");
  lw.set_flag(true);
  lw.dup_bytes_str("raw text");
  ASSERT_EQ(lw.TracingAttributes(), expected);

  ASSERT_TRUE(TracingAttributes(rpc_test::LightweightRequestPB()).empty());
  ASSERT_TRUE(rpc_test::LWLightweightRequestPB(&arena).TracingAttributes().empty());
}

} // namespace rpc
} // namespace yb

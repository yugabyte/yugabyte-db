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

#include "yb/common/common_net.pb.h"

#include "yb/rpc/compressed_stream.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_test_util.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"
#include "yb/rpc/tcp_stream.h"

#include "yb/util/flags.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

DECLARE_bool(enable_stream_compression);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(node_to_node_encryption_scope);

namespace yb {
namespace rpc {

namespace {

CloudInfoPB CloudInfo(const std::string& cloud, const std::string& region,
                      const std::string& zone) {
  CloudInfoPB result;
  result.set_placement_cloud(cloud);
  result.set_placement_region(region);
  result.set_placement_zone(zone);
  return result;
}

} // namespace

class SecureTest : public YBTest {
 protected:
  // Builds a messenger the way a server does, so it holds exactly the transports the current
  // flags give a real one. With use_node_to_node_encryption off, SetupSecureContext creates no
  // context and leaves the messenger with the plain transports alone.
  Result<AutoShutdownMessengerHolder> BuildMessenger() {
    MessengerBuilder builder("test");
    builder.UseDefaultConnectionContextFactory();
    secure_context_ = VERIFY_RESULT(SetupSecureContext(
        /* root_dir= */ "", /* name= */ "test", SecureContextType::kInternal, &builder));
    return CreateAutoShutdownMessengerHolder(VERIFY_RESULT(builder.Build()));
  }

  std::unique_ptr<SecureContext> secure_context_;
};

// node_to_node_encryption_scope selects among the transports a messenger holds; it cannot add
// one. A messenger built without encryption holds none, and naming a protocol it has no stream
// factory for fails the connection, so every scope must still yield a transport it has.
// Without this, a cluster that never enabled encryption cannot open an RPC at all.
TEST_F(SecureTest, ProtocolNeverLeavesWhatMessengerHolds) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = false;
  auto messenger = ASSERT_RESULT(BuildMessenger());

  const auto here = CloudInfo("cloud1", "region1", "zone1");
  const auto elsewhere = CloudInfo("cloud2", "region2", "zone2");

  // The point this messenger has no factory for, which a connection naming it fails on.
  const auto* unavailable = CompressedStreamProtocol(Encrypted::kTrue);
  ASSERT_EQ(&messenger->ProtocolFor(Compressed::kTrue, Encrypted::kTrue), unavailable);

  for (const auto* scope : {"zone", "region", "cloud", "never"}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_scope) = scope;
    SCOPED_TRACE(scope);
    for (const auto& connect_to : {here, elsewhere}) {
      const auto* protocol = &messenger->ProtocolFor(connect_to, here);
      EXPECT_NE(protocol, unavailable);
      EXPECT_NE(protocol, SecureStreamProtocol());
    }
  }
}

// Compression is read off the same protocols as encryption, so a messenger keeps whichever it
// was built with regardless of what the scope decides.
TEST_F(SecureTest, ProtocolKeepsCompression) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = false;
  auto messenger = ASSERT_RESULT(BuildMessenger());

  const auto here = CloudInfo("cloud1", "region1", "zone1");
  EXPECT_EQ(&messenger->ProtocolFor(here, here), CompressedStreamProtocol(Encrypted::kFalse));
}

TEST_F(SecureTest, ProtocolWithoutCompression) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_stream_compression) = false;
  auto messenger = ASSERT_RESULT(BuildMessenger());

  const auto here = CloudInfo("cloud1", "region1", "zone1");
  EXPECT_EQ(&messenger->ProtocolFor(here, here), TcpStream::StaticProtocol());
}

} // namespace rpc
} // namespace yb

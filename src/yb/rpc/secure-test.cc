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

namespace yb {
namespace rpc {

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

// A caller asks for encryption from the address the connection will use, which knows nothing
// about how this messenger was built. Naming a transport it has no stream factory for fails
// the connection, so the request is clamped to what it holds. Without this, a cluster that
// never enabled encryption could not open an RPC at all.
TEST_F(SecureTest, ProtocolNeverLeavesWhatMessengerHolds) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = false;
  auto messenger = ASSERT_RESULT(BuildMessenger());

  // The point this messenger has no factory for, which a connection naming it fails on.
  const auto* unavailable = CompressedStreamProtocol(Encrypted::kTrue);
  ASSERT_EQ(&messenger->ProtocolFor(Compressed::kTrue, Encrypted::kTrue), unavailable);

  for (auto encrypted : {Encrypted::kFalse, Encrypted::kTrue}) {
    SCOPED_TRACE(ToString(encrypted));
    const auto* protocol = &messenger->ProtocolFor(encrypted);
    EXPECT_NE(protocol, unavailable);
    EXPECT_NE(protocol, SecureStreamProtocol());
  }
}

// Compression is read off the same protocols as encryption, so a messenger keeps whichever it
// was built with regardless of what the caller asks for.
TEST_F(SecureTest, ProtocolKeepsCompression) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = false;
  auto messenger = ASSERT_RESULT(BuildMessenger());

  EXPECT_EQ(
      &messenger->ProtocolFor(Encrypted::kFalse), CompressedStreamProtocol(Encrypted::kFalse));
}

TEST_F(SecureTest, ProtocolWithoutCompression) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_stream_compression) = false;
  auto messenger = ASSERT_RESULT(BuildMessenger());

  EXPECT_EQ(&messenger->ProtocolFor(Encrypted::kFalse), TcpStream::StaticProtocol());
}

} // namespace rpc
} // namespace yb

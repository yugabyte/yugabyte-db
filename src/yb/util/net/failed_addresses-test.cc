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

#include "yb/util/net/failed_addresses.h"

#include <gtest/gtest.h>

#include "yb/util/flags.h"
#include "yb/util/test_macros.h"
#include "yb/util/thread_restrictions.h"

DECLARE_int32(retry_failed_address_ms);

namespace yb {

// A failure record has to expire, or one transient failure would demote an address for the
// life of the process; the address that fails first is usually the one the deployment
// prefers.
TEST(FailedAddressesTest, ExpiresAfterTheRetryWindow) {
  const HostPort kFirst("first.example.com", 9100);
  const HostPort kSecond("second.example.com", 9100);

  FailedAddresses failed;
  EXPECT_FALSE(failed.Failed(kFirst));

  failed.MarkFailed(kFirst);
  EXPECT_TRUE(failed.Failed(kFirst));
  // A record names one address, not the node that advertised it.
  EXPECT_FALSE(failed.Failed(kSecond));

  // The same host on another port is a different address.
  EXPECT_FALSE(failed.Failed(HostPort("first.example.com", 9200)));

  {
    google::FlagSaver flag_saver;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retry_failed_address_ms) = 0;
    EXPECT_FALSE(failed.Failed(kFirst));
  }
  EXPECT_TRUE(failed.Failed(kFirst));

  failed.Clear();
  EXPECT_FALSE(failed.Failed(kFirst));
}

}  // namespace yb

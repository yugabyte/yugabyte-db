// Copyright (c) YugaByte, Inc.

#include "yb/integration-tests/redis_table_test_base.h"

#include <glog/logging.h>

#include "yb/client/client.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/integration-tests/yb_table_test_base.h"
#include "yb/client/redis_helpers.h"

using std::string;
using std::vector;
using std::unique_ptr;

namespace yb {
namespace integration_tests {

class RedisTableTest : public RedisTableTestBase {
};

using client::YBRedisWriteOp;
using client::RedisWriteOpForSetKV;
using client::YBColumnSchema;
using client::YBTableCreator;
using client::YBSchemaBuilder;
using client::YBColumnSchema;
using client::YBTableType;
using client::YBSession;

using integration_tests::YBTableTestBase;

TEST_F(RedisTableTest, SimpleRedisSetTest) {
  NO_FATALS(RedisSimpleSetCommands());
}

}  // namespace integration_tests
}  // namespace yb

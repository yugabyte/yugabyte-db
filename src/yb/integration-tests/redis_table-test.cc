// Copyright (c) YugaByte, Inc.

#include <glog/logging.h>

#include "yb/client/client.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/integration-tests/yb_table_test_base.h"

using std::string;
using std::vector;
using std::unique_ptr;

namespace yb {
namespace tablet {

using client::RedisWriteOp;
using client::YBColumnSchema;
using client::YBTableCreator;
using client::YBSchemaBuilder;
using client::YBColumnSchema;
using client::YBTableType;
using client::YBSession;

using integration_tests::YBTableTestBase;

class RedisTableTest : public YBTableTestBase {
 protected:

  void CreateTable() OVERRIDE {
    unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());

    ASSERT_OK(table_creator->table_name(table_name())
                  .table_type(YBTableType::REDIS_TABLE_TYPE)
                  .num_replicas(3)
                  .Create());
    table_exists_ = true;
  }

  using YBTableTestBase::PutKeyValue;

  void PutKeyValue(YBSession* session, string key, string value) OVERRIDE {
    unique_ptr<RedisWriteOp> redis_op(table_->NewRedisWrite());
    CHECK_OK(redis_op->mutable_row()->SetBinary("key_column", key));
    auto* kv = redis_op->mutable_request()->mutable_set_request()->mutable_key_value();
    kv->set_key(key);
    kv->add_value(value);
    ASSERT_OK(session->Apply(redis_op.release()));
    ASSERT_OK(session->Flush());
  }

  void RedisSimpleSetCommands() {
    PutKeyValue("key123", "value123");
    PutKeyValue("key200", "value200");
    PutKeyValue("key300", "value300");
  }
};

TEST_F(RedisTableTest, SimpleRedisSetTest) {
  NO_FATALS(RedisSimpleSetCommands());
}

}  // namespace tablet
}  // namespace yb

// Copyright (c) YugaByte, Inc.

#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol-test-util.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/rpc/messenger.h"
#include "yb/tablet/tablet.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server-test-base.h"
#include "yb/util/cdc_test_util.h"

namespace yb {
namespace cdc {

using yb::rpc::RpcController;
using yb::tablet::TabletPeer;
using yb::tablet::enterprise::Tablet;
using yb::tserver::TabletServerTestBase;
using yb::tserver::WriteRequestPB;
using yb::tserver::WriteResponsePB;

class CDCServiceTest : public TabletServerTestBase {
 public:
  CDCServiceTest() : TabletServerTestBase(TableType::YQL_TABLE_TYPE) {}

 protected:
  void SetUp() override {
    TabletServerTestBase::SetUp();
    StartTabletServer();

    cdc_proxy_ = std::make_unique<CDCServiceProxy>(
        proxy_cache_.get(), HostPort::FromBoundEndpoint(mini_server_->bound_rpc_addr()));
  }

  std::unique_ptr<CDCServiceProxy> cdc_proxy_;
};

void AssertChangeRecords(const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& changes,
                         int32_t expected_int, std::string expected_str) {
  ASSERT_EQ(changes.size(), 2);
  ASSERT_EQ(changes[0].key(), "int_val");
  ASSERT_EQ(changes[0].value().int32_value(), expected_int);
  ASSERT_EQ(changes[1].key(), "string_val");
  ASSERT_EQ(changes[1].value().string_value(), expected_str);
}

TEST_F(CDCServiceTest, TestGetChanges) {
  // Verify that the tablet exists.
  std::shared_ptr<TabletPeer> tablet;
  ASSERT_TRUE(mini_server_->server()->tablet_manager()->LookupTablet(kTabletId, &tablet));

  // Insert test rows
  WriteRequestPB write_req;
  WriteResponsePB write_resp;
  write_req.set_tablet_id(kTabletId);

  {
    RpcController rpc;
    AddTestRowInsert(1, 11, "key1", &write_req);
    AddTestRowInsert(2, 22, "key2", &write_req);

    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy_->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 11), KeyValue(2, 22) }));

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(kTabletId);
  change_req.set_subscriber_uuid("subscriber_test");
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  // Get CDC changes.
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_EQ(change_resp.records_size(), 2);

    std::pair<int, std::string> expected_results[2] =
        {std::make_pair(11, "key1"), std::make_pair(22, "key2")};
    for (int i = 0; i < change_resp.records_size(); i++) {
      ASSERT_EQ(change_resp.records(i).operation(), CDCRecordPB::WRITE);

      // Check the key.
      ASSERT_NO_FATALS(AssertIntKey(change_resp.records(i).key(), i + 1));

      // Check the change records.
      ASSERT_NO_FATALS(AssertChangeRecords(change_resp.records(i).changes(),
                                           expected_results[i].first,
                                           expected_results[i].second));
    }
  }

  // Insert another row.
  {
    write_req.Clear();
    write_req.set_tablet_id(kTabletId);
    AddTestRowInsert(3, 33, "key3", &write_req);

    RpcController rpc;
    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy_->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 11), KeyValue(2, 22), KeyValue(3, 33) }));

  // Get next set of changes.
  // Copy checkpoint received from previous GetChanges CDC request.
  change_req.mutable_from_checkpoint()->CopyFrom(change_resp.checkpoint());
  change_resp.Clear();
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_EQ(change_resp.records_size(), 1);
    ASSERT_EQ(change_resp.records(0).operation(), CDCRecordPB_OperationType_WRITE);

    // Check the key.
    ASSERT_NO_FATALS(AssertIntKey(change_resp.records(0).key(), 3));

    // Check the change records.
    ASSERT_NO_FATALS(AssertChangeRecords(change_resp.records(0).changes(), 33, "key3"));
  }

  // Delete a row.
  {
    write_req.Clear();
    write_req.set_tablet_id(kTabletId);
    AddTestRowDelete(1, &write_req);

    RpcController rpc;
    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy_->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(2, 22), KeyValue(3, 33) }));

  // Get next set of changes.
  // Copy checkpoint received from previous GetChanges CDC request.
  change_req.mutable_from_checkpoint()->CopyFrom(change_resp.checkpoint());
  change_resp.Clear();
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_EQ(change_resp.records_size(), 1);
    ASSERT_EQ(change_resp.records(0).operation(), CDCRecordPB_OperationType_DELETE);

    // Check the key deleted.
    ASSERT_NO_FATALS(AssertIntKey(change_resp.records(0).key(), 1));
  }
}

} // namespace cdc
} // namespace yb

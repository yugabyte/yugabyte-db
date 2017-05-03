// Copyright (c) YugaByte, Inc.

#include <yb/rpc/rpc_introspection.pb.h>
#include "yb/cqlserver/cql_server.h"

#include "yb/util/flag_tags.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/cqlserver/cql_service.h"
#include "yb/rpc/messenger.h"

using yb::rpc::ServiceIf;

DEFINE_int32(cql_service_num_threads, 10,
             "Number of RPC worker threads for the CQL service");
TAG_FLAG(cql_service_num_threads, advanced);

DEFINE_int32(cql_service_queue_length, 50,
             "RPC queue length for CQL service");
TAG_FLAG(cql_service_queue_length, advanced);

DEFINE_int32(cql_nodelist_refresh_interval_secs, 60,
             "Interval after which a node list refresh event should be sent to all CQL clients.");
TAG_FLAG(cql_nodelist_refresh_interval_secs, advanced);

namespace yb {
namespace cqlserver {

namespace {

boost::posix_time::time_duration refresh_interval() {
  return boost::posix_time::seconds(FLAGS_cql_nodelist_refresh_interval_secs);
}

}

CQLServer::CQLServer(const CQLServerOptions& opts, boost::asio::io_service* io)
    : RpcAndWebServerBase("CQLServer", opts, "yb.cqlserver"),
      opts_(opts),
      timer_(*io, refresh_interval()) {
}

Status CQLServer::Start() {
  RETURN_NOT_OK(server::RpcAndWebServerBase::Init());

  gscoped_ptr<ServiceIf> cql_service(
      new CQLServiceImpl(this, messenger_, opts_));
  RETURN_NOT_OK(RegisterService(FLAGS_cql_service_queue_length, cql_service.Pass()));

  RETURN_NOT_OK(server::RpcAndWebServerBase::Start());

  // Start the CQL node list refresh timer.
  timer_.async_wait(boost::bind(&CQLServer::CQLNodeListRefresh, this,
                                boost::asio::placeholders::error));
  return Status::OK();
}

Status CQLServer::Shutdown() {
  boost::system::error_code ec;
  timer_.cancel(ec);
  if (ec) {
    LOG(WARNING) << "Failed to cancel timer: " << ec;
  }
  server::RpcAndWebServerBase::Shutdown();
  return Status::OK();
}

void CQLServer::CQLNodeListRefresh(const boost::system::error_code &e) {
  if (!e) {
    std::unique_ptr<EventResponse> event_response(
        new TopologyChangeEventResponse(TopologyChangeEventResponse::kMovedNode,
                                        first_rpc_address()));
    scoped_refptr<CQLServerEvent> server_event(new CQLServerEvent(std::move(event_response)));
    Status s = messenger_->QueueEventOnAllReactors(server_event);
    if (!s.ok()) {
      LOG(WARNING) << "Failed to send CQL node list refresh event: " << s.ToString();
    }

    // Reschedule the timer.
    boost::system::error_code ec;
    auto new_expires = timer_.expires_at() + refresh_interval();
    timer_.expires_at(new_expires, ec);
    if (ec) {
      LOG(WARNING) << "Failed to reschedule timer: " << ec;
    }
    timer_.async_wait(boost::bind(&CQLServer::CQLNodeListRefresh, this,
        boost::asio::placeholders::error));
  }
}

}  // namespace cqlserver
}  // namespace yb

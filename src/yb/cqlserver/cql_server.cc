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

CQLServer::CQLServer(const CQLServerOptions& opts,
                     boost::asio::io_service* io,
                     const tserver::TabletServer* const tserver)
    : RpcAndWebServerBase("CQLServer", opts, "yb.cqlserver"),
      opts_(opts),
      timer_(*io, refresh_interval()),
      tserver_(tserver) {
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

void CQLServer::Shutdown() {
  boost::system::error_code ec;
  timer_.cancel(ec);
  if (ec) {
    LOG(WARNING) << "Failed to cancel timer: " << ec;
  }
  server::RpcAndWebServerBase::Shutdown();
}

Status CQLServer::QueueEventForAllClients(std::unique_ptr<EventResponse> event_response) {
  scoped_refptr<CQLServerEvent> server_event(new CQLServerEvent(std::move(event_response)));
  RETURN_NOT_OK(messenger_->QueueEventOnAllReactors(server_event));
  return Status::OK();
}

void CQLServer::RescheduleTimer() {
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

void CQLServer::CQLNodeListRefresh(const boost::system::error_code &e) {
  if (!e) {
    Status s;
    if (tserver_ != nullptr) {
      // Get all live tservers.
      std::vector<master::TSInformationPB> live_tservers;
      s = tserver_->GetLiveTServers(&live_tservers);
      if (!s.ok()) {
        LOG (WARNING) << s.ToString();
        RescheduleTimer();
        return;
      }

      // Queue NEW_NODE event for all the live tservers.
      for (const master::TSInformationPB& ts_info : live_tservers) {
        if (ts_info.registration().common().rpc_addresses_size() == 0) {
          LOG (WARNING) << "Skipping TS since it doesn't have any rpc address: "
                        << ts_info.DebugString();
          continue;
        }

        // Use only the first rpc address.
        const yb::HostPortPB& hostport_pb = ts_info.registration().common().rpc_addresses(0);
        Sockaddr addr;
        s = addr.ParseString(hostport_pb.host(), hostport_pb.port());
        if (!s.ok()) {
          LOG (WARNING) << s.ToString();
          continue;
        }

        // Queue event for all clients.
        std::unique_ptr<EventResponse> event_response(
            new TopologyChangeEventResponse(TopologyChangeEventResponse::kNewNode,
                                            addr));
        s = QueueEventForAllClients(std::move(event_response));
        if (!s.ok()) {
          LOG (WARNING) << strings::Substitute("Failed to push event: $0, due to: $1",
                                               event_response->ToString(), s.ToString());
        }
      }
    }

    RescheduleTimer();
  }
}

}  // namespace cqlserver
}  // namespace yb

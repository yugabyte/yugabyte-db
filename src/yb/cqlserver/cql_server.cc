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

  std::unique_ptr<ServiceIf> cql_service(new CQLServiceImpl(this, messenger_, opts_));
  RETURN_NOT_OK(RegisterService(FLAGS_cql_service_queue_length, std::move(cql_service)));

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

std::unique_ptr<CQLServerEvent> CQLServer::BuildTopologyChangeEvent(
    const std::string& event_type, const Endpoint& addr) {
  std::unique_ptr<EventResponse> event_response(new TopologyChangeEventResponse(event_type, addr));
  std::unique_ptr<CQLServerEvent> cql_server_event(new CQLServerEvent(std::move(event_response)));
  return cql_server_event;
}

void CQLServer::CQLNodeListRefresh(const boost::system::error_code &e) {
  if (!e) {
    auto cqlserver_event_list = std::make_shared<CQLServerEventList>();
    if (tserver_ != nullptr) {
      // Get all live tservers.
      std::vector<master::TSInformationPB> live_tservers;
      Status s = tserver_->GetLiveTServers(&live_tservers);
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
        boost::system::error_code ec;
        Endpoint addr(IpAddress::from_string(hostport_pb.host(), ec), hostport_pb.port());
        if (ec) {
          LOG(WARNING) << strings::Substitute("Couldn't parse host $0, error: $1",
                                              hostport_pb.host(), ec.message());
          continue;
        }

        // Queue event for all clients to add a node.
        cqlserver_event_list->AddEvent(
            BuildTopologyChangeEvent(TopologyChangeEventResponse::kNewNode, addr));
      }
    }

    // Queue node refresh event, to remove any nodes that are down. Note that the 'MOVED_NODE'
    // event forces the client to refresh its entire cluster topology. The RPC address associated
    // with the event doesn't have much significance.
    cqlserver_event_list->AddEvent(
        BuildTopologyChangeEvent(TopologyChangeEventResponse::kMovedNode, first_rpc_address()));

    Status s = messenger_->QueueEventOnAllReactors(cqlserver_event_list);
    if (!s.ok()) {
      LOG (WARNING) << strings::Substitute("Failed to push events: [$0], due to: $1",
                                           cqlserver_event_list->ToString(), s.ToString());
    }

    RescheduleTimer();
  }
}

}  // namespace cqlserver
}  // namespace yb

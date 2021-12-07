// Copyright (c) YugaByte, Inc.
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

#include "yb/client/forward_rpc.h"

#include "yb/client/client.h"

#include "yb/common/wire_protocol.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/cast.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/trace.h"

using namespace std::placeholders;

DECLARE_bool(rpc_dump_all_traces);
DECLARE_bool(collect_end_to_end_traces);

namespace yb {

using std::shared_ptr;
using std::string;
using rpc::Rpc;
using tserver::WriteRequestPB;
using tserver::WriteResponsePB;
using tserver::ReadRequestPB;
using tserver::ReadResponsePB;
using tserver::TabletServerErrorPB;

namespace client {
namespace internal {

static CoarseTimePoint ComputeDeadline() {
  // TODO(Sudheer) : Make sure we pass the deadline from the PGGate layer and use that here.
  MonoDelta timeout = MonoDelta::FromSeconds(60);
  return CoarseMonoClock::now() + timeout;
}

template <class Req, class Resp>
ForwardRpc<Req, Resp>::ForwardRpc(const Req *req, Resp *res,
                                  rpc::RpcContext&& context,
                                  YBConsistencyLevel consistency_level,
                                  YBClient *client)
  : Rpc(ComputeDeadline(), client->messenger(), &client->proxy_cache()),
    req_(req),
    res_(res),
    context_(std::move(context)),
    trace_(new Trace),
    start_(MonoTime::Now()),
    tablet_invoker_(false /* local_tserver_only */,
                    consistency_level,
                    client,
                    this,
                    this,
                    nullptr /* tablet */,
                    nullptr /* table */,
                    mutable_retrier(),
                    trace_.get()) {
}

template <class Req, class Resp>
ForwardRpc<Req, Resp>::~ForwardRpc() {
  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took "
              << MonoTime::Now().GetDeltaSince(start_).ToMicroseconds()
              << "us. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

template <class Req, class Resp>
string ForwardRpc<Req, Resp>::ToString() const {
  return Format("$0(tablet: $1, num_attempts: $2)",
                read_only() ? "Read" : "Write",
                req_->tablet_id(),
                num_attempts());
}

template <class Req, class Resp>
void ForwardRpc<Req, Resp>::SendRpc() {
  TRACE_TO(trace_, "SendRpc() called.");
  retained_self_ = shared_from_this();
  tablet_invoker_.Execute(req_->tablet_id(), num_attempts() > 1);
}

template <class Req, class Resp>
void ForwardRpc<Req, Resp>::Finished(const Status& status) {
  Status new_status = status;
  if (tablet_invoker_.Done(&new_status)) {
    if (new_status.ok()) {
      PopulateResponse();
    }
    context_.RespondSuccess();
    retained_self_.reset();
  }
}

template <class Req, class Resp>
void ForwardRpc<Req, Resp>::Failed(const Status& status) {
  TabletServerErrorPB *err = res_->mutable_error();
  StatusToPB(status, err->mutable_status());
}

ForwardWriteRpc::ForwardWriteRpc(const WriteRequestPB *req,
                                 WriteResponsePB *res,
                                 rpc::RpcContext&& context,
                                 YBClient *client) :
  ForwardRpc(req, res, std::move(context), YBConsistencyLevel::STRONG, client) {

  // Ensure that only PGSQL operations are forwarded.
  DCHECK(!req->redis_write_batch_size() && !req->ql_write_batch_size());
}

ForwardWriteRpc::~ForwardWriteRpc() {
}

void ForwardWriteRpc::SendRpcToTserver(int attempt_num) {
  auto trace = trace_;
  TRACE_TO(trace, "SendRpcToTserver");
  ADOPT_TRACE(trace.get());

  tablet_invoker_.proxy()->WriteAsync(
      *req_, res_, PrepareController(),
      std::bind(&ForwardWriteRpc::Finished, this, Status::OK()));
  TRACE_TO(trace, "RpcDispatched Asynchronously");
}

void ForwardWriteRpc::PopulateResponse() {
  for (const auto& r : res_->pgsql_response_batch()) {
    if (r.has_rows_data_sidecar()) {
      Slice s = CHECK_RESULT(retrier().controller().GetSidecar(r.rows_data_sidecar()));
      context_.AddRpcSidecar(s);
    }
  }
}

ForwardReadRpc::ForwardReadRpc(const ReadRequestPB *req,
                               ReadResponsePB *res,
                               rpc::RpcContext&& context,
                               YBClient *client) :
  ForwardRpc(req, res, std::move(context), req->consistency_level(), client) {

  // Ensure that only PGSQL operations are forwarded.
  DCHECK(!req->redis_batch_size() && !req->ql_batch_size());
}


ForwardReadRpc::~ForwardReadRpc() {
}

void ForwardReadRpc::SendRpcToTserver(int attempt_num) {
  auto trace = trace_;
  TRACE_TO(trace, "SendRpcToTserver");
  ADOPT_TRACE(trace.get());

  tablet_invoker_.proxy()->ReadAsync(
      *req_, res_, PrepareController(),
      std::bind(&ForwardReadRpc::Finished, this, Status::OK()));
  TRACE_TO(trace, "RpcDispatched Asynchronously");
}

void ForwardReadRpc::PopulateResponse() {
  for (const auto& r : res_->pgsql_batch()) {
    if (r.has_rows_data_sidecar()) {
      Slice s = CHECK_RESULT(retrier().controller().GetSidecar(r.rows_data_sidecar()));
      context_.AddRpcSidecar(s);
    }
  }
}

}  // namespace internal
}  // namespace client
}  // namespace yb

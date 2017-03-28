// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_MASTER_TABLET_SERVICE_H
#define YB_MASTER_MASTER_TABLET_SERVICE_H

#include "yb/master/master.h"
#include "yb/master/master.service.h"
#include "yb/master/master_tserver.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tserver/tablet_service.h"
#include "yb/tserver/tserver.pb.h"

namespace yb {
namespace master {

// A subset of the TabletService supported by the Master to query specific tables.
class MasterTabletServiceImpl : public yb::tserver::TabletServiceImpl {
 public:

  MasterTabletServiceImpl(MasterTabletServer* server, Master* master);
  void Read(const tserver::ReadRequestPB* req, tserver::ReadResponsePB* resp,
                    rpc::RpcContext* context) override;

  void Write(const tserver::WriteRequestPB* req,
             tserver::WriteResponsePB* resp,
             rpc::RpcContext* context) override;

  void Scan(const tserver::ScanRequestPB* req,
            tserver::ScanResponsePB* resp,
            rpc::RpcContext* context) override;

  void NoOp(const tserver::NoOpRequestPB* req,
            tserver::NoOpResponsePB* resp,
            rpc::RpcContext* context) override;

  void ScannerKeepAlive(const tserver::ScannerKeepAliveRequestPB *req,
                        tserver::ScannerKeepAliveResponsePB *resp,
                        rpc::RpcContext *context) override;

  void ListTablets(const tserver::ListTabletsRequestPB* req,
                   tserver::ListTabletsResponsePB* resp,
                   rpc::RpcContext *context) override;

  void ListTabletsForTabletServer(const tserver::ListTabletsForTabletServerRequestPB* req,
                                  tserver::ListTabletsForTabletServerResponsePB* resp,
                                  rpc::RpcContext *context) override;

  void GetLogLocation(const tserver::GetLogLocationRequestPB* req,
                      tserver::GetLogLocationResponsePB* resp,
                      rpc::RpcContext *context) override;

  void Checksum(const tserver::ChecksumRequestPB* req,
                tserver::ChecksumResponsePB* resp,
                rpc::RpcContext *context) override;

 private:
  CHECKED_STATUS CheckLeaderAndGetTablet(
      const tserver::ReadRequestPB* req,
      tserver::ReadResponsePB* resp,
      rpc::RpcContext* context,
      std::shared_ptr<tablet::AbstractTablet>* tablet) override;

  template<typename ResponsePB>
  void HandleUnsupportedMethod(
      const char* method_name,
      ResponsePB* resp,
      rpc::RpcContext* context);


  Master *const master_;
  DISALLOW_COPY_AND_ASSIGN(MasterTabletServiceImpl);
};

} // namespace master
} // namespace yb
#endif // YB_MASTER_MASTER_TABLET_SERVICE_H

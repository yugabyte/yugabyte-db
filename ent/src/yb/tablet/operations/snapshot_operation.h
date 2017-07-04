//
// Copyright (c) YugaByte, Inc.
//

#ifndef ENT_SRC_YB_TABLET_OPERATIONS_SNAPSHOT_OPERATION_H
#define ENT_SRC_YB_TABLET_OPERATIONS_SNAPSHOT_OPERATION_H

#include <mutex>
#include <string>

#include "yb/gutil/macros.h"
#include "yb/tablet/operations/operation.h"
#include "yb/util/locks.h"

namespace yb {
namespace tablet {

// Operation Context for the CreateTabletSnapshot operation.
// Keeps track of the Operation states (request, result, ...)
class SnapshotOperationState : public OperationState {
 public:
  ~SnapshotOperationState() {}

  SnapshotOperationState(TabletPeer* tablet_peer,
                         const tserver::CreateTabletSnapshotRequestPB* request = nullptr,
                         tserver::CreateTabletSnapshotResponsePB* response = nullptr)
      : OperationState(tablet_peer),
        request_(request),
        response_(response) {
  }

  const tserver::CreateTabletSnapshotRequestPB* request() const override { return request_; }

  void UpdateRequestFromConsensusRound() override {
    request_ = consensus_round()->replicate_msg()->mutable_snapshot_request();
  }

  tserver::CreateTabletSnapshotResponsePB* response() override { return response_; }

  void AcquireSchemaLock(rw_semaphore* l);

  // Release the acquired schema lock.
  // Crashes if the lock was not already acquired.
  void ReleaseSchemaLock();

  // Note: request_ and response_ are set to NULL after this method returns.
  void Finish() {
    // Make the request NULL since after this operation commits
    // the request may be deleted at any moment.
    request_ = NULL;
    response_ = NULL;
  }

  std::string ToString() const override;

 private:

  // The original RPC request and response.
  const tserver::CreateTabletSnapshotRequestPB *request_;
  tserver::CreateTabletSnapshotResponsePB *response_;

  // The lock held on the tablet's schema_lock_.
  std::unique_lock<rw_semaphore> schema_lock_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotOperationState);
};

// Executes the CreateTabletSnapshot operation.
class SnapshotOperation : public Operation {
 public:
  SnapshotOperation(std::unique_ptr<SnapshotOperationState> tx_state,
                    consensus::DriverType type);

  SnapshotOperationState* state() override {
    return down_cast<SnapshotOperationState*>(Operation::state());
  }

  const SnapshotOperationState* state() const override {
    return down_cast<const SnapshotOperationState*>(Operation::state());
  }

  consensus::ReplicateMsgPtr NewReplicateMsg() override;

  CHECKED_STATUS Prepare() override;

  // Starts the CreateTabletSnapshot operation by assigning it a timestamp.
  void Start() override;

  // Executes an Apply for the CreateTabletSnapshot operation
  CHECKED_STATUS Apply(gscoped_ptr<consensus::CommitMsg>* commit_msg) override;

  // Actually commits the operation.
  void Finish(OperationResult result) override;

  std::string ToString() const override;

 private:
  gscoped_ptr<SnapshotOperationState> state_;
  DISALLOW_COPY_AND_ASSIGN(SnapshotOperation);
};

}  // namespace tablet
}  // namespace yb

#endif  // ENT_SRC_YB_TABLET_OPERATIONS_SNAPSHOT_OPERATION_H

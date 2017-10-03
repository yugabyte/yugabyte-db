// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef KUDU_TABLET_TABLET_PEER_H_
#define KUDU_TABLET_TABLET_PEER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "kudu/consensus/consensus.h"
#include "kudu/consensus/log.h"
#include "kudu/gutil/callback.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/transaction_order_verifier.h"
#include "kudu/tablet/transactions/transaction_tracker.h"
#include "kudu/util/metrics.h"
#include "kudu/util/semaphore.h"

namespace kudu {

namespace log {
class LogAnchorRegistry;
}

namespace rpc {
class Messenger;
}

namespace tserver {
class CatchUpServiceTest;
}

class MaintenanceManager;
class MaintenanceOp;

namespace tablet {
class LeaderTransactionDriver;
class ReplicaTransactionDriver;
class TabletPeer;
class TabletStatusPB;
class TabletStatusListener;
class TransactionDriver;

// A peer in a tablet consensus configuration, which coordinates writes to tablets.
// Each time Write() is called this class appends a new entry to a replicated
// state machine through a consensus algorithm, which makes sure that other
// peers see the same updates in the same order. In addition to this, this
// class also splits the work and coordinates multi-threaded execution.
class TabletPeer : public RefCountedThreadSafe<TabletPeer>,
                   public consensus::ReplicaTransactionFactory {
 public:
  typedef std::map<int64_t, int64_t> MaxIdxToSegmentSizeMap;

  TabletPeer(const scoped_refptr<TabletMetadata>& meta,
             const consensus::RaftPeerPB& local_peer_pb, ThreadPool* apply_pool,
             Callback<void(const std::string& reason)> mark_dirty_clbk);

  // Initializes the TabletPeer, namely creating the Log and initializing
  // Consensus.
  Status Init(const std::shared_ptr<tablet::Tablet>& tablet,
              const scoped_refptr<server::Clock>& clock,
              const std::shared_ptr<rpc::Messenger>& messenger,
              const scoped_refptr<log::Log>& log,
              const scoped_refptr<MetricEntity>& metric_entity);

  // Starts the TabletPeer, making it available for Write()s. If this
  // TabletPeer is part of a consensus configuration this will connect it to other peers
  // in the consensus configuration.
  Status Start(const consensus::ConsensusBootstrapInfo& info);

  // Shutdown this tablet peer.
  // If a shutdown is already in progress, blocks until that shutdown is complete.
  void Shutdown();

  // Check that the tablet is in a RUNNING state.
  Status CheckRunning() const;

  // Wait until the tablet is in a RUNNING state or if there's a timeout.
  // TODO have a way to wait for any state?
  Status WaitUntilConsensusRunning(const MonoDelta& timeout);

  // Submits a write to a tablet and executes it asynchronously.
  // The caller is expected to build and pass a TrasactionContext that points
  // to the RPC WriteRequest, WriteResponse, RpcContext and to the tablet's
  // MvccManager.
  Status SubmitWrite(WriteTransactionState *tx_state);

  // Called by the tablet service to start an alter schema transaction.
  //
  // The transaction contains all the information required to execute the
  // AlterSchema operation and send the response back.
  //
  // If the returned Status is OK, the response to the client will be sent
  // asynchronously. Otherwise the tablet service will have to send the response directly.
  //
  // The AlterSchema operation is taking the tablet component lock in exclusive mode
  // meaning that no other operation on the tablet can be executed while the
  // AlterSchema is in progress.
  Status SubmitAlterSchema(gscoped_ptr<AlterSchemaTransactionState> tx_state);

  void GetTabletStatusPB(TabletStatusPB* status_pb_out) const;

  // Used by consensus to create and start a new ReplicaTransaction.
  virtual Status StartReplicaTransaction(
      const scoped_refptr<consensus::ConsensusRound>& round) OVERRIDE;

  consensus::Consensus* consensus() {
    boost::lock_guard<simple_spinlock> lock(lock_);
    return consensus_.get();
  }

  scoped_refptr<consensus::Consensus> shared_consensus() const {
    boost::lock_guard<simple_spinlock> lock(lock_);
    return consensus_;
  }

  Tablet* tablet() const {
    boost::lock_guard<simple_spinlock> lock(lock_);
    return tablet_.get();
  }

  std::shared_ptr<Tablet> shared_tablet() const {
    boost::lock_guard<simple_spinlock> lock(lock_);
    return tablet_;
  }

  const TabletStatePB state() const {
    boost::lock_guard<simple_spinlock> lock(lock_);
    return state_;
  }

  // Returns the current Raft configuration.
  const consensus::RaftConfigPB RaftConfig() const;

  // If any peers in the consensus configuration lack permanent uuids, get them via an
  // RPC call and update.
  // TODO: move this to raft_consensus.h.
  Status UpdatePermanentUuids();

  TabletStatusListener* status_listener() const {
    return status_listener_.get();
  }

  // Sets the tablet to a BOOTSTRAPPING state, indicating it is starting up.
  void SetBootstrapping() {
    boost::lock_guard<simple_spinlock> lock(lock_);
    CHECK_EQ(NOT_STARTED, state_);
    state_ = BOOTSTRAPPING;
  }

  // sets the tablet state to FAILED additionally setting the error to the provided
  // one.
  void SetFailed(const Status& error) {
    boost::lock_guard<simple_spinlock> lock(lock_);
    state_ = FAILED;
    error_ = error;
  }

  // Returns the error that occurred, when state is FAILED.
  Status error() const {
    boost::lock_guard<simple_spinlock> lock(lock_);
    return error_;
  }

  // Returns a human-readable string indicating the state of the tablet.
  // Typically this looks like "NOT_STARTED", "TABLET_DATA_COPYING",
  // etc. For use in places like the Web UI.
  std::string HumanReadableState() const;

  // Adds list of transactions in-flight at the time of the call to
  // 'out'. TransactionStatusPB objects are used to allow this method
  // to be used by both the web-UI and ts-cli.
  void GetInFlightTransactions(Transaction::TraceType trace_type,
                               std::vector<consensus::TransactionStatusPB>* out) const;

  // Returns the minimum known log index that is in-memory or in-flight.
  // Used for selection of log segments to delete during Log GC.
  void GetEarliestNeededLogIndex(int64_t* log_index) const;

  // Returns a map of log index -> segment size, of all the segments that currently cannot be GCed
  // because in-memory structures have anchors in them.
  //
  // Returns a non-ok status if the tablet isn't running.
  Status GetMaxIndexesToSegmentSizeMap(MaxIdxToSegmentSizeMap* idx_size_map) const;

  // Returns the amount of bytes that would be GC'd if RunLogGC() was called.
  //
  // Returns a non-ok status if the tablet isn't running.
  Status GetGCableDataSize(int64_t* retention_size) const;

  // Return a pointer to the Log.
  // TabletPeer keeps a reference to Log after Init().
  log::Log* log() const {
    return log_.get();
  }

  server::Clock* clock() {
    return clock_.get();
  }

  const scoped_refptr<log::LogAnchorRegistry>& log_anchor_registry() const {
    return log_anchor_registry_;
  }

  // Returns the tablet_id of the tablet managed by this TabletPeer.
  // Returns the correct tablet_id even if the underlying tablet is not available
  // yet.
  const std::string& tablet_id() const { return tablet_id_; }

  // Convenience method to return the permanent_uuid of this peer.
  std::string permanent_uuid() const { return tablet_->metadata()->fs_manager()->uuid(); }

  Status NewLeaderTransactionDriver(gscoped_ptr<Transaction> transaction,
                                    scoped_refptr<TransactionDriver>* driver);

  Status NewReplicaTransactionDriver(gscoped_ptr<Transaction> transaction,
                                     scoped_refptr<TransactionDriver>* driver);

  // Tells the tablet's log to garbage collect.
  Status RunLogGC();

  // Register the maintenance ops associated with this peer's tablet, also invokes
  // Tablet::RegisterMaintenanceOps().
  void RegisterMaintenanceOps(MaintenanceManager* maintenance_manager);

  // Unregister the maintenance ops associated with this peer's tablet.
  // This method is not thread safe.
  void UnregisterMaintenanceOps();

  // Return pointer to the transaction tracker for this peer.
  const TransactionTracker* transaction_tracker() const { return &txn_tracker_; }

  const scoped_refptr<TabletMetadata>& tablet_metadata() const {
    return meta_;
  }

 private:
  friend class RefCountedThreadSafe<TabletPeer>;
  friend class TabletPeerTest;
  FRIEND_TEST(TabletPeerTest, TestMRSAnchorPreventsLogGC);
  FRIEND_TEST(TabletPeerTest, TestDMSAnchorPreventsLogGC);
  FRIEND_TEST(TabletPeerTest, TestActiveTransactionPreventsLogGC);

  ~TabletPeer();

  // Wait until the TabletPeer is fully in SHUTDOWN state.
  void WaitUntilShutdown();

  // After bootstrap is complete and consensus is setup this initiates the transactions
  // that were not complete on bootstrap.
  // Not implemented yet. See .cc file.
  Status StartPendingTransactions(consensus::RaftPeerPB::Role my_role,
                                  const consensus::ConsensusBootstrapInfo& bootstrap_info);

  const scoped_refptr<TabletMetadata> meta_;

  const std::string tablet_id_;

  const consensus::RaftPeerPB local_peer_pb_;

  TabletStatePB state_;
  Status error_;
  TransactionTracker txn_tracker_;
  TransactionOrderVerifier txn_order_verifier_;
  scoped_refptr<log::Log> log_;
  std::shared_ptr<Tablet> tablet_;
  std::shared_ptr<rpc::Messenger> messenger_;
  scoped_refptr<consensus::Consensus> consensus_;
  gscoped_ptr<TabletStatusListener> status_listener_;
  simple_spinlock prepare_replicate_lock_;

  // Lock protecting state_ as well as smart pointers to collaborating
  // classes such as tablet_ and consensus_.
  mutable simple_spinlock lock_;

  // Lock taken during Init/Shutdown which ensures that only a single thread
  // attempts to perform major lifecycle operations (Init/Shutdown) at once.
  // This must be acquired before acquiring lock_ if they are acquired together.
  // We don't just use lock_ since the lifecycle operations may take a while
  // and we'd like other threads to be able to quickly poll the state_ variable
  // during them in order to reject RPCs, etc.
  mutable simple_spinlock state_change_lock_;

  // IMPORTANT: correct execution of PrepareTask assumes that 'prepare_pool_'
  // is single-threaded, moving to a multi-tablet setup where multiple TabletPeers
  // use the same 'prepare_pool_' needs to enforce that, for a single
  // TabletPeer, PrepareTasks are executed *serially*.
  // TODO move the prepare pool to TabletServer.
  gscoped_ptr<ThreadPool> prepare_pool_;

  // Pool that executes apply tasks for transactions. This is a multi-threaded
  // pool, constructor-injected by either the Master (for system tables) or
  // the Tablet server.
  ThreadPool* apply_pool_;

  scoped_refptr<server::Clock> clock_;

  scoped_refptr<log::LogAnchorRegistry> log_anchor_registry_;

  // Function to mark this TabletPeer's tablet as dirty in the TSTabletManager.
  // This function must be called any time the cluster membership or cluster
  // leadership changes.
  Callback<void(const std::string& reason)> mark_dirty_clbk_;

  // List of maintenance operations for the tablet that need information that only the peer
  // can provide.
  std::vector<MaintenanceOp*> maintenance_ops_;

  DISALLOW_COPY_AND_ASSIGN(TabletPeer);
};

// A callback to wait for the in-flight transactions to complete and to flush
// the Log when they do.
// Tablet is passed as a raw pointer as this callback is set in TabletMetadata and
// were we to keep the tablet as a shared_ptr a circular dependency would occur:
// callback->tablet->metadata->callback. Since the tablet indirectly owns this
// callback we know that is must still be alive when it fires.
class FlushInflightsToLogCallback : public RefCountedThreadSafe<FlushInflightsToLogCallback> {
 public:
  FlushInflightsToLogCallback(Tablet* tablet,
                              const scoped_refptr<log::Log>& log)
   : tablet_(tablet),
     log_(log) {}

  Status WaitForInflightsAndFlushLog();

 private:
  Tablet* tablet_;
  scoped_refptr<log::Log> log_;
};


}  // namespace tablet
}  // namespace kudu

#endif /* KUDU_TABLET_TABLET_PEER_H_ */

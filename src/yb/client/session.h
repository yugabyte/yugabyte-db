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

#ifndef YB_CLIENT_SESSION_H
#define YB_CLIENT_SESSION_H

#include <future>
#include <unordered_set>

#include "yb/client/client_fwd.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/common_fwd.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/locks.h"
#include "yb/util/monotime.h"

namespace yb {

class ConsistentReadPoint;

struct ReadHybridTime;

namespace client {

namespace internal {
class Batcher;
class ErrorCollector;
} // internal

YB_STRONGLY_TYPED_BOOL(Restart);

struct NODISCARD_CLASS FlushStatus {
  Status status = Status::OK();
  // Contains more detailed per-operation list of errors if status is not OK.
  CollectedErrors errors;
};

// A YBSession belongs to a specific YBClient, and represents a context in
// which all read/write data access should take place. Within a session,
// multiple operations may be accumulated and batched together for better
// efficiency. Settings like timeouts, priorities, and trace IDs are also set
// per session.
//
// A YBSession's main purpose is for grouping together multiple data-access
// operations together into batches or transactions. It is important to note
// the distinction between these two:
//
// * A batch is a set of operations which are grouped together in order to
//   amortize fixed costs such as RPC call overhead and round trip times.
//   A batch DOES NOT imply any ACID-like guarantees. Within a batch, some
//   operations may succeed while others fail, and concurrent readers may see
//   partial results. If the client crashes mid-batch, it is possible that some
//   of the operations will be made durable while others were lost.
//
// * In contrast, a transaction is a set of operations which are treated as an
//   indivisible semantic unit, per the usual definitions of database transactions
//   and isolation levels.
//
// YBSession is separate from YBTable because a given batch or transaction
// may span multiple tables. Even in the context of batching and not multi-table
// ACID transactions, we may be able to coalesce writes to different tables hosted
// on the same server into the same RPC.
//
// YBSession is separate from YBClient because, in a multi-threaded
// application, different threads may need to concurrently execute
// transactions. Similar to a JDBC "session", transaction boundaries will be
// delineated on a per-session basis -- in between a "BeginTransaction" and
// "Commit" call on a given session, all operations will be part of the same
// transaction. Meanwhile another concurrent Session object can safely run
// non-transactional work or other transactions without interfering.
//
// Additionally, there is a guarantee that writes from different sessions do not
// get batched together into the same RPCs -- this means that latency-sensitive
// clients can run through the same YBClient object as throughput-oriented
// clients, perhaps by setting the latency-sensitive session's timeouts low and
// priorities high. Without the separation of batches, a latency-sensitive
// single-row insert might get batched along with 10MB worth of inserts from the
// batch writer, thus delaying the response significantly.
//
// Users who are familiar with the Hibernate ORM framework should find this
// concept of a Session familiar.
//
// This class is not thread-safe.
class YBSession : public std::enable_shared_from_this<YBSession> {
 public:
  explicit YBSession(YBClient* client, const scoped_refptr<ClockBase>& clock = nullptr);

  ~YBSession();

  // Set the consistent read point used by the non-transactional operations in this session. If the
  // operations are restarted and last read point indicates the operations do need to be restarted,
  // the read point will be updated to restart read-time. Otherwise, the read point will be set to
  // the current time.
  void RestartNonTxnReadPoint(Restart restart);

  // Sets read point for this session. When tablet_id is specified, then local limit from read_time
  // will be used for local limit of this tablet.
  void SetReadPoint(const ReadHybridTime& read_time, const TabletId& tablet_id = TabletId());

  // Returns true if our current read point requires restart.
  bool IsRestartRequired();

  // Defer the read hybrid time to the global limit.  Since the global limit should not change for a
  // session, this call is idempotent.
  void DeferReadPoint();

  // Changes transaction used by this session.
  void SetTransaction(YBTransactionPtr transaction);

  // Set the timeout for writes made in this session.
  void SetTimeout(MonoDelta delta);

  void SetDeadline(CoarseTimePoint deadline);

  // TODO: add "doAs" ability here for proxy servers to be able to act on behalf of
  // other users, assuming access rights.

  // Apply the write operation.
  //
  // Applied operations just added to the session and waits to be flushed.
  void Apply(YBOperationPtr yb_op);

  bool IsInProgress(YBOperationPtr yb_op) const;

  void Apply(const std::vector<YBOperationPtr>& ops);

  // Flush any pending writes.
  //
  // Returns a bad status if session failed to resolve tablets for at least some operations or
  // if there are any pending errors after operations have been flushed.
  // FlushAndGetOpsErrors could be used instead of Flush to get info about which specific
  // operations failed.
  //
  // Async version invokes callback as soon as all operations have been flushed and passes
  // general status and which specific operations failed.
  //
  // In the case that the async version of this method is used, then the callback
  // will be called upon completion of the operations which were buffered since the
  // last flush. In other words, in the following sequence:
  //
  //    session->Apply(a);
  //    session->FlushAsync(callback_1);
  //    session->Apply(b);
  //    session->FlushAsync(callback_2);
  //
  // ... 'callback_2' will be triggered once 'b' has been inserted, regardless of whether
  // 'a' has completed or not.
  //
  // Note that this also means that, if FlushAsync is called twice in succession, with
  // no intervening operations, the second flush will return immediately. For example:
  //
  //    session->Insert(a);
  //    session->FlushAsync(callback_1); // called when 'a' is inserted
  //    session->FlushAsync(callback_2); // called immediately!
  //
  // Note that, as in all other async functions in YB, the callback may be called
  // either from an IO thread or the same thread which calls FlushAsync. The callback
  // should not block.
  //
  // For FlushAsync, 'callback' must remain valid until it is invoked.
  void FlushAsync(FlushCallback callback);
  std::future<FlushStatus> FlushFuture();

  // For production code use async variants of the following functions instead.
  FlushStatus TEST_FlushAndGetOpsErrors();
  Status TEST_Flush();
  Status TEST_ApplyAndFlush(YBOperationPtr yb_op);
  Status TEST_ApplyAndFlush(const std::vector<YBOperationPtr>& ops);
  Status TEST_ReadSync(std::shared_ptr<YBOperation> yb_op);

  // Abort the unflushed or in-flight operations in the session.
  void Abort();

  // Close the session.
  // Returns Status::IllegalState() if 'force' is false and there are still pending
  // operations. If 'force' is true batcher_ is aborted even if there are pending
  // operations.
  Status Close(bool force = false);

  // Return true if there are operations which have not yet been delivered to the
  // cluster. This may include buffered operations (i.e those that have not yet been
  // flushed) as well as in-flight operations (i.e those that are in the process of
  // being sent to the servers).
  // TODO: maybe "incomplete" or "undelivered" is clearer?
  bool TEST_HasPendingOperations() const;

  // Return the number of buffered operations. These are operations that have
  // not yet been flushed - i.e they are not en-route yet.
  //
  // Note that this is different than TEST_HasPendingOperations() above, which includes
  // operations which have been sent and not yet responded to.
  size_t TEST_CountBufferedOperations() const;

  // Returns true if this session has not flushed operations.
  bool HasNotFlushedOperations() const;

  // Allow local calls to run in the current thread.
  void set_allow_local_calls_in_curr_thread(bool flag);
  bool allow_local_calls_in_curr_thread() const;

  // Sets in transaction read limit for this session.
  void SetInTxnLimit(HybridTime value);

  YBClient* client() const;

  // Sets force consistent read mode, if true then consistent read point will be used even we have
  // only one command to flush.
  // It is useful when whole statement is executed using multiple flushes.
  void SetForceConsistentRead(ForceConsistentRead value);

  const internal::AsyncRpcMetricsPtr& async_rpc_metrics() const {
    return async_rpc_metrics_;
  }

  // Called by Batcher when a flush has started/finished.
  void FlushStarted(internal::BatcherPtr batcher);
  void FlushFinished(internal::BatcherPtr batcher);

  ConsistentReadPoint* read_point();

  void SetRejectionScoreSource(RejectionScoreSourcePtr rejection_score_source);

  struct BatcherConfig {
    std::weak_ptr<YBSession> session;
    client::YBClient* client;
    YBTransactionPtr transaction;
    std::shared_ptr<ConsistentReadPoint> non_transactional_read_point;
    bool allow_local_calls_in_curr_thread = true;
    bool force_consistent_read = false;
    RejectionScoreSourcePtr rejection_score_source;

    ConsistentReadPoint* read_point() const;
  };

 private:
  friend class YBClient;
  friend class internal::Batcher;

  internal::Batcher& Batcher();

  BatcherConfig batcher_config_;

  // Lock protecting flushed_batchers_.
  mutable simple_spinlock lock_;

  // The current batcher being prepared.
  internal::BatcherPtr batcher_;

  // Any batchers which have been flushed but not yet finished.
  //
  // Upon a batch finishing, it will call FlushFinished(), which removes the batcher from
  // this set. This set does not hold any reference count to the Batcher, since, while
  // the flush is active, the batcher manages its own refcount. The Batcher will always
  // call FlushFinished() before it destructs itself, so we're guaranteed that these
  // pointers stay valid.
  std::unordered_set<internal::BatcherPtr> flushed_batchers_;

  // Session only one of deadline and timeout could be active.
  // When new batcher is created its deadline is set as session deadline or
  // current time + session timeout.
  CoarseTimePoint deadline_;
  MonoDelta timeout_;

  internal::AsyncRpcMetricsPtr async_rpc_metrics_;

  DISALLOW_COPY_AND_ASSIGN(YBSession);
};

// In case of tablet splitting YBSession can flush an operation to an outdated tablet and this can
// be retried by the session internally without returning error to upper layers.
bool ShouldSessionRetryError(const Status& status);

int YsqlClientReadWriteTimeoutMs();
int SysCatalogRetryableRequestTimeoutSecs();
int RetryableRequestTimeoutSecs(TableType table_type);

} // namespace client
} // namespace yb

#endif // YB_CLIENT_SESSION_H

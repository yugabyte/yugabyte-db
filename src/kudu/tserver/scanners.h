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
#ifndef KUDU_TSERVER_SCANNERS_H
#define KUDU_TSERVER_SCANNERS_H

#include <boost/thread/shared_mutex.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "kudu/common/iterator_stats.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/util/auto_release_pool.h"
#include "kudu/util/memory/arena.h"
#include "kudu/util/metrics.h"
#include "kudu/util/monotime.h"
#include "kudu/util/oid_generator.h"

namespace kudu {

class MetricEntity;
class RowwiseIterator;
class ScanSpec;
class Schema;
class Status;
class Thread;

struct IteratorStats;

namespace tserver {

class Scanner;
struct ScannerMetrics;
typedef std::shared_ptr<Scanner> SharedScanner;

// Manages the live scanners within a Tablet Server.
//
// When a scanner is created by a client, it is assigned a unique scanner ID.
// The client may then use this ID to fetch more rows from the scanner
// or close it.
//
// Since scanners keep resources on the server, the manager periodically
// removes any scanners which have not been accessed since a configurable TTL.
class ScannerManager {
 public:
  explicit ScannerManager(const scoped_refptr<MetricEntity>& metric_entity);
  ~ScannerManager();

  // Starts the expired scanner removal thread.
  Status StartRemovalThread();

  // Create a new scanner with a unique ID, inserting it into the map.
  void NewScanner(const scoped_refptr<tablet::TabletPeer>& tablet_peer,
                  const std::string& requestor_string,
                  SharedScanner* scanner);

  // Lookup the given scanner by its ID.
  // Returns true if the scanner is found successfully.
  bool LookupScanner(const std::string& scanner_id, SharedScanner* scanner);

  // Unregister the given scanner by its ID.
  // Returns true if unregistered successfully.
  bool UnregisterScanner(const std::string& scanner_id);

  // Return the number of scanners currently active.
  // Note this method will not return accurate value
  // if under concurrent modifications.
  size_t CountActiveScanners() const;

  // List all active scanners.
  // Note this method will not return a consistent view
  // of all active scanners if under concurrent modifications.
  void ListScanners(std::vector<SharedScanner>* scanners);

  // Iterate through scanners and remove any which are past their TTL.
  void RemoveExpiredScanners();

 private:
  FRIEND_TEST(ScannerTest, TestExpire);

  enum {
    kNumScannerMapStripes = 32
  };

  typedef std::unordered_map<std::string, SharedScanner> ScannerMap;

  typedef std::pair<std::string, SharedScanner> ScannerMapEntry;

  struct ScannerMapStripe {
    // Lock protecting the scanner map.
    mutable boost::shared_mutex lock_;
    // Map of the currently active scanners.
    ScannerMap scanners_by_id_;
  };

  // Periodically call RemoveExpiredScanners().
  void RunRemovalThread();

  ScannerMapStripe& GetStripeByScannerId(const string& scanner_id);

  // (Optional) scanner metrics for this instance.
  gscoped_ptr<ScannerMetrics> metrics_;

  // If true, removal thread should shut itself down. Protected
  // by 'shutdown_lock_' and 'shutdown_cv_'.
  bool shutdown_;
  mutable boost::mutex shutdown_lock_;
  boost::condition_variable shutdown_cv_;

  std::vector<ScannerMapStripe*> scanner_maps_;

  // Generator for scanner IDs.
  ObjectIdGenerator oid_generator_;

  // Thread to remove expired scanners.
  scoped_refptr<kudu::Thread> removal_thread_;

  FunctionGaugeDetacher metric_detacher_;

  DISALLOW_COPY_AND_ASSIGN(ScannerManager);
};

// RAII wrapper to unregister a scanner upon scope exit.
class ScopedUnregisterScanner {
 public:
  ScopedUnregisterScanner(ScannerManager* mgr, std::string id)
      : mgr_(mgr), id_(std::move(id)), cancelled_(false) {}

  ~ScopedUnregisterScanner() {
    if (!cancelled_) {
      mgr_->UnregisterScanner(id_);
    }
  }

  // Do not unregister the scanner when the scope is exited.
  void Cancel() {
    cancelled_ = true;
  }

 private:
  ScannerManager* const mgr_;
  const std::string id_;
  bool cancelled_;
};

// An open scanner on the server side.
class Scanner {
 public:
  explicit Scanner(std::string id,
                   const scoped_refptr<tablet::TabletPeer>& tablet_peer,
                   std::string requestor_string, ScannerMetrics* metrics);
  ~Scanner();

  // Attach an actual iterator and a ScanSpec to this Scanner.
  // Takes ownership of 'iter' and 'spec'.
  void Init(gscoped_ptr<RowwiseIterator> iter,
            gscoped_ptr<ScanSpec> spec);

  // Return true if the scanner has been initialized (i.e has an iterator).
  // Once a Scanner is initialized, it is safe to assume that iter() and spec()
  // return non-NULL for the lifetime of the Scanner object.
  bool IsInitialized() const {
    boost::lock_guard<simple_spinlock> l(lock_);
    return iter_ != NULL;
  }

  RowwiseIterator* iter() {
    return DCHECK_NOTNULL(iter_.get());
  }

  const RowwiseIterator* iter() const {
    return DCHECK_NOTNULL(iter_.get());
  }

  // Update the last-access time to the current time,
  // delaying the expiration of the Scanner for another TTL
  // period.
  void UpdateAccessTime();

  // Return the auto-release pool which will be freed when this scanner
  // closes. This can be used as a storage area for the ScanSpec and any
  // associated data (eg storage for its predicates).
  AutoReleasePool* autorelease_pool() {
    return &autorelease_pool_;
  }

  Arena* arena() {
    return &arena_;
  }

  const std::string& id() const { return id_; }

  // Return the ScanSpec associated with this Scanner.
  const ScanSpec& spec() const;

  const std::string tablet_id() const {
    // scanners-test passes a null tablet_peer.
    return tablet_peer_ ? tablet_peer_->tablet_id() : "null tablet";
  }

  const scoped_refptr<tablet::TabletPeer>& tablet_peer() const { return tablet_peer_; }

  const std::string& requestor_string() const { return requestor_string_; }

  // Returns the current call sequence ID of the scanner.
  uint32_t call_seq_id() const {
    boost::lock_guard<simple_spinlock> l(lock_);
    return call_seq_id_;
  }

  // Increments the call sequence ID.
  void IncrementCallSeqId() {
    boost::lock_guard<simple_spinlock> l(lock_);
    call_seq_id_ += 1;
  }

  // Return the delta from the last time this scan was updated to 'now'.
  MonoDelta TimeSinceLastAccess(const MonoTime& now) const {
    boost::lock_guard<simple_spinlock> l(lock_);
    return now.GetDeltaSince(last_access_time_);
  }

  // Returns the time this scan was started.
  const MonoTime& start_time() const { return start_time_; }

  // Associate a projection schema with the Scanner. The scanner takes
  // ownership of 'client_projection_schema'.
  //
  // Note: 'client_projection_schema' is set if the client's
  // projection is a subset of the iterator's schema -- the iterator's
  // schema needs to include all columns that have predicates, whereas
  // the client may not want to project all of them.
  void set_client_projection_schema(gscoped_ptr<Schema> client_projection_schema) {
    client_projection_schema_.swap(client_projection_schema);
  }

  // Returns request's projection schema if it differs from the schema
  // used by the iterator (which must contain all columns used as
  // predicates). Returns NULL if the iterator's schema is the same as
  // the projection schema.
  // See the note about 'set_client_projection_schema' above.
  const Schema* client_projection_schema() const { return client_projection_schema_.get(); }

  // Get per-column stats for each iterator.
  void GetIteratorStats(std::vector<IteratorStats>* stats) const;

  const IteratorStats& already_reported_stats() const {
    return already_reported_stats_;
  }
  void set_already_reported_stats(const IteratorStats& stats) {
    already_reported_stats_ = stats;
  }

 private:
  friend class ScannerManager;

  // The unique ID of this scanner.
  const std::string id_;

  // Tablet associated with the scanner.
  const scoped_refptr<tablet::TabletPeer> tablet_peer_;

  // Information about the requestor. Populated from
  // RpcContext::requestor_string().
  const std::string requestor_string_;

  // The last time that the scanner was accessed.
  MonoTime last_access_time_;

  // The current call sequence ID.
  uint32_t call_seq_id_;

  // Protects last_access_time_ call_seq_id_, iter_, and spec_.
  mutable simple_spinlock lock_;

  // The time the scanner was started.
  const MonoTime start_time_;

  // (Optional) scanner metrics struct, for recording scanner's duration.
  ScannerMetrics* metrics_;

  // A summary of the statistics already reported to the metrics system
  // for this scanner. This allows us to report the metrics incrementally
  // as the scanner proceeds.
  IteratorStats already_reported_stats_;

  // The spec used by 'iter_'
  gscoped_ptr<ScanSpec> spec_;

  // Stores the request's projection schema, if it differs from the
  // schema used by the iterator.
  gscoped_ptr<Schema> client_projection_schema_;

  gscoped_ptr<RowwiseIterator> iter_;

  AutoReleasePool autorelease_pool_;

  // Arena used for allocations which must last as long as the scanner
  // itself. This is _not_ used for row data, which is scoped to a single RPC
  // response.
  Arena arena_;

  DISALLOW_COPY_AND_ASSIGN(Scanner);
};


} // namespace tserver
} // namespace kudu

#endif

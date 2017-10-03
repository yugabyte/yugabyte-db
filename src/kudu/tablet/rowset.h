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
#ifndef KUDU_TABLET_ROWSET_H
#define KUDU_TABLET_ROWSET_H

#include <boost/thread/mutex.hpp>
#include <memory>
#include <string>
#include <vector>

#include "kudu/cfile/cfile_util.h"
#include "kudu/common/iterator.h"
#include "kudu/common/rowid.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/macros.h"
#include "kudu/tablet/mvcc.h"
#include "kudu/util/bloom_filter.h"
#include "kudu/util/faststring.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

namespace kudu {

class RowChangeList;

namespace consensus {
class OpId;
}

namespace tablet {

class CompactionInput;
class OperationResultPB;
class MvccSnapshot;
class RowSetKeyProbe;
class RowSetMetadata;
struct ProbeStats;

class RowSet {
 public:
  enum DeltaCompactionType {
    MAJOR_DELTA_COMPACTION,
    MINOR_DELTA_COMPACTION
  };

  // Check if a given row key is present in this rowset.
  // Sets *present and returns Status::OK, unless an error
  // occurs.
  //
  // If the row was once present in this rowset, but no longer present
  // due to a DELETE, then this should set *present = false, as if
  // it were never there.
  virtual Status CheckRowPresent(const RowSetKeyProbe &probe, bool *present,
                                 ProbeStats* stats) const = 0;

  // Update/delete a row in this rowset.
  // The 'update_schema' is the client schema used to encode the 'update' RowChangeList.
  //
  // If the row does not exist in this rowset, returns
  // Status::NotFound().
  virtual Status MutateRow(Timestamp timestamp,
                           const RowSetKeyProbe &probe,
                           const RowChangeList &update,
                           const consensus::OpId& op_id,
                           ProbeStats* stats,
                           OperationResultPB* result) = 0;

  // Return a new RowIterator for this rowset, with the given projection.
  // The projection schema must remain valid for the lifetime of the iterator.
  // The iterator will return rows/updates which were committed as of the time of
  // 'snap'.
  // The returned iterator is not Initted.
  virtual Status NewRowIterator(const Schema *projection,
                                const MvccSnapshot &snap,
                                gscoped_ptr<RowwiseIterator>* out) const = 0;

  // Create the input to be used for a compaction.
  // The provided 'projection' is for the compaction output. Each row
  // will be projected into this Schema.
  virtual Status NewCompactionInput(const Schema* projection,
                                    const MvccSnapshot &snap,
                                    gscoped_ptr<CompactionInput>* out) const = 0;

  // Count the number of rows in this rowset.
  virtual Status CountRows(rowid_t *count) const = 0;

  // Return the bounds for this RowSet. 'min_encoded_key' and 'max_encoded_key'
  // are set to the first and last encoded keys for this RowSet. The storage
  // for these slices is part of the RowSet and only guaranteed to stay valid
  // until the RowSet is destroyed.
  //
  // In the case that the rowset is still mutable (eg MemRowSet), this may
  // return Status::NotImplemented.
  virtual Status GetBounds(Slice *min_encoded_key,
                           Slice *max_encoded_key) const = 0;

  // Return a displayable string for this rowset.
  virtual string ToString() const = 0;

  // Dump the full contents of this rowset, for debugging.
  // This is very verbose so only useful within unit tests.
  virtual Status DebugDump(vector<string> *lines = NULL) = 0;

  // Estimate the number of bytes on-disk
  virtual uint64_t EstimateOnDiskSize() const = 0;

  // Return the lock used for including this DiskRowSet in a compaction.
  // This prevents multiple compactions and flushes from trying to include
  // the same rowset.
  virtual boost::mutex *compact_flush_lock() = 0;

  // Returns the metadata associated with this rowset.
  virtual std::shared_ptr<RowSetMetadata> metadata() = 0;

  // Get the size of the delta's MemStore
  virtual size_t DeltaMemStoreSize() const = 0;

  virtual bool DeltaMemStoreEmpty() const = 0;

  // Get the minimum log index corresponding to unflushed data in this row set.
  virtual int64_t MinUnflushedLogIndex() const = 0;

  // Get the performance improvement that running a minor or major delta compaction would give.
  // The returned score ranges between 0 and 1 inclusively.
  virtual double DeltaStoresCompactionPerfImprovementScore(DeltaCompactionType type) const = 0;

  // Flush the DMS if there's one
  virtual Status FlushDeltas() = 0;

  // Compact delta stores if more than one.
  virtual Status MinorCompactDeltaStores() = 0;

  virtual ~RowSet() {}

  // Return true if this RowSet is available for compaction, based on
  // the current state of the compact_flush_lock. This should only be
  // used under the Tablet's compaction selection lock, or else the
  // lock status may change at any point.
  virtual bool IsAvailableForCompaction() {
    // Try to obtain the lock. If we don't succeed, it means the rowset
    // was already locked for compaction by some other compactor thread,
    // or it is a RowSet type which can't be used as a compaction input.
    //
    // We can be sure that our check here will remain true until after
    // the compaction selection has finished because only one thread
    // makes compaction selection at a time on a given Tablet due to
    // Tablet::compact_select_lock_.
    boost::mutex::scoped_try_lock try_lock(*compact_flush_lock());
    return try_lock.owns_lock();
  }

};

// Used often enough, may as well typedef it.
typedef vector<std::shared_ptr<RowSet> > RowSetVector;
// Structure which caches an encoded and hashed key, suitable
// for probing against rowsets.
class RowSetKeyProbe {
 public:
  // row_key: a reference to the key portion of a row in memory
  // to probe for.
  //
  // NOTE: row_key is not copied and must be valid for the lifetime
  // of this object.
  explicit RowSetKeyProbe(ConstContiguousRow row_key)
      : row_key_(std::move(row_key)) {
    encoded_key_ = EncodedKey::FromContiguousRow(row_key_);
    bloom_probe_ = BloomKeyProbe(encoded_key_slice());
  }

  // RowSetKeyProbes are usually allocated on the stack, which means that we
  // must copy it if we require it later (e.g. Table::Mutate()).
  //
  // Still, the ConstContiguousRow row_key_ remains a reference to the data
  // underlying the original RowsetKeyProbe and is not copied.
  explicit RowSetKeyProbe(const RowSetKeyProbe& probe)
  : row_key_(probe.row_key_) {
    encoded_key_ = EncodedKey::FromContiguousRow(row_key_);
    bloom_probe_ = BloomKeyProbe(encoded_key_slice());
  }

  const ConstContiguousRow& row_key() const { return row_key_; }

  // Pointer to the key which has been encoded to be contiguous
  // and lexicographically comparable
  const Slice &encoded_key_slice() const { return encoded_key_->encoded_key(); }

  // Return the cached structure used to query bloom filters.
  const BloomKeyProbe &bloom_probe() const { return bloom_probe_; }

  // The schema containing the key.
  const Schema* schema() const { return row_key_.schema(); }

  const EncodedKey &encoded_key() const {
    return *encoded_key_;
  }

 private:
  const ConstContiguousRow row_key_;
  gscoped_ptr<EncodedKey> encoded_key_;
  BloomKeyProbe bloom_probe_;
};

// Statistics collected during row operations, counting how many times
// various structures had to be consulted to perform the operation.
//
// These eventually propagate into tablet-scoped metrics, and when we
// have RPC tracing capability, we could also stringify them into the
// trace to understand why an RPC may have been slow.
struct ProbeStats {
  ProbeStats()
    : blooms_consulted(0),
      keys_consulted(0),
      deltas_consulted(0),
      mrs_consulted(0) {
  }

  // Incremented for each bloom filter consulted.
  int blooms_consulted;

  // Incremented for each key cfile consulted.
  int keys_consulted;

  // Incremented for each delta file consulted.
  int deltas_consulted;

  // Incremented for each MemRowSet consulted.
  int mrs_consulted;
};

// RowSet which is used during the middle of a flush or compaction.
// It consists of a set of one or more input rowsets, and a single
// output rowset. All mutations are duplicated to the appropriate input
// rowset as well as the output rowset. All reads are directed to the
// union of the input rowsets.
//
// See compaction.txt for a little more detail on how this is used.
class DuplicatingRowSet : public RowSet {
 public:
  DuplicatingRowSet(RowSetVector old_rowsets, RowSetVector new_rowsets);

  virtual Status MutateRow(Timestamp timestamp,
                           const RowSetKeyProbe &probe,
                           const RowChangeList &update,
                           const consensus::OpId& op_id,
                           ProbeStats* stats,
                           OperationResultPB* result) OVERRIDE;

  Status CheckRowPresent(const RowSetKeyProbe &probe, bool *present,
                         ProbeStats* stats) const OVERRIDE;

  virtual Status NewRowIterator(const Schema *projection,
                                const MvccSnapshot &snap,
                                gscoped_ptr<RowwiseIterator>* out) const OVERRIDE;

  virtual Status NewCompactionInput(const Schema* projection,
                                    const MvccSnapshot &snap,
                                    gscoped_ptr<CompactionInput>* out) const OVERRIDE;

  Status CountRows(rowid_t *count) const OVERRIDE;

  virtual Status GetBounds(Slice *min_encoded_key,
                           Slice *max_encoded_key) const OVERRIDE;

  uint64_t EstimateOnDiskSize() const OVERRIDE;

  string ToString() const OVERRIDE;

  virtual Status DebugDump(vector<string> *lines = NULL) OVERRIDE;

  std::shared_ptr<RowSetMetadata> metadata() OVERRIDE;

  // A flush-in-progress rowset should never be selected for compaction.
  boost::mutex *compact_flush_lock() OVERRIDE {
    LOG(FATAL) << "Cannot be compacted";
    return NULL;
  }

  virtual bool IsAvailableForCompaction() OVERRIDE {
    return false;
  }

  ~DuplicatingRowSet();

  size_t DeltaMemStoreSize() const OVERRIDE { return 0; }

  bool DeltaMemStoreEmpty() const OVERRIDE { return true; }

  double DeltaStoresCompactionPerfImprovementScore(DeltaCompactionType type) const OVERRIDE {
    return 0;
  }

  int64_t MinUnflushedLogIndex() const OVERRIDE { return -1; }

  Status FlushDeltas() OVERRIDE {
    // It's important that DuplicatingRowSet does not FlushDeltas. This prevents
    // a bug where we might end up with out-of-order deltas. See the long
    // comment in Tablet::Flush(...)
    return Status::OK();
  }

  Status MinorCompactDeltaStores() OVERRIDE { return Status::OK(); }

 private:
  friend class Tablet;

  DISALLOW_COPY_AND_ASSIGN(DuplicatingRowSet);

  RowSetVector old_rowsets_;
  RowSetVector new_rowsets_;
};


} // namespace tablet
} // namespace kudu

#endif

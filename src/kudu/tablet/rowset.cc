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

#include "kudu/tablet/rowset.h"

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "kudu/common/generic_iterators.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/tablet/rowset_metadata.h"

using std::shared_ptr;
using strings::Substitute;

namespace kudu { namespace tablet {

DuplicatingRowSet::DuplicatingRowSet(RowSetVector old_rowsets,
                                     RowSetVector new_rowsets)
    : old_rowsets_(std::move(old_rowsets)),
      new_rowsets_(std::move(new_rowsets)) {
  CHECK_GT(old_rowsets_.size(), 0);
  CHECK_GT(new_rowsets_.size(), 0);
}

DuplicatingRowSet::~DuplicatingRowSet() {
}

// Stringify the given list of rowsets into 'dst'.
static void AppendRowSetStrings(const RowSetVector &rowsets, string *dst) {
  bool first = true;
  dst->append("[");
  for (const shared_ptr<RowSet> &rs : rowsets) {
    if (!first) {
      dst->append(", ");
    }
    first = false;
    dst->append(rs->ToString());
  }
  dst->append("]");
}

string DuplicatingRowSet::ToString() const {
  string ret;
  ret.append("DuplicatingRowSet(");

  AppendRowSetStrings(old_rowsets_, &ret);
  ret.append(" -> ");
  AppendRowSetStrings(new_rowsets_, &ret);
  ret.append(")");
  return ret;
}

Status DuplicatingRowSet::NewRowIterator(const Schema *projection,
                                         const MvccSnapshot &snap,
                                         gscoped_ptr<RowwiseIterator>* out) const {
  // Use the original rowset.
  if (old_rowsets_.size() == 1) {
    return old_rowsets_[0]->NewRowIterator(projection, snap, out);
  } else {
    // Union between them

    vector<shared_ptr<RowwiseIterator> > iters;
    for (const shared_ptr<RowSet> &rowset : old_rowsets_) {
      gscoped_ptr<RowwiseIterator> iter;
      RETURN_NOT_OK_PREPEND(rowset->NewRowIterator(projection, snap, &iter),
                            Substitute("Could not create iterator for rowset $0",
                                       rowset->ToString()));
      iters.push_back(shared_ptr<RowwiseIterator>(iter.release()));
    }

    out->reset(new UnionIterator(iters));
    return Status::OK();
  }
}

Status DuplicatingRowSet::NewCompactionInput(const Schema* projection,
                                             const MvccSnapshot &snap,
                                             gscoped_ptr<CompactionInput>* out) const  {
  LOG(FATAL) << "duplicating rowsets do not act as compaction input";
  return Status::OK();
}


Status DuplicatingRowSet::MutateRow(Timestamp timestamp,
                                    const RowSetKeyProbe &probe,
                                    const RowChangeList &update,
                                    const consensus::OpId& op_id,
                                    ProbeStats* stats,
                                    OperationResultPB* result) {
  // Duplicate the update to both the relevant input rowset and the output rowset.
  //
  // It's crucial to do the mutation against the input side first, due to the potential
  // for a race during flush: the output rowset may not yet hold a DELETE which
  // is present in the input rowset. In that case, the UPDATE against the output rowset would
  // succeed whereas it can't be applied to the input rowset. So, we update the input rowset first,
  // and if it succeeds, propagate to the output.

  // First mutate the relevant input rowset.
  bool updated = false;
  for (const shared_ptr<RowSet> &rowset : old_rowsets_) {
    Status s = rowset->MutateRow(timestamp, probe, update, op_id, stats, result);
    if (s.ok()) {
      updated = true;
      break;
    } else if (!s.IsNotFound()) {
      LOG(ERROR) << "Unable to update key "
                 << probe.schema()->CreateKeyProjection().DebugRow(probe.row_key())
                 << " (failed on rowset " << rowset->ToString() << "): "
                 << s.ToString();
      return s;
    }
  }

  if (!updated) {
    return Status::NotFound("not found in any compaction input");
  }

  // If it succeeded there, we also need to mirror into the new rowset.
  int mirrored_count = 0;
  for (const shared_ptr<RowSet> &new_rowset : new_rowsets_) {
    Status s = new_rowset->MutateRow(timestamp, probe, update, op_id, stats, result);
    if (s.ok()) {
      mirrored_count++;
      #ifdef NDEBUG
      // In non-DEBUG builds, we can break as soon as we find the correct
      // rowset to mirror to. In a DEBUG build, though, we keep looking
      // through all, and make sure that we only update in one of them.
      break;
      #endif
    } else if (!s.IsNotFound()) {
      LOG(FATAL) << "Unable to mirror update to rowset " << new_rowset->ToString()
                 << " for key: " << probe.schema()->CreateKeyProjection().DebugRow(probe.row_key())
                 << ": " << s.ToString();
    }
    // IsNotFound is OK - it might be in a different one.
  }
  CHECK_EQ(mirrored_count, 1)
    << "Updated row in compaction input, but didn't mirror in exactly 1 new rowset: "
    << probe.schema()->CreateKeyProjection().DebugRow(probe.row_key());
  return Status::OK();
}

Status DuplicatingRowSet::CheckRowPresent(const RowSetKeyProbe &probe,
                                          bool *present, ProbeStats* stats) const {
  *present = false;
  for (const shared_ptr<RowSet> &rowset : old_rowsets_) {
    RETURN_NOT_OK(rowset->CheckRowPresent(probe, present, stats));
    if (*present) {
      return Status::OK();
    }
  }
  return Status::OK();
}

Status DuplicatingRowSet::CountRows(rowid_t *count) const {
  int64_t accumulated_count = 0;
  for (const shared_ptr<RowSet> &rs : new_rowsets_) {
    rowid_t this_count;
    RETURN_NOT_OK(rs->CountRows(&this_count));
    accumulated_count += this_count;
  }

  CHECK_LT(accumulated_count, std::numeric_limits<rowid_t>::max())
    << "TODO: should make sure this is 64-bit safe - probably not right now"
    << " because rowid_t is only 32-bit.";
  *count = accumulated_count;
  return Status::OK();
}

Status DuplicatingRowSet::GetBounds(Slice *min_encoded_key,
                                    Slice *max_encoded_key) const {
  // The range out of the output rowset always spans the full range
  // of the input rowsets, since no new rows can be inserted.
  // The output rowsets are in ascending order, so their total range
  // spans the range [front().min, back().max].
  Slice junk;
  RETURN_NOT_OK(new_rowsets_.front()->GetBounds(min_encoded_key, &junk));
  RETURN_NOT_OK(new_rowsets_.back()->GetBounds(&junk, max_encoded_key));
  return Status::OK();
}

uint64_t DuplicatingRowSet::EstimateOnDiskSize() const {
  // The actual value of this doesn't matter, since it won't be selected
  // for compaction.
  uint64_t size = 0;
  for (const shared_ptr<RowSet> &rs : new_rowsets_) {
    size += rs->EstimateOnDiskSize();
  }
  return size;
}

shared_ptr<RowSetMetadata> DuplicatingRowSet::metadata() {
  return shared_ptr<RowSetMetadata>(reinterpret_cast<RowSetMetadata *>(NULL));
}

Status DuplicatingRowSet::DebugDump(vector<string> *lines) {
  int i = 1;
  for (const shared_ptr<RowSet> &rs : old_rowsets_) {
    LOG_STRING(INFO, lines) << "Duplicating rowset input " << ToString() << " "
                            << i << "/" << old_rowsets_.size() << ":";
    RETURN_NOT_OK(rs->DebugDump(lines));
    i++;
  }
  i = 1;
  for (const shared_ptr<RowSet> &rs : new_rowsets_) {
    LOG_STRING(INFO, lines) << "Duplicating rowset output " << ToString() << " "
                            << i << "/" << new_rowsets_.size() << ":";
    RETURN_NOT_OK(rs->DebugDump(lines));
    i++;
  }

  return Status::OK();
}

} // namespace tablet
} // namespace kudu

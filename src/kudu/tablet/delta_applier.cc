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

#include "kudu/tablet/delta_applier.h"

#include <string>
#include <vector>

#include "kudu/common/iterator.h"
#include "kudu/tablet/delta_store.h"
#include "kudu/util/status.h"

using std::shared_ptr;
using std::string;

namespace kudu {
namespace tablet {

  // Construct. The base_iter and delta_iter should not be Initted.
DeltaApplier::DeltaApplier(shared_ptr<CFileSet::Iterator> base_iter,
                           shared_ptr<DeltaIterator> delta_iter)
    : base_iter_(std::move(base_iter)),
      delta_iter_(std::move(delta_iter)),
      first_prepare_(true) {}

DeltaApplier::~DeltaApplier() {
}

Status DeltaApplier::Init(ScanSpec *spec) {
  RETURN_NOT_OK(base_iter_->Init(spec));
  RETURN_NOT_OK(delta_iter_->Init(spec));
  return Status::OK();
}


string DeltaApplier::ToString() const {
  string s;
  s.append("DeltaApplier(");
  s.append(base_iter_->ToString());
  s.append(" + ");
  s.append(delta_iter_->ToString());
  s.append(")");
  return s;
}

const Schema &DeltaApplier::schema() const {
  return base_iter_->schema();
}

void DeltaApplier::GetIteratorStats(std::vector<IteratorStats>* stats) const {
  return base_iter_->GetIteratorStats(stats);
}

bool DeltaApplier::HasNext() const {
  return base_iter_->HasNext();
}

Status DeltaApplier::PrepareBatch(size_t *nrows) {
  // The initial seek is deferred from Init() into the first PrepareBatch()
  // because it requires a loaded delta file, and we don't want to require
  // that at Init() time.
  if (first_prepare_) {
    RETURN_NOT_OK(delta_iter_->SeekToOrdinal(base_iter_->cur_ordinal_idx()));
    first_prepare_ = false;
  }
  RETURN_NOT_OK(base_iter_->PrepareBatch(nrows));
  RETURN_NOT_OK(delta_iter_->PrepareBatch(*nrows, DeltaIterator::PREPARE_FOR_APPLY));
  return Status::OK();
}

Status DeltaApplier::FinishBatch() {
  return base_iter_->FinishBatch();
}

Status DeltaApplier::InitializeSelectionVector(SelectionVector *sel_vec) {
  DCHECK(!first_prepare_) << "PrepareBatch() must be called at least once";
  RETURN_NOT_OK(base_iter_->InitializeSelectionVector(sel_vec));
  return delta_iter_->ApplyDeletes(sel_vec);
}

Status DeltaApplier::MaterializeColumn(size_t col_idx, ColumnBlock *dst) {
  DCHECK(!first_prepare_) << "PrepareBatch() must be called at least once";

  // Copy the base data.
  RETURN_NOT_OK(base_iter_->MaterializeColumn(col_idx, dst));

  // Apply all the updates for this column.
  RETURN_NOT_OK(delta_iter_->ApplyUpdates(col_idx, dst));
  return Status::OK();
}

} // namespace tablet
} // namespace kudu

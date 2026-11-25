// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/sample_block_resolver.h"

#include <algorithm>
#include <type_traits>

#include "yb/docdb/ql_storage_interface.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/table/internal_iterator.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb::docdb {

// GetSampleBlocks() will put resolved blocks into its reservoir as they are.
static_assert(std::is_same_v<SampleBlock, YQLStorageIf::SampleBlocksReservoir::value_type>);

namespace {

Slice CutKey(Slice internal_key) {
  return TryExtractDocKey(rocksdb::ExtractUserKey(internal_key));
}

void KeepLargest(Slice cut_key, std::optional<KeyBuffer>* largest) {
  if (!*largest || cut_key.compare((*largest)->AsSlice()) > 0) {
    *largest = KeyBuffer(cut_key);
  }
}

// Init() numbers a file's items from the data block count in its table properties, so the count
// must match the index: a wrong one would misplace the skipped last entry without an error.
// NotSupported if the file can't be read by position.
Status CheckNumDataBlocks(
    rocksdb::DataBlockAwareIndexInternalIterator* iter, uint64_t num_data_blocks) {
  if (num_data_blocks == 0) {
    iter->SeekToFirst();
  } else {
    const auto& last = VERIFY_RESULT_REF(iter->SeekToPosition(num_data_blocks - 1));
    RETURN_NOT_OK(iter->status());
    SCHECK(last.Valid(), Corruption,
           Format("SST index has fewer entries than its $0 data blocks", num_data_blocks));
    iter->Next();
  }
  RETURN_NOT_OK(iter->status());
  SCHECK(!iter->Valid(), Corruption,
         Format("SST index has more entries than its $0 data blocks", num_data_blocks));
  return Status::OK();
}

} // namespace

Slice TryExtractDocKey(Slice encoded_key) {
  const auto doc_key_size =
      dockv::DocKey::TryGetEncodedSize(encoded_key, dockv::DocKeyPart::kWholeDocKey);
  if (doc_key_size) {
    return Slice(encoded_key.data(), *doc_key_size);
  }
  return encoded_key;
}

Slice TableRowsStart(Slice lower_bound_key) {
  return lower_bound_key.empty() ? Slice(&dockv::kMinRegularDbTableRowFirstByte, 1)
                                 : lower_bound_key;
}

SampleBlockResolver::SampleBlockResolver(
    rocksdb::SstFileIndexIterators* sst_files, const KeyBounds& bounds)
    : sst_files_(*sst_files), comparator_(*sst_files->comparator), bounds_(bounds) {}

Status SampleBlockResolver::Init() {
  DCHECK(files_.empty() && !tail_lower_) << "Init() called twice";
  const auto table_rows_start = TableRowsStart(bounds_.lower.AsSlice());
  const auto upper_bound_key = bounds_.upper.AsSlice();
  const auto lower_key =
      rocksdb::InternalKey::MaxPossibleForUserKey(table_rows_start).Encode().ToBuffer();
  std::string upper_key;
  if (!upper_bound_key.empty()) {
    upper_key = rocksdb::InternalKey::MaxPossibleForUserKey(upper_bound_key).Encode().ToBuffer();
  }
  std::vector<File> files;
  // GetSampleBlocks() skips only the entry its Seek() lands on. Of several entries equal to
  // table_rows_start, which one that is is arbitrary, and skipping any of them leaves the same
  // keys, so the resolver skips the first by file.
  std::optional<size_t> start_key_file_idx;
  for (auto& sst_file : sst_files_.files) {
    auto* iter = sst_file.index_iter.get();
    RETURN_NOT_OK(CheckNumDataBlocks(iter, sst_file.num_data_blocks));
    // A file's last entry is never an item (SkipLastEntry in GetSampleBlocks()).
    if (sst_file.num_data_blocks <= 1) {
      continue;
    }
    const auto last_item_end = sst_file.num_data_blocks - 1;
    File file;
    file.iter = iter;

    // NotSupported if the file can't be read by position.
    file.begin = VERIFY_RESULT(iter->PositionOf(lower_key));
    SCHECK_LE(
        file.begin, sst_file.num_data_blocks, Corruption,
        "Index position past the number of data blocks of an SST file");
    file.first_key.Assign(iter->Valid() ? iter->key() : Slice());
    RETURN_NOT_OK(iter->status());
    file.end = last_item_end;
    if (!upper_key.empty()) {
      file.end = std::min(file.end, VERIFY_RESULT(iter->PositionOf(upper_key)));
      // GetSampleBlocks() stops on the DocKey, which can be below upper_bound_key for an index key
      // above it.
      while (file.end < last_item_end && iter->Valid() &&
             CutKey(iter->key()).compare(upper_bound_key) < 0) {
        ++file.end;
        iter->Next();
      }
      RETURN_NOT_OK(iter->status());
    }
    if (file.begin < file.end) {
      // The iterator is on the entry at end, unless end is the file's last entry.
      if (file.end == last_item_end) {
        iter->SeekToLast();
      }
      const auto& entry = iter->Prev();
      if (!entry.Valid()) {
        return EntryExpected(*iter);
      }
      file.last_key.Assign(entry.key);
    }

    // first_key is the entry at begin, if any.
    if (file.begin < last_item_end && !file.first_key.empty() &&
        rocksdb::ExtractUserKey(file.first_key.AsSlice()) == table_rows_start &&
        (!start_key_file_idx ||
         comparator_.Compare(
             file.first_key.AsSlice(), files[*start_key_file_idx].first_key.AsSlice()) < 0)) {
      start_key_file_idx = files.size();
    }
    files.push_back(std::move(file));
  }
  if (start_key_file_idx) {
    ++files[*start_key_file_idx].begin;
  }

  for (size_t file_idx = 0; file_idx != files.size(); ++file_idx) {
    auto& file = files[file_idx];
    if (file.begin >= file.end) {
      continue;
    }
    // The skip moved begin to the entry after first_key.
    if (file_idx == start_key_file_idx) {
      file.iter->Seek(file.first_key.AsSlice());
      if (!file.iter->Valid()) {
        return EntryExpected(*file.iter);
      }
      const auto& entry = file.iter->Next();
      if (!entry.Valid()) {
        return EntryExpected(*file.iter);
      }
      file.first_key.Assign(entry.key);
    }
    KeepLargest(CutKey(file.last_key.AsSlice()), &tail_lower_);
    num_boundary_items_ += file.end - file.begin;
    item_ends_.push_back(num_boundary_items_);
    files_.push_back(std::move(file));
  }
  return Status::OK();
}

Result<std::optional<SampleBlock>> SampleBlockResolver::Resolve(uint64_t position) {
  if (position == num_boundary_items_) {
    return MakeBlock(tail_lower_, bounds_.upper.AsSlice());
  }
  const auto [file_idx, file_position] = VERIFY_RESULT(Locate(position));
  return ResolveBoundary(file_idx, file_position);
}

Status SampleBlockResolver::EntryExpected(
    const rocksdb::DataBlockAwareIndexInternalIterator& iter) {
  RETURN_NOT_OK(iter.status());
  return STATUS(Corruption, "Missing SST index entry at a position inside the file");
}

bool SampleBlockResolver::AddPredecessor(
    Slice internal_key, Slice upper, std::optional<KeyBuffer>* lower) {
  const auto cut_key = CutKey(internal_key);
  if (cut_key == upper) {
    return false;
  }
  DCHECK_LT(cut_key.compare(upper), 0) << "Cut keys out of order: " << cut_key.ToDebugHexString()
                                       << " >= " << upper.ToDebugHexString();
  KeepLargest(cut_key, lower);
  return true;
}

Result<std::pair<size_t, uint64_t>> SampleBlockResolver::Locate(uint64_t position) const {
  SCHECK_LT(position, num_boundary_items_, InvalidArgument, "Item position out of range");
  const auto file_idx = static_cast<size_t>(
      std::upper_bound(item_ends_.begin(), item_ends_.end(), position) - item_ends_.begin());
  return std::pair(
      file_idx,
      files_[file_idx].begin + position - (file_idx == 0 ? 0 : item_ends_[file_idx - 1]));
}

Result<bool> SampleBlockResolver::AddPredecessorInFile(
    size_t file_idx, Slice target, EqualKeyComesFirst equal_key_comes_first, Slice upper,
    std::optional<KeyBuffer>* lower) {
  const auto& file = files_[file_idx];
  const auto comes_before = [this, target, equal_key_comes_first](Slice key) {
    const auto cmp = comparator_.Compare(key, target);
    return cmp < 0 || (cmp == 0 && equal_key_comes_first);
  };
  // Files whose items all come before or after `target` need no index descent.
  if (!comes_before(file.first_key.AsSlice())) {
    return true;
  }
  if (comes_before(file.last_key.AsSlice())) {
    return AddPredecessor(file.last_key.AsSlice(), upper, lower);
  }
  auto* iter = file.iter;
  iter->Seek(target);
  // Keys are unique within a file, so at most one entry equals `target`.
  if (equal_key_comes_first && iter->Valid() && iter->key() == target) {
    iter->Next();
  }
  RETURN_NOT_OK(iter->status());
  // `target` is between the file's first and last items, so the iterator is on an item right after
  // the predecessor.
  if (!iter->Valid()) {
    return EntryExpected(*iter);
  }
  const auto& entry = iter->Prev();
  if (!entry.Valid()) {
    return EntryExpected(*iter);
  }
  return AddPredecessor(entry.key, upper, lower);
}

std::optional<SampleBlock> SampleBlockResolver::MakeBlock(
    const std::optional<KeyBuffer>& lower, Slice upper) const {
  SampleBlock block;
  block.first.Assign(lower ? lower->AsSlice() : bounds_.lower.AsSlice());
  block.second.Assign(upper);
  // As in GetSampleBlocks(), both bounds empty means the whole tablet. Unlike it, an inverted
  // block, possible only first, is dropped too.
  if (!block.second.empty() && block.first.AsSlice().compare(block.second.AsSlice()) >= 0) {
    return std::nullopt;
  }
  return block;
}

Result<std::optional<SampleBlock>> SampleBlockResolver::ResolveBoundary(
    size_t file_idx, uint64_t position) {
  auto& file = files_[file_idx];
  auto* iter = file.iter;
  // PositionOf() only checks the restart interval of the block it lands in, so a file Init()
  // accepted can still return NotSupported here.
  const auto& entry = VERIFY_RESULT_REF(iter->SeekToPosition(position));
  if (!entry.Valid()) {
    return EntryExpected(*iter);
  }
  const KeyBuffer target(entry.key);
  const auto upper = CutKey(target.AsSlice());
  std::optional<KeyBuffer> lower;
  if (position > file.begin) {
    const auto& prev = iter->Prev();
    if (!prev.Valid()) {
      return EntryExpected(*iter);
    }
    if (!AddPredecessor(prev.key, upper, &lower)) {
      return std::nullopt;
    }
  }
  for (size_t other_idx = 0; other_idx != files_.size(); ++other_idx) {
    if (other_idx == file_idx) {
      continue;
    }
    if (!VERIFY_RESULT(AddPredecessorInFile(
            other_idx, target.AsSlice(), EqualKeyComesFirst(other_idx < file_idx),
            upper, &lower))) {
      return std::nullopt;
    }
  }
  return MakeBlock(lower, upper);
}

} // namespace yb::docdb

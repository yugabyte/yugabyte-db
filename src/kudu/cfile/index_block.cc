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


#include "kudu/cfile/cfile_writer.h"
#include "kudu/cfile/index_block.h"
#include "kudu/util/protobuf_util.h"

namespace kudu {
namespace cfile {

inline void SliceEncode(const Slice &key, faststring *buf) {
  InlinePutVarint32(buf, key.size());
  buf->append(key.data(), key.size());
}

inline const uint8_t *SliceDecode(const uint8_t *encoded_ptr, const uint8_t *limit,
                         Slice *retptr) {
  uint32_t len;
  const uint8_t *data_start = GetVarint32Ptr(encoded_ptr, limit, &len);
  if (data_start == nullptr) {
    // bad varint
    return nullptr;
  }

  if (data_start + len > limit) {
    // length extends past end of valid area
    return nullptr;
  }

  *retptr = Slice(data_start, len);
  return data_start + len;
}

IndexBlockBuilder::IndexBlockBuilder(
  const WriterOptions *options,
  bool is_leaf)
  : options_(options),
    finished_(false),
    is_leaf_(is_leaf) {
}


void IndexBlockBuilder::Add(const Slice &keyptr,
                            const BlockPointer &ptr) {
  DCHECK(!finished_) <<
    "Must Reset() after Finish() before more Add()";

  size_t entry_offset = buffer_.size();
  SliceEncode(keyptr, &buffer_);
  ptr.EncodeTo(&buffer_);
  entry_offsets_.push_back(entry_offset);
}

Slice IndexBlockBuilder::Finish() {
  CHECK(!finished_) << "already called Finish()";

  for (uint32_t off : entry_offsets_) {
    InlinePutFixed32(&buffer_, off);
  }

  IndexBlockTrailerPB trailer;
  trailer.set_num_entries(entry_offsets_.size());
  trailer.set_type(
    is_leaf_ ? IndexBlockTrailerPB::LEAF : IndexBlockTrailerPB::INTERNAL);
  AppendPBToString(trailer, &buffer_);

  InlinePutFixed32(&buffer_, trailer.GetCachedSize());

  finished_ = true;
  return Slice(buffer_);
}


// Return the key of the first entry in this index block.
Status IndexBlockBuilder::GetFirstKey(Slice *key) const {
  // TODO: going to need to be able to pass an arena or something
  // for slices, which need to copy

  if (entry_offsets_.empty()) {
    return Status::NotFound("no keys in builder");
  }

  bool success = nullptr != SliceDecode(buffer_.data(),buffer_.data() + buffer_.size(),key);

  if (success) {
    return Status::OK();
  } else {
    return Status::Corruption("Unable to decode first key");
  }
}

size_t IndexBlockBuilder::EstimateEncodedSize() const {
  // the actual encoded index entries
  int size = buffer_.size();

  // entry offsets
  size += sizeof(uint32_t) * entry_offsets_.size();

  // estimate trailer cheaply -- not worth actually constructing
  // a trailer to determine the size.
  size += 16;

  return size;
}

// Construct a reader.
// After construtoin, call
IndexBlockReader::IndexBlockReader()
  : parsed_(false) {
}

void IndexBlockReader::Reset() {
  data_ = Slice();
  parsed_ = false;
}

Status IndexBlockReader::Parse(const Slice &data) {
  parsed_ = false;
  data_ = data;


  if (data_.size() < sizeof(uint32_t)) {
    return Status::Corruption("index block too small");
  }

  const uint8_t *trailer_size_ptr =
    data_.data() + data_.size() - sizeof(uint32_t);
  uint32_t trailer_size = DecodeFixed32(trailer_size_ptr);

  size_t max_size = trailer_size_ptr - data_.data();
  if (trailer_size <= 0 ||
      trailer_size > max_size) {
    string err = "invalid index block trailer size: " +
      boost::lexical_cast<string>(trailer_size);
    return Status::Corruption(err);
  }

  const uint8_t *trailer_ptr = trailer_size_ptr - trailer_size;

  bool success = trailer_.ParseFromArray(trailer_ptr, trailer_size);
  if (!success) {
    return Status::Corruption(
      "unable to parse trailer",
      trailer_.InitializationErrorString());
  }

  key_offsets_ = trailer_ptr - sizeof(uint32_t) * trailer_.num_entries();
  CHECK(trailer_ptr >= data_.data());

  VLOG(2) << "Parsed index trailer: " << trailer_.DebugString();

  parsed_ = true;
  return Status::OK();
}

size_t IndexBlockReader::Count() const {
  CHECK(parsed_) << "not parsed";
  return trailer_.num_entries();
}

IndexBlockIterator *IndexBlockReader::NewIterator() const {
  CHECK(parsed_) << "not parsed";
  return new IndexBlockIterator(this);
}

bool IndexBlockReader::IsLeaf() {
  return trailer_.type() == IndexBlockTrailerPB::LEAF;
}

int IndexBlockReader::CompareKey(int idx_in_block,
                                 const Slice &search_key) const {
  const uint8_t *key_ptr, *limit;
  GetKeyPointer(idx_in_block, &key_ptr, &limit);
  Slice this_slice;
  if (PREDICT_FALSE(SliceDecode(key_ptr, limit, &this_slice) == nullptr)) {
    LOG(WARNING)<< "Invalid data in block!";
    return 0;
  }

  return this_slice.compare(search_key);
}

Status IndexBlockReader::ReadEntry(size_t idx, Slice *key, BlockPointer *block_ptr) const {
  if (idx >= trailer_.num_entries()) {
    return Status::NotFound("Invalid index");
  }

  // At 'ptr', data is encoded as follows:
  // <key> <block offset> <block length>

  const uint8_t *ptr, *limit;
  GetKeyPointer(idx, &ptr, &limit);

  ptr = SliceDecode(ptr, limit, key);
  if (ptr == nullptr) {
    return Status::Corruption("Invalid key in index");
  }

  return block_ptr->DecodeFrom(ptr, data_.data() + data_.size());
}

void IndexBlockReader::GetKeyPointer(int idx_in_block, const uint8_t **ptr,
                                     const uint8_t **limit) const {
  size_t offset_in_block = DecodeFixed32(
    &key_offsets_[idx_in_block * sizeof(uint32_t)]);
  *ptr = data_.data() + offset_in_block;

  int next_idx = idx_in_block + 1;

  if (PREDICT_FALSE(next_idx >= trailer_.num_entries())) {
    DCHECK(next_idx == Count()) << "Bad index: " << idx_in_block
                                << " Count: " << Count();
    // last key in block: limit is the beginning of the offsets array
    *limit = key_offsets_;
  } else {
    // otherwise limit is the beginning of the next key
    offset_in_block = DecodeFixed32(
      &key_offsets_[next_idx * sizeof(uint32_t)]);
    *limit = data_.data() + offset_in_block;
  }
}

size_t IndexBlockBuilder::Count() const {
  return entry_offsets_.size();
}

void IndexBlockBuilder::Reset() {
  buffer_.clear();
  entry_offsets_.clear();
  finished_ = false;
}

IndexBlockIterator::IndexBlockIterator(const IndexBlockReader *reader)
  : reader_(reader),
    cur_idx_(-1),
    seeked_(false) {
}

void IndexBlockIterator::Reset() {
  seeked_ = false;
  cur_idx_ = -1;
}

Status IndexBlockIterator::SeekAtOrBefore(const Slice &search_key) {
  size_t left = 0;
  size_t right = reader_->Count() - 1;
  while (left < right) {
    int mid = (left + right + 1) / 2;

    int compare = reader_->CompareKey(mid, search_key);
    if (compare < 0) {  // mid < search
      left = mid;
    } else if (compare > 0) {  // mid > search
      right = mid - 1;
    } else {  // mid == search
      left = mid;
      break;
    }
  }

  // closest is now 'left'
  int compare = reader_->CompareKey(left, search_key);
  if (compare > 0) {
    // The last midpoint was still greather then the
    // provided key, which implies that the key is
    // lower than the lowest in the block.
    return Status::NotFound("key not present");
  }

  return SeekToIndex(left);
}

Status IndexBlockIterator::SeekToIndex(size_t idx) {
  cur_idx_ = idx;
  Status s = reader_->ReadEntry(idx, &cur_key_, &cur_ptr_);
  seeked_ = s.ok();
  return s;
}

bool IndexBlockIterator::HasNext() const {
  return cur_idx_ + 1 < reader_->Count();
}

Status IndexBlockIterator::Next() {
  return SeekToIndex(cur_idx_ + 1);
}

const BlockPointer &IndexBlockIterator::GetCurrentBlockPointer() const {
  CHECK(seeked_) << "not seeked";
  return cur_ptr_;
}

const Slice IndexBlockIterator::GetCurrentKey() const {
  CHECK(seeked_) << "not seeked";
  return cur_key_;
}

} // namespace cfile
} // namespace kudu

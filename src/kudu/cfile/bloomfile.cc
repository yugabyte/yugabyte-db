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

#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <mutex>
#include <sched.h>
#include <string>
#include <unistd.h>

#include "kudu/cfile/bloomfile.h"
#include "kudu/cfile/cfile_writer.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/sysinfo.h"
#include "kudu/util/coding.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/malloc.h"
#include "kudu/util/pb_util.h"

DECLARE_bool(cfile_lazy_open);

namespace kudu {
namespace cfile {

using fs::ReadableBlock;
using fs::ScopedWritableBlockCloser;
using fs::WritableBlock;

////////////////////////////////////////////////////////////
// Writer
////////////////////////////////////////////////////////////

BloomFileWriter::BloomFileWriter(gscoped_ptr<WritableBlock> block,
                                 const BloomFilterSizing &sizing)
  : bloom_builder_(sizing) {
  cfile::WriterOptions opts;
  opts.write_posidx = false;
  opts.write_validx = true;
  // Never use compression, regardless of the default settings, since
  // bloom filters are high-entropy data structures by their nature.
  opts.storage_attributes.encoding  = PLAIN_ENCODING;
  opts.storage_attributes.compression = NO_COMPRESSION;
  writer_.reset(new cfile::CFileWriter(opts, GetTypeInfo(BINARY), false, block.Pass()));
}

Status BloomFileWriter::Start() {
  return writer_->Start();
}

Status BloomFileWriter::Finish() {
  ScopedWritableBlockCloser closer;
  RETURN_NOT_OK(FinishAndReleaseBlock(&closer));
  return closer.CloseBlocks();
}

Status BloomFileWriter::FinishAndReleaseBlock(ScopedWritableBlockCloser* closer) {
  if (bloom_builder_.count() > 0) {
    RETURN_NOT_OK(FinishCurrentBloomBlock());
  }
  return writer_->FinishAndReleaseBlock(closer);
}

size_t BloomFileWriter::written_size() const {
  return writer_->written_size();
}

Status BloomFileWriter::AppendKeys(
  const Slice *keys, size_t n_keys) {

  // If this is the call on a new bloom, copy the first key.
  if (bloom_builder_.count() == 0 && n_keys > 0) {
    first_key_.assign_copy(keys[0].data(), keys[0].size());
  }

  for (size_t i = 0; i < n_keys; i++) {

    bloom_builder_.AddKey(BloomKeyProbe(keys[i]));

    // Bloom has reached optimal occupancy: flush it to the file
    if (PREDICT_FALSE(bloom_builder_.count() >= bloom_builder_.expected_count())) {
      RETURN_NOT_OK(FinishCurrentBloomBlock());

      // Copy the next key as the first key of the next block.
      // Doing this here avoids having to do it in normal code path of the loop.
      if (i < n_keys - 1) {
        first_key_.assign_copy(keys[i + 1].data(), keys[i + 1].size());
      }
    }
  }

  return Status::OK();
}

Status BloomFileWriter::FinishCurrentBloomBlock() {
  VLOG(1) << "Appending a new bloom block, first_key=" << Slice(first_key_).ToDebugString();

  // Encode the header.
  BloomBlockHeaderPB hdr;
  hdr.set_num_hash_functions(bloom_builder_.n_hashes());
  faststring hdr_str;
  PutFixed32(&hdr_str, hdr.ByteSize());
  CHECK(pb_util::AppendToString(hdr, &hdr_str));

  // The data is the concatenation of the header and the bloom itself.
  vector<Slice> slices;
  slices.push_back(Slice(hdr_str));
  slices.push_back(bloom_builder_.slice());

  // Append to the file.
  Slice start_key(first_key_);
  RETURN_NOT_OK(writer_->AppendRawBlock(slices, 0, &start_key, "bloom block"));

  bloom_builder_.Clear();

  #ifndef NDEBUG
  first_key_.assign_copy("POST_RESET");
  #endif

  return Status::OK();
}

////////////////////////////////////////////////////////////
// Reader
////////////////////////////////////////////////////////////

Status BloomFileReader::Open(gscoped_ptr<ReadableBlock> block,
                             const ReaderOptions& options,
                             gscoped_ptr<BloomFileReader> *reader) {
  gscoped_ptr<BloomFileReader> bf_reader;
  RETURN_NOT_OK(OpenNoInit(block.Pass(), options, &bf_reader));
  RETURN_NOT_OK(bf_reader->Init());

  *reader = bf_reader.Pass();
  return Status::OK();
}

Status BloomFileReader::OpenNoInit(gscoped_ptr<ReadableBlock> block,
                                   const ReaderOptions& options,
                                   gscoped_ptr<BloomFileReader> *reader) {
  gscoped_ptr<CFileReader> cf_reader;
  RETURN_NOT_OK(CFileReader::OpenNoInit(block.Pass(), options, &cf_reader));
  gscoped_ptr<BloomFileReader> bf_reader(new BloomFileReader(
      cf_reader.Pass(), options));
  if (!FLAGS_cfile_lazy_open) {
    RETURN_NOT_OK(bf_reader->Init());
  }

  *reader = bf_reader.Pass();
  return Status::OK();
}

BloomFileReader::BloomFileReader(gscoped_ptr<CFileReader> reader,
                                 const ReaderOptions& options)
  : reader_(reader.Pass()),
    mem_consumption_(options.parent_mem_tracker,
                     memory_footprint_excluding_reader()) {
}

Status BloomFileReader::Init() {
  return init_once_.Init(&BloomFileReader::InitOnce, this);
}

Status BloomFileReader::InitOnce() {
  // Fully open the CFileReader if it was lazily opened earlier.
  //
  // If it's already initialized, this is a no-op.
  RETURN_NOT_OK(reader_->Init());

  if (reader_->is_compressed()) {
    return Status::Corruption("bloom file is compressed (compression not supported)",
                              reader_->ToString());
  }
  if (!reader_->has_validx()) {
    return Status::Corruption("bloom file missing value index",
                              reader_->ToString());
  }

  BlockPointer validx_root = reader_->validx_root();

  // Ugly hack: create a per-cpu iterator.
  // Instead this should be threadlocal, or allow us to just
  // stack-allocate these things more smartly!
  int n_cpus = base::MaxCPUIndex() + 1;
  for (int i = 0; i < n_cpus; i++) {
    index_iters_.push_back(
      IndexTreeIterator::Create(reader_.get(), validx_root));
  }
  iter_locks_.reset(new padded_spinlock[n_cpus]);

  // The memory footprint has changed.
  mem_consumption_.Reset(memory_footprint_excluding_reader());

  return Status::OK();
}

Status BloomFileReader::ParseBlockHeader(const Slice &block,
                                         BloomBlockHeaderPB *hdr,
                                         Slice *bloom_data) const {
  Slice data(block);
  if (PREDICT_FALSE(data.size() < 4)) {
    return Status::Corruption("Invalid bloom block header: not enough bytes");
  }

  uint32_t header_len = DecodeFixed32(data.data());
  data.remove_prefix(sizeof(header_len));

  if (header_len > data.size()) {
    return Status::Corruption(
      StringPrintf("Header length %d doesn't fit in buffer of size %ld",
                   header_len, data.size()));
  }

  if (!hdr->ParseFromArray(data.data(), header_len)) {
    return Status::Corruption(
      string("Invalid bloom block header: ") +
      hdr->InitializationErrorString() +
      "\nHeader:" + HexDump(Slice(data.data(), header_len)));
  }

  data.remove_prefix(header_len);
  *bloom_data = data;
  return Status::OK();
}

Status BloomFileReader::CheckKeyPresent(const BloomKeyProbe &probe,
                                        bool *maybe_present) {
  DCHECK(init_once_.initted());

#if defined(__linux__)
  int cpu = sched_getcpu();
#else
  // Use just one lock if on OS X.
  int cpu = 0;
#endif
  BlockPointer bblk_ptr;
  {
    std::unique_lock<simple_spinlock> lock;
    while (true) {
      std::unique_lock<simple_spinlock> l(iter_locks_[cpu], std::try_to_lock);
      if (l.owns_lock()) {
        lock.swap(l);
        break;
      }
      cpu = (cpu + 1) % index_iters_.size();
    }

    cfile::IndexTreeIterator *index_iter = &index_iters_[cpu];

    Status s = index_iter->SeekAtOrBefore(probe.key());
    if (PREDICT_FALSE(s.IsNotFound())) {
      // Seek to before the first entry in the file.
      *maybe_present = false;
      return Status::OK();
    }
    RETURN_NOT_OK(s);

    // Successfully found the pointer to the bloom block. Read it.
    bblk_ptr = index_iter->GetCurrentBlockPointer();
  }

  BlockHandle dblk_data;
  RETURN_NOT_OK(reader_->ReadBlock(bblk_ptr, CFileReader::CACHE_BLOCK, &dblk_data));

  // Parse the header in the block.
  BloomBlockHeaderPB hdr;
  Slice bloom_data;
  RETURN_NOT_OK(ParseBlockHeader(dblk_data.data(), &hdr, &bloom_data));

  // Actually check the bloom filter.
  BloomFilter bf(bloom_data, hdr.num_hash_functions());
  *maybe_present = bf.MayContainKey(probe);
  return Status::OK();
}

size_t BloomFileReader::memory_footprint_excluding_reader() const {
  size_t size = kudu_malloc_usable_size(this);

  size += init_once_.memory_footprint_excluding_this();

  // This seems to be the easiest way to get a heap pointer to the ptr_vector.
  //
  // TODO: Track the iterators' memory footprint? May change with every seek;
  // not clear if it's worth doing.
  size += kudu_malloc_usable_size(
      const_cast<BloomFileReader*>(this)->index_iters_.c_array());
  for (int i = 0; i < index_iters_.size(); i++) {
    size += kudu_malloc_usable_size(&index_iters_[i]);
  }

  if (iter_locks_) {
    size += kudu_malloc_usable_size(iter_locks_.get());
  }

  return size;
}

} // namespace cfile
} // namespace kudu

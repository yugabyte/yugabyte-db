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

#include "kudu/fs/log_block_manager.h"


#include "kudu/fs/block_manager_metrics.h"
#include "kudu/fs/block_manager_util.h"
#include "kudu/gutil/callback.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/gutil/strings/strip.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/walltime.h"
#include "kudu/util/alignment.h"
#include "kudu/util/atomic.h"
#include "kudu/util/env.h"
#include "kudu/util/env_util.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/malloc.h"
#include "kudu/util/metrics.h"
#include "kudu/util/mutex.h"
#include "kudu/util/path_util.h"
#include "kudu/util/pb_util.h"
#include "kudu/util/random_util.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/threadpool.h"

// TODO: How should this be configured? Should provide some guidance.
DEFINE_uint64(log_container_max_size, 10LU * 1024 * 1024 * 1024,
              "Maximum size (soft) of a log container");
TAG_FLAG(log_container_max_size, advanced);

DEFINE_uint64(log_container_preallocate_bytes, 32LU * 1024 * 1024,
              "Number of bytes to preallocate in a log container when "
              "creating new blocks. Set to 0 to disable preallocation");
TAG_FLAG(log_container_preallocate_bytes, advanced);

DEFINE_bool(log_block_manager_test_hole_punching, true,
            "Ensure hole punching is supported by the underlying filesystem");
TAG_FLAG(log_block_manager_test_hole_punching, advanced);
TAG_FLAG(log_block_manager_test_hole_punching, unsafe);

DECLARE_bool(enable_data_block_fsync);
DECLARE_bool(block_manager_lock_dirs);

METRIC_DEFINE_gauge_uint64(server, log_block_manager_bytes_under_management,
                           "Bytes Under Management",
                           kudu::MetricUnit::kBytes,
                           "Number of bytes of data blocks currently under management");

METRIC_DEFINE_gauge_uint64(server, log_block_manager_blocks_under_management,
                           "Blocks Under Management",
                           kudu::MetricUnit::kBlocks,
                           "Number of data blocks currently under management");

METRIC_DEFINE_counter(server, log_block_manager_containers,
                      "Number of Block Containers",
                      kudu::MetricUnit::kLogBlockContainers,
                      "Number of log block containers");

METRIC_DEFINE_counter(server, log_block_manager_full_containers,
                      "Number of Full Block Counters",
                      kudu::MetricUnit::kLogBlockContainers,
                      "Number of full log block containers");

using std::unordered_map;
using std::unordered_set;
using strings::Substitute;
using kudu::env_util::ScopedFileDeleter;
using kudu::fs::internal::LogBlock;
using kudu::fs::internal::LogBlockContainer;
using kudu::pb_util::ReadablePBContainerFile;
using kudu::pb_util::WritablePBContainerFile;

namespace kudu {

namespace fs {

namespace internal {

////////////////////////////////////////////////////////////
// LogBlockManagerMetrics
////////////////////////////////////////////////////////////

// Metrics container associated with the log block manager.
//
// Includes implementation-agnostic metrics as well as some that are
// specific to the log block manager.
struct LogBlockManagerMetrics {
  explicit LogBlockManagerMetrics(const scoped_refptr<MetricEntity>& metric_entity);

  // Implementation-agnostic metrics.
  BlockManagerMetrics generic_metrics;

  scoped_refptr<AtomicGauge<uint64_t> > bytes_under_management;
  scoped_refptr<AtomicGauge<uint64_t> > blocks_under_management;

  scoped_refptr<Counter> containers;
  scoped_refptr<Counter> full_containers;
};

#define MINIT(x) x(METRIC_log_block_manager_##x.Instantiate(metric_entity))
#define GINIT(x) x(METRIC_log_block_manager_##x.Instantiate(metric_entity, 0))
LogBlockManagerMetrics::LogBlockManagerMetrics(const scoped_refptr<MetricEntity>& metric_entity)
  : generic_metrics(metric_entity),
    GINIT(bytes_under_management),
    GINIT(blocks_under_management),
    MINIT(containers),
    MINIT(full_containers) {
}
#undef GINIT
#undef MINIT

////////////////////////////////////////////////////////////
// LogBlockContainer
////////////////////////////////////////////////////////////

// A single block container belonging to the log-backed block manager.
//
// A container may only be used to write one WritableBlock at a given time.
// However, existing blocks may be deleted concurrently. As such, almost
// all container functions must be reentrant, even if the container itself
// is logically thread unsafe (i.e. multiple clients calling WriteData()
// concurrently will produce nonsensical container data). Thread unsafe
// functions are marked explicitly.
class LogBlockContainer {
 public:
  static const std::string kMetadataFileSuffix;
  static const std::string kDataFileSuffix;
  static const char* kMagic;

  // Creates a new block container in 'dir'.
  static Status Create(LogBlockManager* block_manager,
                       PathInstanceMetadataPB* instance,
                       const std::string& dir,
                       gscoped_ptr<LogBlockContainer>* container);

  // Opens an existing block container in 'dir'.
  //
  // Every container is comprised of two files: "<dir>/<id>.data" and
  // "<dir>/<id>.metadata". Together, 'dir' and 'id' fully describe both files.
  static Status Open(LogBlockManager* block_manager,
                     PathInstanceMetadataPB* instance,
                     const std::string& dir,
                     const std::string& id,
                     gscoped_ptr<LogBlockContainer>* container);

  // Indicates that the writing of 'block' is finished. If successful,
  // adds the block to the block manager's in-memory maps.
  //
  // Returns a status that is either the same as 's' (if !s.ok()) or
  // potentially different (if s.ok() and FinishBlock() failed).
  //
  // After returning, this container has been released to the block manager
  // and may no longer be used in the context of writing 'block'.
  Status FinishBlock(const Status& s, WritableBlock* block);

  // Frees the space associated with a block at 'offset' and 'length'. This
  // is a physical operation, not a logical one; a separate AppendMetadata()
  // is required to record the deletion in container metadata.
  //
  // The on-disk effects of this call are made durable only after SyncData().
  Status DeleteBlock(int64_t offset, int64_t length);

  // Writes 'data' to this container's data file at offset 'offset'.
  //
  // The on-disk effects of this call are made durable only after SyncData().
  Status WriteData(int64_t offset, const Slice& data);

  // See RWFile::Read().
  Status ReadData(int64_t offset, size_t length,
                  Slice* result, uint8_t* scratch) const;

  // Appends 'pb' to this container's metadata file.
  //
  // The on-disk effects of this call are made durable only after SyncMetadata().
  Status AppendMetadata(const BlockRecordPB& pb);

  // Asynchronously flush this container's data file from 'offset' through
  // to 'length'.
  //
  // Does not guarantee data durability; use SyncData() for that.
  Status FlushData(int64_t offset, int64_t length);

  // Asynchronously flush this container's metadata file (all dirty bits).
  //
  // Does not guarantee metadata durability; use SyncMetadata() for that.
  //
  // TODO: Add support to just flush a range.
  Status FlushMetadata();

  // Synchronize this container's data file with the disk. On success,
  // guarantees that the data is made durable.
  //
  // TODO: Add support to synchronize just a range.
  Status SyncData();

  // Synchronize this container's metadata file with the disk. On success,
  // guarantees that the metadata is made durable.
  //
  // TODO: Add support to synchronize just a range.
  Status SyncMetadata();

  // Ensure that 'length' bytes are preallocated in this container,
  // beginning from the position where the last written block ended.
  Status Preallocate(size_t length);

  // Manipulates the block manager's memory tracker on behalf of blocks.
  void ConsumeMemory(int64_t bytes);
  void ReleaseMemory(int64_t bytes);

  // Reads the container's metadata from disk, sanity checking and
  // returning the records.
  Status ReadContainerRecords(deque<BlockRecordPB>* records) const;

  // Updates 'total_bytes_written_', marking this container as full if
  // needed. Should only be called when a block is fully written, as it
  // will round up the container data file's position.
  //
  // This function is thread unsafe.
  void UpdateBytesWritten(int64_t more_bytes);

  // Run a task on this container's root path thread pool.
  //
  // Normally the task is performed asynchronously. However, if submission to
  // the pool fails, it runs synchronously on the current thread.
  void ExecClosure(const Closure& task);

  // Simple accessors.
  std::string dir() const { return DirName(path_); }
  const std::string& ToString() const { return path_; }
  LogBlockManager* block_manager() const { return block_manager_; }
  int64_t total_bytes_written() const { return total_bytes_written_; }
  bool full() const {
    return total_bytes_written_ >=  FLAGS_log_container_max_size;
  }
  const LogBlockManagerMetrics* metrics() const { return metrics_; }
  const PathInstanceMetadataPB* instance() const { return instance_; }

 private:
  // RAII-style class for finishing containers in FinishBlock().
  class ScopedFinisher {
   public:
    // 'container' must outlive the finisher.
    explicit ScopedFinisher(LogBlockContainer* container) :
      container_(container) {
    }
    ~ScopedFinisher() {
      container_->block_manager()->MakeContainerAvailable(container_);
    }
   private:
    LogBlockContainer* container_;
  };

  LogBlockContainer(LogBlockManager* block_manager,
                    PathInstanceMetadataPB* instance, std::string path,
                    gscoped_ptr<WritablePBContainerFile> metadata_writer,
                    gscoped_ptr<RWFile> data_file);

  // Performs sanity checks on a block record.
  void CheckBlockRecord(const BlockRecordPB& record,
                        uint64_t data_file_size) const;

  // The owning block manager. Must outlive the container itself.
  LogBlockManager* const block_manager_;

  // The path to the container's files. Equivalent to "<dir>/<id>" (see the
  // container constructor).
  const std::string path_;

  // Opened file handles to the container's files.
  //
  // WritableFile is not thread safe so access to each writer must be
  // serialized through a (sleeping) mutex. We use different mutexes to
  // avoid contention in cases where only one writer is needed.
  gscoped_ptr<WritablePBContainerFile> metadata_pb_writer_;
  Mutex metadata_pb_writer_lock_;
  Mutex data_writer_lock_;
  gscoped_ptr<RWFile> data_file_;

  // The amount of data written thus far in the container.
  int64_t total_bytes_written_;

  // The metrics. Not owned by the log container; it has the same lifespan
  // as the block manager.
  const LogBlockManagerMetrics* metrics_;

  const PathInstanceMetadataPB* instance_;

  DISALLOW_COPY_AND_ASSIGN(LogBlockContainer);
};

const std::string LogBlockContainer::kMetadataFileSuffix(".metadata");
const std::string LogBlockContainer::kDataFileSuffix(".data");

LogBlockContainer::LogBlockContainer(
    LogBlockManager* block_manager, PathInstanceMetadataPB* instance,
    string path, gscoped_ptr<WritablePBContainerFile> metadata_writer,
    gscoped_ptr<RWFile> data_file)
    : block_manager_(block_manager),
      path_(std::move(path)),
      metadata_pb_writer_(metadata_writer.Pass()),
      data_file_(data_file.Pass()),
      total_bytes_written_(0),
      metrics_(block_manager->metrics()),
      instance_(instance) {}

Status LogBlockContainer::Create(LogBlockManager* block_manager,
                                 PathInstanceMetadataPB* instance,
                                 const string& dir,
                                 gscoped_ptr<LogBlockContainer>* container) {
  string common_path;
  string metadata_path;
  string data_path;
  Status metadata_status;
  Status data_status;
  gscoped_ptr<WritableFile> metadata_writer;
  gscoped_ptr<RWFile> data_file;
  WritableFileOptions wr_opts;
  wr_opts.mode = Env::CREATE_NON_EXISTING;

  // Repeat in the event of a container id collision (unlikely).
  //
  // When looping, we delete any created-and-orphaned files.
  do {
    if (metadata_writer) {
      block_manager->env()->DeleteFile(metadata_path);
    }
    common_path = JoinPathSegments(dir, block_manager->oid_generator()->Next());
    metadata_path = StrCat(common_path, kMetadataFileSuffix);
    metadata_status = block_manager->env()->NewWritableFile(wr_opts,
                                                            metadata_path,
                                                            &metadata_writer);
    if (data_file) {
      block_manager->env()->DeleteFile(data_path);
    }
    data_path = StrCat(common_path, kDataFileSuffix);
    RWFileOptions rw_opts;
    rw_opts.mode = Env::CREATE_NON_EXISTING;
    data_status = block_manager->env()->NewRWFile(rw_opts,
                                                  data_path,
                                                  &data_file);
  } while (PREDICT_FALSE(metadata_status.IsAlreadyPresent() ||
                         data_status.IsAlreadyPresent()));
  if (metadata_status.ok() && data_status.ok()) {
    gscoped_ptr<WritablePBContainerFile> metadata_pb_writer(
        new WritablePBContainerFile(metadata_writer.Pass()));
    RETURN_NOT_OK(metadata_pb_writer->Init(BlockRecordPB()));
    container->reset(new LogBlockContainer(block_manager,
                                           instance,
                                           common_path,
                                           metadata_pb_writer.Pass(),
                                           data_file.Pass()));
    VLOG(1) << "Created log block container " << (*container)->ToString();
  }

  // Prefer metadata status (arbitrarily).
  return !metadata_status.ok() ? metadata_status : data_status;
}

Status LogBlockContainer::Open(LogBlockManager* block_manager,
                               PathInstanceMetadataPB* instance,
                               const string& dir, const string& id,
                               gscoped_ptr<LogBlockContainer>* container) {
  string common_path = JoinPathSegments(dir, id);

  // Open the existing metadata and data files for writing.
  string metadata_path = StrCat(common_path, kMetadataFileSuffix);
  gscoped_ptr<WritableFile> metadata_writer;
  WritableFileOptions wr_opts;
  wr_opts.mode = Env::OPEN_EXISTING;

  RETURN_NOT_OK(block_manager->env()->NewWritableFile(wr_opts,
                                                      metadata_path,
                                                      &metadata_writer));
  gscoped_ptr<WritablePBContainerFile> metadata_pb_writer(
      new WritablePBContainerFile(metadata_writer.Pass()));
  // No call to metadata_pb_writer->Init() because we're reopening an
  // existing pb container (that should already have a valid header).

  string data_path = StrCat(common_path, kDataFileSuffix);
  gscoped_ptr<RWFile> data_file;
  RWFileOptions rw_opts;
  rw_opts.mode = Env::OPEN_EXISTING;
  RETURN_NOT_OK(block_manager->env()->NewRWFile(rw_opts,
                                                data_path,
                                                &data_file));

  // Create the in-memory container and populate it.
  gscoped_ptr<LogBlockContainer> open_container(new LogBlockContainer(block_manager,
                                                                      instance,
                                                                      common_path,
                                                                      metadata_pb_writer.Pass(),
                                                                      data_file.Pass()));
  VLOG(1) << "Opened log block container " << open_container->ToString();
  container->reset(open_container.release());
  return Status::OK();
}

Status LogBlockContainer::ReadContainerRecords(deque<BlockRecordPB>* records) const {
  string metadata_path = StrCat(path_, kMetadataFileSuffix);
  gscoped_ptr<RandomAccessFile> metadata_reader;
  RETURN_NOT_OK(block_manager()->env()->NewRandomAccessFile(metadata_path, &metadata_reader));
  ReadablePBContainerFile pb_reader(metadata_reader.Pass());
  RETURN_NOT_OK(pb_reader.Init());

  uint64_t data_file_size;
  RETURN_NOT_OK(data_file_->Size(&data_file_size));
  deque<BlockRecordPB> local_records;
  Status read_status;
  while (true) {
    local_records.resize(local_records.size() + 1);
    read_status = pb_reader.ReadNextPB(&local_records.back());
    if (!read_status.ok()) {
      // Drop the last element; we didn't use it.
      local_records.pop_back();
      break;
    }
    CheckBlockRecord(local_records.back(), data_file_size);
  }
  Status close_status = pb_reader.Close();
  Status ret = !read_status.IsEndOfFile() ? read_status : close_status;
  if (ret.ok()) {
    records->swap(local_records);
  }
  return ret;
}

void LogBlockContainer::CheckBlockRecord(const BlockRecordPB& record,
                                         uint64_t data_file_size) const {
  if (record.op_type() == CREATE &&
      (!record.has_offset()  ||
       !record.has_length()  ||
        record.offset() < 0  ||
        record.length() < 0  ||
        record.offset() + record.length() > data_file_size)) {
    LOG(FATAL) << "Found malformed block record: " << record.DebugString();
  }
}

Status LogBlockContainer::FinishBlock(const Status& s, WritableBlock* block) {
  ScopedFinisher finisher(this);
  if (!s.ok()) {
    // Early return; 'finisher' makes the container available again.
    return s;
  }

  // A failure when syncing the container means the container (and its new
  // blocks) may be missing the next time the on-disk state is reloaded.
  //
  // As such, it's not correct to add the block to in-memory state unless
  // synchronization succeeds. In the worst case, this means the data file
  // will have written some garbage that can be expunged during a GC.
  RETURN_NOT_OK(block_manager()->SyncContainer(*this));

  CHECK(block_manager()->AddLogBlock(this, block->id(),
                                     total_bytes_written(), block->BytesAppended()));
  UpdateBytesWritten(block->BytesAppended());
  if (full() && block_manager()->metrics()) {
    block_manager()->metrics()->full_containers->Increment();
  }
  return Status::OK();
}

Status LogBlockContainer::DeleteBlock(int64_t offset, int64_t length) {
  DCHECK_GE(offset, 0);
  DCHECK_GE(length, 0);

  // Guaranteed by UpdateBytesWritten().
  DCHECK_EQ(0, offset % instance()->filesystem_block_size_bytes());

  // It is invalid to punch a zero-size hole.
  if (length) {
    lock_guard<Mutex> l(&data_writer_lock_);
    // Round up to the nearest filesystem block so that the kernel will
    // actually reclaim disk space.
    //
    // It's OK if we exceed the file's total size; the kernel will truncate
    // our request.
    return data_file_->PunchHole(offset, KUDU_ALIGN_UP(
        length, instance()->filesystem_block_size_bytes()));
  }
  return Status::OK();
}

Status LogBlockContainer::WriteData(int64_t offset, const Slice& data) {
  DCHECK_GE(offset, 0);

  lock_guard<Mutex> l(&data_writer_lock_);
  return data_file_->Write(offset, data);
}

Status LogBlockContainer::ReadData(int64_t offset, size_t length,
                                   Slice* result, uint8_t* scratch) const {
  DCHECK_GE(offset, 0);

  return data_file_->Read(offset, length, result, scratch);
}

Status LogBlockContainer::AppendMetadata(const BlockRecordPB& pb) {
  lock_guard<Mutex> l(&metadata_pb_writer_lock_);
  return metadata_pb_writer_->Append(pb);
}

Status LogBlockContainer::FlushData(int64_t offset, int64_t length) {
  DCHECK_GE(offset, 0);
  DCHECK_GE(length, 0);

  lock_guard<Mutex> l(&data_writer_lock_);
  return data_file_->Flush(RWFile::FLUSH_ASYNC, offset, length);
}

Status LogBlockContainer::FlushMetadata() {
  lock_guard<Mutex> l(&metadata_pb_writer_lock_);
  return metadata_pb_writer_->Flush();
}

Status LogBlockContainer::SyncData() {
  if (FLAGS_enable_data_block_fsync) {
    lock_guard<Mutex> l(&data_writer_lock_);
    return data_file_->Sync();
  }
  return Status::OK();
}

Status LogBlockContainer::SyncMetadata() {
  if (FLAGS_enable_data_block_fsync) {
    lock_guard<Mutex> l(&metadata_pb_writer_lock_);
    return metadata_pb_writer_->Sync();
  }
  return Status::OK();
}

Status LogBlockContainer::Preallocate(size_t length) {
  return data_file_->PreAllocate(total_bytes_written(), length);
}

void LogBlockContainer::ConsumeMemory(int64_t bytes) {
  block_manager()->mem_tracker_->Consume(bytes);
}

void LogBlockContainer::ReleaseMemory(int64_t bytes) {
  block_manager()->mem_tracker_->Release(bytes);
}

void LogBlockContainer::UpdateBytesWritten(int64_t more_bytes) {
  DCHECK_GE(more_bytes, 0);

  // The number of bytes is rounded up to the nearest filesystem block so
  // that each Kudu block is guaranteed to be on a filesystem block
  // boundary. This guarantees that the disk space can be reclaimed when
  // the block is deleted.
  total_bytes_written_ += KUDU_ALIGN_UP(more_bytes,
                                        instance()->filesystem_block_size_bytes());
  if (full()) {
    VLOG(1) << "Container " << ToString() << " with size "
            << total_bytes_written_ << " is now full, max size is "
            << FLAGS_log_container_max_size;
  }
}

void LogBlockContainer::ExecClosure(const Closure& task) {
  ThreadPool* pool = FindOrDie(block_manager()->thread_pools_by_root_path_,
                               dir());
  Status s = pool->SubmitClosure(task);
  if (!s.ok()) {
    WARN_NOT_OK(
        s, "Could not submit task to thread pool, running it synchronously");
    task.Run();
  }
}

////////////////////////////////////////////////////////////
// LogBlock
////////////////////////////////////////////////////////////

// The persistent metadata that describes a logical block.
//
// A block grows a LogBlock when its data has been synchronized with
// the disk. That's when it's fully immutable (i.e. none of its metadata
// can change), and when it becomes readable and persistent.
//
// LogBlocks are reference counted to simplify support for deletion with
// outstanding readers. All refcount increments are performed with the
// block manager lock held, as are deletion-based decrements. However,
// no lock is held when ~LogReadableBlock decrements the refcount, thus it
// must be made thread safe (by extending RefCountedThreadSafe instead of
// the simpler RefCounted).
class LogBlock : public RefCountedThreadSafe<LogBlock> {
 public:
  LogBlock(LogBlockContainer* container, BlockId block_id, int64_t offset,
           int64_t length);
  ~LogBlock();

  const BlockId& block_id() const { return block_id_; }
  LogBlockContainer* container() const { return container_; }
  int64_t offset() const { return offset_; }
  int64_t length() const { return length_; }

  // Delete the block. Actual deletion takes place when the
  // block is destructed.
  void Delete();

 private:
  // The owning container. Must outlive the LogBlock.
  LogBlockContainer* container_;

  // The block identifier.
  const BlockId block_id_;

  // The block's offset in the container.
  const int64_t offset_;

  // The block's length.
  const int64_t length_;

  // Whether the block has been marked for deletion.
  bool deleted_;

  DISALLOW_COPY_AND_ASSIGN(LogBlock);
};

LogBlock::LogBlock(LogBlockContainer* container, BlockId block_id,
                   int64_t offset, int64_t length)
    : container_(container),
      block_id_(std::move(block_id)),
      offset_(offset),
      length_(length),
      deleted_(false) {
  DCHECK_GE(offset, 0);
  DCHECK_GE(length, 0);

  container_->ConsumeMemory(kudu_malloc_usable_size(this));
}

static void DeleteBlockAsync(LogBlockContainer* container,
                             BlockId block_id,
                             int64_t offset,
                             int64_t length) {
  // We don't call SyncData() to synchronize the deletion because it's
  // expensive, and in the worst case, we'll just leave orphaned data
  // behind to be cleaned up in the next GC.
  VLOG(3) << "Freeing space belonging to block " << block_id;
  WARN_NOT_OK(container->DeleteBlock(offset, length),
              Substitute("Could not delete block $0", block_id.ToString()));
}

LogBlock::~LogBlock() {
  if (deleted_) {
    container_->ExecClosure(Bind(&DeleteBlockAsync, container_, block_id_,
                                 offset_, length_));
  }
  container_->ReleaseMemory(kudu_malloc_usable_size(this));
}

void LogBlock::Delete() {
  DCHECK(!deleted_);
  deleted_ = true;
}

////////////////////////////////////////////////////////////
// LogWritableBlock
////////////////////////////////////////////////////////////

// A log-backed block that has been opened for writing.
//
// There's no reference to a LogBlock as this block has yet to be
// persisted.
class LogWritableBlock : public WritableBlock {
 public:
  enum SyncMode {
    SYNC,
    NO_SYNC
  };

  LogWritableBlock(LogBlockContainer* container, BlockId block_id,
                   int64_t block_offset);

  virtual ~LogWritableBlock();

  virtual Status Close() OVERRIDE;

  virtual Status Abort() OVERRIDE;

  virtual const BlockId& id() const OVERRIDE;

  virtual BlockManager* block_manager() const OVERRIDE;

  virtual Status Append(const Slice& data) OVERRIDE;

  virtual Status FlushDataAsync() OVERRIDE;

  virtual size_t BytesAppended() const OVERRIDE;

  virtual State state() const OVERRIDE;

  // Actually close the block, possibly synchronizing its dirty data and
  // metadata to disk.
  Status DoClose(SyncMode mode);

  // Write this block's metadata to disk.
  //
  // Does not synchronize the written data; that takes place in Close().
  Status AppendMetadata();

 private:

  // RAII-style class for finishing writable blocks in DoClose().
  class ScopedFinisher {
   public:
    // Both 'block' and 's' must outlive the finisher.
    ScopedFinisher(LogWritableBlock* block, Status* s) :
      block_(block),
      status_(s) {
    }
    ~ScopedFinisher() {
      block_->state_ = CLOSED;
      *status_ = block_->container_->FinishBlock(*status_, block_);
    }
   private:
    LogWritableBlock* block_;
    Status* status_;
  };

  // The owning container. Must outlive the block.
  LogBlockContainer* container_;

  // The block's identifier.
  const BlockId block_id_;

  // The block's offset within the container. Known from the moment the
  // block is created.
  const int64_t block_offset_;

  // The block's length. Changes with each Append().
  int64_t block_length_;

  // The state of the block describing where it is in the write lifecycle,
  // for example, has it been synchronized to disk?
  WritableBlock::State state_;

  DISALLOW_COPY_AND_ASSIGN(LogWritableBlock);
};

LogWritableBlock::LogWritableBlock(LogBlockContainer* container,
                                   BlockId block_id, int64_t block_offset)
    : container_(container),
      block_id_(std::move(block_id)),
      block_offset_(block_offset),
      block_length_(0),
      state_(CLEAN) {
  DCHECK_GE(block_offset, 0);
  DCHECK_EQ(0, block_offset % container->instance()->filesystem_block_size_bytes());
  if (container->metrics()) {
    container->metrics()->generic_metrics.blocks_open_writing->Increment();
    container->metrics()->generic_metrics.total_writable_blocks->Increment();
  }
}

LogWritableBlock::~LogWritableBlock() {
  if (state_ != CLOSED) {
    WARN_NOT_OK(Abort(), Substitute("Failed to abort block $0",
                                    id().ToString()));
  }
}

Status LogWritableBlock::Close() {
  return DoClose(SYNC);
}

Status LogWritableBlock::Abort() {
  RETURN_NOT_OK(DoClose(NO_SYNC));

  // DoClose() has unlocked the container; it may be locked by someone else.
  // But block_manager_ is immutable, so this is safe.
  return container_->block_manager()->DeleteBlock(id());
}

const BlockId& LogWritableBlock::id() const {
  return block_id_;
}

BlockManager* LogWritableBlock::block_manager() const {
  return container_->block_manager();
}

Status LogWritableBlock::Append(const Slice& data) {
  DCHECK(state_ == CLEAN || state_ == DIRTY)
      << "Invalid state: " << state_;

  // The metadata change is deferred to Close() or FlushDataAsync(),
  // whichever comes first. We can't do it now because the block's
  // length is still in flux.
  RETURN_NOT_OK(container_->WriteData(block_offset_ + block_length_, data));

  block_length_ += data.size();
  state_ = DIRTY;
  return Status::OK();
}

Status LogWritableBlock::FlushDataAsync() {
  DCHECK(state_ == CLEAN || state_ == DIRTY || state_ == FLUSHING)
      << "Invalid state: " << state_;
  if (state_ == DIRTY) {
    VLOG(3) << "Flushing block " << id();
    RETURN_NOT_OK(container_->FlushData(block_offset_, block_length_));

    RETURN_NOT_OK(AppendMetadata());

    // TODO: Flush just the range we care about.
    RETURN_NOT_OK(container_->FlushMetadata());
  }

  state_ = FLUSHING;
  return Status::OK();
}

size_t LogWritableBlock::BytesAppended() const {
  return block_length_;
}


WritableBlock::State LogWritableBlock::state() const {
  return state_;
}

Status LogWritableBlock::DoClose(SyncMode mode) {
  if (state_ == CLOSED) {
    return Status::OK();
  }

  // Tracks the first failure (if any).
  //
  // It's important that any subsequent failures mutate 's' before
  // returning. Otherwise 'finisher' won't properly provide the first
  // failure to LogBlockContainer::FinishBlock().
  //
  // Note also that when 'finisher' goes out of scope it may mutate 's'.
  Status s;
  {
    ScopedFinisher finisher(this, &s);

    // FlushDataAsync() was not called; append the metadata now.
    if (state_ == CLEAN || state_ == DIRTY) {
      s = AppendMetadata();
      RETURN_NOT_OK(s);
    }

    if (mode == SYNC &&
        (state_ == CLEAN || state_ == DIRTY || state_ == FLUSHING)) {
      VLOG(3) << "Syncing block " << id();

      // TODO: Sync just this block's dirty data.
      s = container_->SyncData();
      RETURN_NOT_OK(s);

      // TODO: Sync just this block's dirty metadata.
      s = container_->SyncMetadata();
      RETURN_NOT_OK(s);

      if (container_->metrics()) {
        container_->metrics()->generic_metrics.blocks_open_writing->Decrement();
        container_->metrics()->generic_metrics.total_bytes_written->IncrementBy(
            BytesAppended());
      }
    }
  }

  return s;
}

Status LogWritableBlock::AppendMetadata() {
  BlockRecordPB record;
  id().CopyToPB(record.mutable_block_id());
  record.set_op_type(CREATE);
  record.set_timestamp_us(GetCurrentTimeMicros());
  record.set_offset(block_offset_);
  record.set_length(block_length_);
  return container_->AppendMetadata(record);
}

////////////////////////////////////////////////////////////
// LogReadableBlock
////////////////////////////////////////////////////////////

// A log-backed block that has been opened for reading.
//
// Refers to a LogBlock representing the block's persisted metadata.
class LogReadableBlock : public ReadableBlock {
 public:
  LogReadableBlock(LogBlockContainer* container,
                   const scoped_refptr<LogBlock>& log_block);

  virtual ~LogReadableBlock();

  virtual Status Close() OVERRIDE;

  virtual const BlockId& id() const OVERRIDE;

  virtual Status Size(uint64_t* sz) const OVERRIDE;

  virtual Status Read(uint64_t offset, size_t length,
                      Slice* result, uint8_t* scratch) const OVERRIDE;

  virtual size_t memory_footprint() const OVERRIDE;

 private:
  // The owning container. Must outlive this block.
  LogBlockContainer* container_;

  // A reference to this block's metadata.
  scoped_refptr<internal::LogBlock> log_block_;

  // Whether or not this block has been closed. Close() is thread-safe, so
  // this must be an atomic primitive.
  AtomicBool closed_;

  DISALLOW_COPY_AND_ASSIGN(LogReadableBlock);
};

LogReadableBlock::LogReadableBlock(LogBlockContainer* container,
                                   const scoped_refptr<LogBlock>& log_block)
  : container_(container),
    log_block_(log_block),
    closed_(false) {
  if (container_->metrics()) {
    container_->metrics()->generic_metrics.blocks_open_reading->Increment();
    container_->metrics()->generic_metrics.total_readable_blocks->Increment();
  }
}

LogReadableBlock::~LogReadableBlock() {
  WARN_NOT_OK(Close(), Substitute("Failed to close block $0",
                                  id().ToString()));
}

Status LogReadableBlock::Close() {
  if (closed_.CompareAndSet(false, true)) {
    log_block_.reset();
    if (container_->metrics()) {
      container_->metrics()->generic_metrics.blocks_open_reading->Decrement();
    }
  }

  return Status::OK();
}

const BlockId& LogReadableBlock::id() const {
  return log_block_->block_id();
}

Status LogReadableBlock::Size(uint64_t* sz) const {
  DCHECK(!closed_.Load());

  *sz = log_block_->length();
  return Status::OK();
}

Status LogReadableBlock::Read(uint64_t offset, size_t length,
                              Slice* result, uint8_t* scratch) const {
  DCHECK(!closed_.Load());

  uint64_t read_offset = log_block_->offset() + offset;
  if (log_block_->length() < offset + length) {
    return Status::IOError("Out-of-bounds read",
                           Substitute("read of [$0-$1) in block [$2-$3)",
                                      read_offset,
                                      read_offset + length,
                                      log_block_->offset(),
                                      log_block_->offset() + log_block_->length()));
  }
  RETURN_NOT_OK(container_->ReadData(read_offset, length, result, scratch));

  if (container_->metrics()) {
    container_->metrics()->generic_metrics.total_bytes_read->IncrementBy(length);
  }
  return Status::OK();
}

size_t LogReadableBlock::memory_footprint() const {
  return kudu_malloc_usable_size(this);
}

} // namespace internal

////////////////////////////////////////////////////////////
// LogBlockManager
////////////////////////////////////////////////////////////

static const char* kBlockManagerType = "log";

LogBlockManager::LogBlockManager(Env* env, const BlockManagerOptions& opts)
  : mem_tracker_(MemTracker::CreateTracker(-1,
                                           "log_block_manager",
                                           opts.parent_mem_tracker)),
    // TODO: C++11 provides a single-arg constructor
    blocks_by_block_id_(10,
                        BlockMap::hasher(),
                        BlockMap::key_equal(),
                        BlockAllocator(mem_tracker_)),
    env_(DCHECK_NOTNULL(env)),
    read_only_(opts.read_only),
    root_paths_(opts.root_paths),
    root_paths_idx_(0),
    rand_(GetRandomSeed32()) {
  DCHECK_GT(root_paths_.size(), 0);
  if (opts.metric_entity) {
    metrics_.reset(new internal::LogBlockManagerMetrics(opts.metric_entity));
  }
}

LogBlockManager::~LogBlockManager() {
  // A LogBlock's destructor depends on its container, so all LogBlocks must be
  // destroyed before their containers.
  blocks_by_block_id_.clear();

  // As LogBlock destructors run, some blocks may be deleted, so we might be
  // waiting here for a little while.
  LOG_SLOW_EXECUTION(INFO, 1000,
                     Substitute("waiting on $0 log block manager thread pools",
                                thread_pools_by_root_path_.size())) {
    for (const ThreadPoolMap::value_type& e :
                  thread_pools_by_root_path_) {
      ThreadPool* p = e.second;
      p->Wait();
      p->Shutdown();
    }
  }

  STLDeleteElements(&all_containers_);
  STLDeleteValues(&thread_pools_by_root_path_);
  STLDeleteValues(&instances_by_root_path_);
  mem_tracker_->UnregisterFromParent();
}

static const char kHolePunchErrorMsg[] =
    "Error during hole punch test. The log block manager requires a "
    "filesystem with hole punching support such as ext4 or xfs. On el6, "
    "kernel version 2.6.32-358 or newer is required. To run without hole "
    "punching (at the cost of some efficiency and scalability), reconfigure "
    "Kudu with --block_manager=file. Refer to the Kudu documentation for more "
    "details. Raw error message follows";

Status LogBlockManager::Create() {
  CHECK(!read_only_);

  RETURN_NOT_OK(Init());

  deque<ScopedFileDeleter*> delete_on_failure;
  ElementDeleter d(&delete_on_failure);

  // The UUIDs and indices will be included in every instance file.
  vector<string> all_uuids(root_paths_.size());
  for (string& u : all_uuids) {
    u = oid_generator()->Next();
  }
  int idx = 0;

  // Ensure the data paths exist and create the instance files.
  unordered_set<string> to_sync;
  for (const string& root_path : root_paths_) {
    bool created;
    RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(env_, root_path, &created),
                          Substitute("Could not create directory $0", root_path));
    if (created) {
      delete_on_failure.push_front(new ScopedFileDeleter(env_, root_path));
      to_sync.insert(DirName(root_path));
    }

    if (FLAGS_log_block_manager_test_hole_punching) {
      RETURN_NOT_OK_PREPEND(CheckHolePunch(root_path), kHolePunchErrorMsg);
    }

    string instance_filename = JoinPathSegments(
        root_path, kInstanceMetadataFileName);
    PathInstanceMetadataFile metadata(env_, kBlockManagerType,
                                      instance_filename);
    RETURN_NOT_OK_PREPEND(metadata.Create(all_uuids[idx], all_uuids), instance_filename);
    delete_on_failure.push_front(new ScopedFileDeleter(env_, instance_filename));
    idx++;
  }

  // Ensure newly created directories are synchronized to disk.
  if (FLAGS_enable_data_block_fsync) {
    for (const string& dir : to_sync) {
      RETURN_NOT_OK_PREPEND(env_->SyncDir(dir),
                            Substitute("Unable to synchronize directory $0", dir));
    }
  }

  // Success: don't delete any files.
  for (ScopedFileDeleter* deleter : delete_on_failure) {
    deleter->Cancel();
  }
  return Status::OK();
}

Status LogBlockManager::Open() {
  RETURN_NOT_OK(Init());

  vector<Status> statuses(root_paths_.size());
  unordered_map<string, PathInstanceMetadataFile*> metadata_files;
  ValueDeleter deleter(&metadata_files);
  for (const string& root_path : root_paths_) {
    InsertOrDie(&metadata_files, root_path, nullptr);
  }

  // Submit each open to its own thread pool and wait for them to complete.
  int i = 0;
  for (const string& root_path : root_paths_) {
    ThreadPool* pool = FindOrDie(thread_pools_by_root_path_, root_path);
    RETURN_NOT_OK_PREPEND(pool->SubmitClosure(
        Bind(&LogBlockManager::OpenRootPath,
             Unretained(this),
             root_path,
             &statuses[i],
             &FindOrDie(metadata_files, root_path))),
                          Substitute("Could not open root path $0", root_path));
    i++;
  }
  for (const ThreadPoolMap::value_type& e :
                thread_pools_by_root_path_) {
    e.second->Wait();
  }

  // Ensure that no tasks failed.
  for (const Status& s : statuses) {
    if (!s.ok()) {
      return s;
    }
  }

  instances_by_root_path_.swap(metadata_files);
  return Status::OK();
}


Status LogBlockManager::CreateBlock(const CreateBlockOptions& opts,
                                    gscoped_ptr<WritableBlock>* block) {
  CHECK(!read_only_);

  // Find a free container. If one cannot be found, create a new one.
  //
  // TODO: should we cap the number of outstanding containers and force
  // callers to block if we've reached it?
  LogBlockContainer* container = GetAvailableContainer();
  if (!container) {
    // Round robin through the root paths to select where the next
    // container should live.
    int32 old_idx;
    int32 new_idx;
    do {
      old_idx = root_paths_idx_.Load();
      new_idx = (old_idx + 1) % root_paths_.size();
    } while (!root_paths_idx_.CompareAndSet(old_idx, new_idx));
    string root_path = root_paths_[old_idx];

    // Guaranteed by LogBlockManager::Open().
    PathInstanceMetadataFile* instance = FindOrDie(instances_by_root_path_, root_path);

    gscoped_ptr<LogBlockContainer> new_container;
    RETURN_NOT_OK(LogBlockContainer::Create(this,
                                            instance->metadata(),
                                            root_path,
                                            &new_container));
    container = new_container.release();
    {
      lock_guard<simple_spinlock> l(&lock_);
      dirty_dirs_.insert(root_path);
      AddNewContainerUnlocked(container);
    }
  }

  // By preallocating with each CreateBlock(), we're effectively
  // maintaining a rolling buffer of preallocated data just ahead of where
  // the next write will fall.
  if (FLAGS_log_container_preallocate_bytes) {
    RETURN_NOT_OK(container->Preallocate(FLAGS_log_container_preallocate_bytes));
  }

  // Generate a free block ID.
  BlockId new_block_id;
  do {
    new_block_id.SetId(rand_.Next64());
  } while (!TryUseBlockId(new_block_id));

  block->reset(new internal::LogWritableBlock(container,
                                              new_block_id,
                                              container->total_bytes_written()));
  VLOG(3) << "Created block " << (*block)->id() << " in container "
          << container->ToString();
  return Status::OK();
}

Status LogBlockManager::CreateBlock(gscoped_ptr<WritableBlock>* block) {
  return CreateBlock(CreateBlockOptions(), block);
}

Status LogBlockManager::OpenBlock(const BlockId& block_id,
                                  gscoped_ptr<ReadableBlock>* block) {
  scoped_refptr<LogBlock> lb;
  {
    lock_guard<simple_spinlock> l(&lock_);
    lb = FindPtrOrNull(blocks_by_block_id_, block_id);
  }
  if (!lb) {
    return Status::NotFound("Can't find block", block_id.ToString());
  }

  block->reset(new internal::LogReadableBlock(lb->container(),
                                              lb.get()));
  VLOG(3) << "Opened block " << (*block)->id()
          << " from container " << lb->container()->ToString();
  return Status::OK();
}

Status LogBlockManager::DeleteBlock(const BlockId& block_id) {
  CHECK(!read_only_);

  scoped_refptr<LogBlock> lb(RemoveLogBlock(block_id));
  if (!lb) {
    return Status::NotFound("Can't find block", block_id.ToString());
  }
  VLOG(3) << "Deleting block " << block_id;
  lb->Delete();

  // Record the on-disk deletion.
  //
  // TODO: what if this fails? Should we restore the in-memory block?
  BlockRecordPB record;
  block_id.CopyToPB(record.mutable_block_id());
  record.set_op_type(DELETE);
  record.set_timestamp_us(GetCurrentTimeMicros());
  RETURN_NOT_OK(lb->container()->AppendMetadata(record));

  // We don't bother fsyncing the metadata append for deletes in order to avoid
  // the disk overhead. Even if we did fsync it, we'd still need to account for
  // garbage at startup time (in the event that we crashed just before the
  // fsync). TODO: Implement GC of orphaned blocks. See KUDU-829.

  return Status::OK();
}

Status LogBlockManager::CloseBlocks(const std::vector<WritableBlock*>& blocks) {
  VLOG(3) << "Closing " << blocks.size() << " blocks";
  if (FLAGS_block_coalesce_close) {
    // Ask the kernel to begin writing out each block's dirty data. This is
    // done up-front to give the kernel opportunities to coalesce contiguous
    // dirty pages.
    for (WritableBlock* block : blocks) {
      RETURN_NOT_OK(block->FlushDataAsync());
    }
  }

  // Now close each block, waiting for each to become durable.
  for (WritableBlock* block : blocks) {
    RETURN_NOT_OK(block->Close());
  }
  return Status::OK();
}

int64_t LogBlockManager::CountBlocksForTests() const {
  lock_guard<simple_spinlock> l(&lock_);
  return blocks_by_block_id_.size();
}

void LogBlockManager::AddNewContainerUnlocked(LogBlockContainer* container) {
  DCHECK(lock_.is_locked());
  all_containers_.push_back(container);
  if (metrics()) {
    metrics()->containers->Increment();
    if (container->full()) {
      metrics()->full_containers->Increment();
    }
  }
}

LogBlockContainer* LogBlockManager::GetAvailableContainer() {
  LogBlockContainer* container = nullptr;
  lock_guard<simple_spinlock> l(&lock_);
  if (!available_containers_.empty()) {
    container = available_containers_.front();
    available_containers_.pop_front();
  }
  return container;
}

void LogBlockManager::MakeContainerAvailable(LogBlockContainer* container) {
  lock_guard<simple_spinlock> l(&lock_);
  MakeContainerAvailableUnlocked(container);
}

void LogBlockManager::MakeContainerAvailableUnlocked(LogBlockContainer* container) {
  DCHECK(lock_.is_locked());
  if (container->full()) {
    return;
  }
  available_containers_.push_back(container);
}

Status LogBlockManager::SyncContainer(const LogBlockContainer& container) {
  Status s;
  bool to_sync = false;
  {
    lock_guard<simple_spinlock> l(&lock_);
    to_sync = dirty_dirs_.erase(container.dir());
  }

  if (to_sync && FLAGS_enable_data_block_fsync) {
    s = env_->SyncDir(container.dir());

    // If SyncDir fails, the container directory must be restored to
    // dirty_dirs_. Otherwise a future successful LogWritableBlock::Close()
    // on this container won't call SyncDir again, and the container might
    // be lost on crash.
    //
    // In the worst case (another block synced this container as we did),
    // we'll sync it again needlessly.
    if (!s.ok()) {
      lock_guard<simple_spinlock> l(&lock_);
      dirty_dirs_.insert(container.dir());
    }
  }
  return s;
}

bool LogBlockManager::TryUseBlockId(const BlockId& block_id) {
  if (block_id.IsNull()) {
    return false;
  }

  lock_guard<simple_spinlock> l(&lock_);
  if (ContainsKey(blocks_by_block_id_, block_id)) {
    return false;
  }
  return InsertIfNotPresent(&open_block_ids_, block_id);
}

bool LogBlockManager::AddLogBlock(LogBlockContainer* container,
                                  const BlockId& block_id,
                                  int64_t offset,
                                  int64_t length) {
  lock_guard<simple_spinlock> l(&lock_);
  scoped_refptr<LogBlock> lb(new LogBlock(container, block_id, offset, length));
  return AddLogBlockUnlocked(lb);
}

bool LogBlockManager::AddLogBlockUnlocked(const scoped_refptr<LogBlock>& lb) {
  DCHECK(lock_.is_locked());

  if (!InsertIfNotPresent(&blocks_by_block_id_, lb->block_id(), lb)) {
    return false;
  }

  // There may already be an entry in open_block_ids_ (e.g. we just finished
  // writing out a block).
  open_block_ids_.erase(lb->block_id());
  if (metrics()) {
    metrics()->blocks_under_management->Increment();
    metrics()->bytes_under_management->IncrementBy(lb->length());
  }
  return true;
}

scoped_refptr<LogBlock> LogBlockManager::RemoveLogBlock(const BlockId& block_id) {
  lock_guard<simple_spinlock> l(&lock_);
  return RemoveLogBlockUnlocked(block_id);
}

scoped_refptr<LogBlock> LogBlockManager::RemoveLogBlockUnlocked(const BlockId& block_id) {
  DCHECK(lock_.is_locked());

  scoped_refptr<LogBlock> result =
      EraseKeyReturnValuePtr(&blocks_by_block_id_, block_id);
  if (result && metrics()) {
    metrics()->blocks_under_management->Decrement();
    metrics()->bytes_under_management->DecrementBy(result->length());
  }
  return result;
}

void LogBlockManager::OpenRootPath(const string& root_path,
                                   Status* result_status,
                                   PathInstanceMetadataFile** result_metadata) {
  if (!env_->FileExists(root_path)) {
    *result_status = Status::NotFound(Substitute(
        "LogBlockManager at $0 not found", root_path));
    return;
  }

  // Open and lock the metadata instance file.
  string instance_filename = JoinPathSegments(
      root_path, kInstanceMetadataFileName);
  gscoped_ptr<PathInstanceMetadataFile> metadata(
      new PathInstanceMetadataFile(env_, kBlockManagerType,
                                   instance_filename));
  Status s = metadata->LoadFromDisk();
  if (!s.ok()) {
    *result_status = s.CloneAndPrepend(Substitute(
        "Could not open $0", instance_filename));
    return;
  }
  if (FLAGS_block_manager_lock_dirs) {
    s = metadata->Lock();
    if (!s.ok()) {
      Status new_status = s.CloneAndPrepend(Substitute(
          "Could not lock $0", instance_filename));
      if (read_only_) {
        // Not fatal in read-only mode.
        LOG(WARNING) << new_status.ToString();
        LOG(WARNING) << "Proceeding without lock";
      } else {
        *result_status = new_status;
        return;
      }
    }
  }

  // Find all containers and open them.
  vector<string> children;
  s = env_->GetChildren(root_path, &children);
  if (!s.ok()) {
    *result_status = s.CloneAndPrepend(Substitute(
        "Could not list children of $0", root_path));
    return;
  }
  for (const string& child : children) {
    string id;
    if (!TryStripSuffixString(child, LogBlockContainer::kMetadataFileSuffix, &id)) {
      continue;
    }
    gscoped_ptr<LogBlockContainer> container;
    s = LogBlockContainer::Open(this, metadata->metadata(),
                                root_path, id, &container);
    if (!s.ok()) {
      *result_status = s.CloneAndPrepend(Substitute(
          "Could not open container $0", id));
      return;
    }

    // Populate the in-memory block maps using each container's records.
    deque<BlockRecordPB> records;
    s = container->ReadContainerRecords(&records);
    if (!s.ok()) {
      *result_status = s.CloneAndPrepend(Substitute(
          "Could not read records from container $0", container->ToString()));
      return;
    }

    // Process the records, building a container-local map.
    //
    // It's important that we don't try to add these blocks to the global map
    // incrementally as we see each record, since it's possible that one container
    // has a "CREATE <b>" while another has a "CREATE <b> ; DELETE <b>" pair.
    // If we processed those two containers in this order, then upon processing
    // the second container, we'd think there was a duplicate block. Building
    // the container-local map first ensures that we discount deleted blocks
    // before checking for duplicate IDs.
    UntrackedBlockMap blocks_in_container;
    for (const BlockRecordPB& r : records) {
      ProcessBlockRecord(r, container.get(), &blocks_in_container);
    }

    // Under the lock, merge this map into the main block map and add
    // the container.
    {
      lock_guard<simple_spinlock> l(&lock_);
      for (const UntrackedBlockMap::value_type& e : blocks_in_container) {
        if (!AddLogBlockUnlocked(e.second)) {
          LOG(FATAL) << "Found duplicate CREATE record for block " << e.first
                     << " which already is alive from another container when "
                     << " processing container " << container->ToString();
        }
      }

      AddNewContainerUnlocked(container.get());
      MakeContainerAvailableUnlocked(container.release());
    }
  }

  *result_status = Status::OK();
  *result_metadata = metadata.release();
}

void LogBlockManager::ProcessBlockRecord(const BlockRecordPB& record,
                                         LogBlockContainer* container,
                                         UntrackedBlockMap* block_map) {
  BlockId block_id(BlockId::FromPB(record.block_id()));
  switch (record.op_type()) {
    case CREATE: {
      scoped_refptr<LogBlock> lb(new LogBlock(container, block_id,
                                              record.offset(), record.length()));
      if (!InsertIfNotPresent(block_map, block_id, lb)) {
        LOG(FATAL) << "Found duplicate CREATE record for block "
                   << block_id.ToString() << " in container "
                   << container->ToString() << ": "
                   << record.DebugString();
      }

      VLOG(2) << Substitute("Found CREATE block $0 at offset $1 with length $2",
                            block_id.ToString(),
                            record.offset(), record.length());

      // This block must be included in the container's logical size, even if
      // it has since been deleted. This helps satisfy one of our invariants:
      // once a container byte range has been used, it may never be reused in
      // the future.
      //
      // If we ignored deleted blocks, we would end up reusing the space
      // belonging to the last deleted block in the container.
      container->UpdateBytesWritten(record.length());
      break;
    }
    case DELETE:
      if (block_map->erase(block_id) != 1) {
        LOG(FATAL) << "Found DELETE record for invalid block "
                   << block_id.ToString() << " in container "
                   << container->ToString() << ": "
                   << record.DebugString();
      }
      VLOG(2) << Substitute("Found DELETE block $0", block_id.ToString());
      break;
    default:
      LOG(FATAL) << "Found unknown op type in block record: "
                 << record.DebugString();
  }
}

Status LogBlockManager::CheckHolePunch(const string& path) {
  // Arbitrary constants.
  static uint64_t kFileSize = 4096 * 4;
  static uint64_t kHoleOffset = 4096;
  static uint64_t kHoleSize = 8192;
  static uint64_t kPunchedFileSize = kFileSize - kHoleSize;

  // Open the test file.
  string filename = JoinPathSegments(path, "hole_punch_test_file");
  gscoped_ptr<RWFile> file;
  RWFileOptions opts;
  RETURN_NOT_OK(env_->NewRWFile(opts, filename, &file));

  // The file has been created; delete it on exit no matter what happens.
  ScopedFileDeleter file_deleter(env_, filename);

  // Preallocate it, making sure the file's size is what we'd expect.
  uint64_t sz;
  RETURN_NOT_OK(file->PreAllocate(0, kFileSize));
  RETURN_NOT_OK(env_->GetFileSizeOnDisk(filename, &sz));
  if (sz != kFileSize) {
    return Status::IOError(Substitute(
        "Unexpected pre-punch file size for $0: expected $1 but got $2",
        filename, kFileSize, sz));
  }

  // Punch the hole, testing the file's size again.
  RETURN_NOT_OK(file->PunchHole(kHoleOffset, kHoleSize));
  RETURN_NOT_OK(env_->GetFileSizeOnDisk(filename, &sz));
  if (sz != kPunchedFileSize) {
    return Status::IOError(Substitute(
        "Unexpected post-punch file size for $0: expected $1 but got $2",
        filename, kPunchedFileSize, sz));
  }

  return Status::OK();
}

Status LogBlockManager::Init() {
  // Initialize thread pools.
  ThreadPoolMap pools;
  ValueDeleter d(&pools);
  int i = 0;
  for (const string& root : root_paths_) {
    gscoped_ptr<ThreadPool> p;
    RETURN_NOT_OK_PREPEND(ThreadPoolBuilder(Substitute("lbm root $0", i++))
                          .set_max_threads(1)
                          .Build(&p),
                          "Could not build thread pool");
    InsertOrDie(&pools, root, p.release());
  }
  thread_pools_by_root_path_.swap(pools);

  return Status::OK();
}

} // namespace fs
} // namespace kudu

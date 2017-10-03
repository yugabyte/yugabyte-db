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

#include "kudu/fs/file_block_manager.h"

#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

#include "kudu/fs/block_manager_metrics.h"
#include "kudu/fs/block_manager_util.h"
#include "kudu/fs/fs.pb.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/atomic.h"
#include "kudu/util/env.h"
#include "kudu/util/env_util.h"
#include "kudu/util/malloc.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/metrics.h"
#include "kudu/util/oid_generator.h"
#include "kudu/util/path_util.h"
#include "kudu/util/random_util.h"
#include "kudu/util/status.h"

using kudu::env_util::ScopedFileDeleter;
using std::shared_ptr;
using std::string;
using std::unordered_set;
using std::vector;
using strings::Substitute;

DECLARE_bool(enable_data_block_fsync);
DECLARE_bool(block_manager_lock_dirs);

namespace kudu {
namespace fs {

namespace internal {

////////////////////////////////////////////////////////////
// FileBlockLocation
////////////////////////////////////////////////////////////

// Logical location of a block in the file block manager.
//
// A block ID uniquely locates a block. Every ID is a uint64_t, broken down
// into multiple logical components:
// 1. Bytes 0 (MSB) and 1 identify the block's root path by path set index. See
//    fs.proto for more details on path sets.
// 2. Bytes 2-7 (LSB) uniquely identify the block within the root path. As more
//    and more blocks are created in a root path, the likelihood of a collision
//    becomes greater. In the event of a collision, the block manager will
//    retry(see CreateBlock()).
//
// A FileBlockLocation abstracts away these details so that clients need not
// worry about them. It is constructed via FromParts() or FromBlockId() and is
// copyable and assignable.
class FileBlockLocation {
 public:
  // Empty constructor
  FileBlockLocation() {
  }

  // Construct a location from its constituent parts.
  static FileBlockLocation FromParts(const string& root_path,
                                     uint16_t root_path_idx,
                                     const BlockId& block_id);

  // Construct a location from a full block ID.
  static FileBlockLocation FromBlockId(const string& root_path,
                                       const BlockId& block_id);

  // Get the root path index of a given block ID.
  static uint16_t GetRootPathIdx(const BlockId& block_id) {
    return block_id.id() >> 48;
  }

  // Returns the full filesystem path for this location.
  string GetFullPath() const;

  // Create all subdirectories needed for this location.
  //
  // On success, 'created_dirs' contains the directories that were actually
  // created (as opposed to those that were reused).
  Status CreateBlockDir(Env* env, vector<string>* created_dirs);

  // Writes all parent directories that are part of this location to
  // 'parent_dirs'.
  //
  // The directories are written in "fsync order"; that is, the order in
  // which they should be fsynced to make them durable.
  void GetAllParentDirs(vector<string>* parent_dirs) const;

  // Simple accessors.
  const string& root_path() const { return root_path_; }
  const BlockId& block_id() const { return block_id_; }

 private:
  FileBlockLocation(string root_path, BlockId block_id)
      : root_path_(std::move(root_path)), block_id_(std::move(block_id)) {}

  // These per-byte accessors yield subdirectories in which blocks are grouped.
  string byte2() const {
    return StringPrintf("%02llx",
                        (block_id_.id() & 0x0000FF0000000000ULL) >> 40);
  }
  string byte3() const {
    return StringPrintf("%02llx",
                        (block_id_.id() & 0x000000FF00000000ULL) >> 32);
  }
  string byte4() const {
    return StringPrintf("%02llx",
                        (block_id_.id() & 0x00000000FF000000ULL) >> 24);
  }

  string root_path_;
  BlockId block_id_;
};

FileBlockLocation FileBlockLocation::FromParts(const string& root_path,
                                               uint16_t root_path_idx,
                                               const BlockId& block_id) {
  // The combined ID consists of 'root_path_idx' (top 2 bytes) and 'block_id'
  // (bottom 6 bytes). The top 2 bytes of 'block_id' are dropped.
  uint64_t combined_id = static_cast<uint64_t>(root_path_idx) << 48;
  combined_id |= block_id.id() & ((1ULL << 48) - 1);
  return FileBlockLocation(root_path, BlockId(combined_id));
}

FileBlockLocation FileBlockLocation::FromBlockId(const string& root_path,
                                                 const BlockId& block_id) {
  return FileBlockLocation(root_path, block_id);
}

string FileBlockLocation::GetFullPath() const {
  string p = root_path_;
  p = JoinPathSegments(p, byte2());
  p = JoinPathSegments(p, byte3());
  p = JoinPathSegments(p, byte4());
  p = JoinPathSegments(p, block_id_.ToString());
  return p;
}

Status FileBlockLocation::CreateBlockDir(Env* env,
                                         vector<string>* created_dirs) {
  DCHECK(env->FileExists(root_path_));

  bool path0_created;
  string path0 = JoinPathSegments(root_path_, byte2());
  RETURN_NOT_OK(env_util::CreateDirIfMissing(env, path0, &path0_created));

  bool path1_created;
  string path1 = JoinPathSegments(path0, byte3());
  RETURN_NOT_OK(env_util::CreateDirIfMissing(env, path1, &path1_created));

  bool path2_created;
  string path2 = JoinPathSegments(path1, byte4());
  RETURN_NOT_OK(env_util::CreateDirIfMissing(env, path2, &path2_created));

  if (path2_created) {
    created_dirs->push_back(path1);
  }
  if (path1_created) {
    created_dirs->push_back(path0);
  }
  if (path0_created) {
    created_dirs->push_back(root_path_);
  }
  return Status::OK();
}

void FileBlockLocation::GetAllParentDirs(vector<string>* parent_dirs) const {
  string path0 = JoinPathSegments(root_path_, byte2());
  string path1 = JoinPathSegments(path0, byte3());
  string path2 = JoinPathSegments(path1, byte4());

  // This is the order in which the parent directories should be
  // synchronized to disk.
  parent_dirs->push_back(path2);
  parent_dirs->push_back(path1);
  parent_dirs->push_back(path0);
  parent_dirs->push_back(root_path_);
}

////////////////////////////////////////////////////////////
// FileWritableBlock
////////////////////////////////////////////////////////////

// A file-backed block that has been opened for writing.
//
// Contains a pointer to the block manager as well as a FileBlockLocation
// so that dirty metadata can be synced via BlockManager::SyncMetadata()
// at Close() time. Embedding a FileBlockLocation (and not a simpler
// BlockId) consumes more memory, but the number of outstanding
// FileWritableBlock instances is expected to be low.
class FileWritableBlock : public WritableBlock {
 public:
  FileWritableBlock(FileBlockManager* block_manager, FileBlockLocation location,
                    shared_ptr<WritableFile> writer);

  virtual ~FileWritableBlock();

  virtual Status Close() OVERRIDE;

  virtual Status Abort() OVERRIDE;

  virtual BlockManager* block_manager() const OVERRIDE;

  virtual const BlockId& id() const OVERRIDE;

  virtual Status Append(const Slice& data) OVERRIDE;

  virtual Status FlushDataAsync() OVERRIDE;

  virtual size_t BytesAppended() const OVERRIDE;

  virtual State state() const OVERRIDE;

 private:
  enum SyncMode {
    SYNC,
    NO_SYNC
  };

  // Close the block, optionally synchronizing dirty data and metadata.
  Status Close(SyncMode mode);

  // Back pointer to the block manager.
  //
  // Should remain alive for the lifetime of this block.
  FileBlockManager* block_manager_;

  // The block's location.
  const FileBlockLocation location_;

  // The underlying opened file backing this block.
  shared_ptr<WritableFile> writer_;

  State state_;

  // The number of bytes successfully appended to the block.
  size_t bytes_appended_;

  DISALLOW_COPY_AND_ASSIGN(FileWritableBlock);
};

FileWritableBlock::FileWritableBlock(FileBlockManager* block_manager,
                                     FileBlockLocation location,
                                     shared_ptr<WritableFile> writer)
    : block_manager_(block_manager),
      location_(std::move(location)),
      writer_(std::move(writer)),
      state_(CLEAN),
      bytes_appended_(0) {
  if (block_manager_->metrics_) {
    block_manager_->metrics_->blocks_open_writing->Increment();
    block_manager_->metrics_->total_writable_blocks->Increment();
  }
}

FileWritableBlock::~FileWritableBlock() {
  if (state_ != CLOSED) {
    WARN_NOT_OK(Abort(), Substitute("Failed to close block $0",
                                    id().ToString()));
  }
}

Status FileWritableBlock::Close() {
  return Close(SYNC);
}

Status FileWritableBlock::Abort() {
  RETURN_NOT_OK(Close(NO_SYNC));
  return block_manager()->DeleteBlock(id());
}

BlockManager* FileWritableBlock::block_manager() const {
  return block_manager_;
}

const BlockId& FileWritableBlock::id() const {
  return location_.block_id();
}

Status FileWritableBlock::Append(const Slice& data) {
  DCHECK(state_ == CLEAN || state_ == DIRTY)
      << "Invalid state: " << state_;

  RETURN_NOT_OK(writer_->Append(data));
  state_ = DIRTY;
  bytes_appended_ += data.size();
  return Status::OK();
}

Status FileWritableBlock::FlushDataAsync() {
  DCHECK(state_ == CLEAN || state_ == DIRTY || state_ == FLUSHING)
      << "Invalid state: " << state_;
  if (state_ == DIRTY) {
    VLOG(3) << "Flushing block " << id();
    RETURN_NOT_OK(writer_->Flush(WritableFile::FLUSH_ASYNC));
  }

  state_ = FLUSHING;
  return Status::OK();
}

size_t FileWritableBlock::BytesAppended() const {
  return bytes_appended_;
}

WritableBlock::State FileWritableBlock::state() const {
  return state_;
}

Status FileWritableBlock::Close(SyncMode mode) {
  if (state_ == CLOSED) {
    return Status::OK();
  }

  Status sync;
  if (mode == SYNC &&
      (state_ == CLEAN || state_ == DIRTY || state_ == FLUSHING)) {
    // Safer to synchronize data first, then metadata.
    VLOG(3) << "Syncing block " << id();
    if (FLAGS_enable_data_block_fsync) {
      sync = writer_->Sync();
    }
    if (sync.ok()) {
      sync = block_manager_->SyncMetadata(location_);
    }
    WARN_NOT_OK(sync, Substitute("Failed to sync when closing block $0",
                                 id().ToString()));
  }
  Status close = writer_->Close();

  state_ = CLOSED;
  writer_.reset();
  if (block_manager_->metrics_) {
    block_manager_->metrics_->blocks_open_writing->Decrement();
    block_manager_->metrics_->total_bytes_written->IncrementBy(BytesAppended());
  }

  // Prefer the result of Close() to that of Sync().
  return !close.ok() ? close : sync;
}

////////////////////////////////////////////////////////////
// FileReadableBlock
////////////////////////////////////////////////////////////

// A file-backed block that has been opened for reading.
//
// There may be millions of instances of FileReadableBlock outstanding, so
// great care must be taken to reduce its size. To that end, it does _not_
// embed a FileBlockLocation, using the simpler BlockId instead.
class FileReadableBlock : public ReadableBlock {
 public:
  FileReadableBlock(const FileBlockManager* block_manager, BlockId block_id,
                    shared_ptr<RandomAccessFile> reader);

  virtual ~FileReadableBlock();

  virtual Status Close() OVERRIDE;

  virtual const BlockId& id() const OVERRIDE;

  virtual Status Size(uint64_t* sz) const OVERRIDE;

  virtual Status Read(uint64_t offset, size_t length,
                      Slice* result, uint8_t* scratch) const OVERRIDE;

  virtual size_t memory_footprint() const OVERRIDE;

 private:
  // Back pointer to the owning block manager.
  const FileBlockManager* block_manager_;

  // The block's identifier.
  const BlockId block_id_;

  // The underlying opened file backing this block.
  shared_ptr<RandomAccessFile> reader_;

  // Whether or not this block has been closed. Close() is thread-safe, so
  // this must be an atomic primitive.
  AtomicBool closed_;

  DISALLOW_COPY_AND_ASSIGN(FileReadableBlock);
};

FileReadableBlock::FileReadableBlock(const FileBlockManager* block_manager,
                                     BlockId block_id,
                                     shared_ptr<RandomAccessFile> reader)
    : block_manager_(block_manager),
      block_id_(std::move(block_id)),
      reader_(std::move(reader)),
      closed_(false) {
  if (block_manager_->metrics_) {
    block_manager_->metrics_->blocks_open_reading->Increment();
    block_manager_->metrics_->total_readable_blocks->Increment();
  }
}

FileReadableBlock::~FileReadableBlock() {
  WARN_NOT_OK(Close(), Substitute("Failed to close block $0",
                                  id().ToString()));
}

Status FileReadableBlock::Close() {
  if (closed_.CompareAndSet(false, true)) {
    reader_.reset();
    if (block_manager_->metrics_) {
      block_manager_->metrics_->blocks_open_reading->Decrement();
    }
  }

  return Status::OK();
}

const BlockId& FileReadableBlock::id() const {
  return block_id_;
}

Status FileReadableBlock::Size(uint64_t* sz) const {
  DCHECK(!closed_.Load());

  return reader_->Size(sz);
}

Status FileReadableBlock::Read(uint64_t offset, size_t length,
                               Slice* result, uint8_t* scratch) const {
  DCHECK(!closed_.Load());

  RETURN_NOT_OK(env_util::ReadFully(reader_.get(), offset, length, result, scratch));
  if (block_manager_->metrics_) {
    block_manager_->metrics_->total_bytes_read->IncrementBy(length);
  }

  return Status::OK();
}

size_t FileReadableBlock::memory_footprint() const {
  DCHECK(reader_);
  return kudu_malloc_usable_size(this) + reader_->memory_footprint();
}

} // namespace internal

////////////////////////////////////////////////////////////
// FileBlockManager
////////////////////////////////////////////////////////////

static const char* kBlockManagerType = "file";
static const int kMaxPaths = (1 << 16) - 1;

Status FileBlockManager::SyncMetadata(const internal::FileBlockLocation& location) {
  vector<string> parent_dirs;
  location.GetAllParentDirs(&parent_dirs);

  // Figure out what directories to sync.
  vector<string> to_sync;
  {
    lock_guard<simple_spinlock> l(&lock_);
    for (const string& parent_dir : parent_dirs) {
      if (dirty_dirs_.erase(parent_dir)) {
        to_sync.push_back(parent_dir);
      }
    }
  }

  // Sync them.
  if (FLAGS_enable_data_block_fsync) {
    for (const string& s : to_sync) {
      RETURN_NOT_OK(env_->SyncDir(s));
    }
  }
  return Status::OK();
}

bool FileBlockManager::FindBlockPath(const BlockId& block_id,
                                     string* path) const {
  PathInstanceMetadataFile* metadata_file = FindPtrOrNull(
      root_paths_by_idx_, internal::FileBlockLocation::GetRootPathIdx(block_id));
  if (metadata_file) {
    *path = internal::FileBlockLocation::FromBlockId(
        metadata_file->path(), block_id).GetFullPath();
  }
  return metadata_file != nullptr;
}

FileBlockManager::FileBlockManager(Env* env, const BlockManagerOptions& opts)
  : env_(DCHECK_NOTNULL(env)),
    read_only_(opts.read_only),
    root_paths_(opts.root_paths),
    rand_(GetRandomSeed32()),
    mem_tracker_(MemTracker::CreateTracker(-1,
                                           "file_block_manager",
                                           opts.parent_mem_tracker)) {
  DCHECK_GT(root_paths_.size(), 0);
  if (opts.metric_entity) {
    metrics_.reset(new internal::BlockManagerMetrics(opts.metric_entity));
  }
}

FileBlockManager::~FileBlockManager() {
  STLDeleteValues(&root_paths_by_idx_);
  mem_tracker_->UnregisterFromParent();
}

Status FileBlockManager::Create() {
  CHECK(!read_only_);

  deque<ScopedFileDeleter*> delete_on_failure;
  ElementDeleter d(&delete_on_failure);

  if (root_paths_.size() > kMaxPaths) {
    return Status::NotSupported(
        Substitute("File block manager supports a maximum of $0 paths", kMaxPaths));
  }

  // The UUIDs and indices will be included in every instance file.
  ObjectIdGenerator oid_generator;
  vector<string> all_uuids(root_paths_.size());
  for (string& u : all_uuids) {
    u = oid_generator.Next();
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

    string instance_filename = JoinPathSegments(
        root_path, kInstanceMetadataFileName);
    PathInstanceMetadataFile metadata(env_, kBlockManagerType,
                                      instance_filename);
    RETURN_NOT_OK_PREPEND(metadata.Create(all_uuids[idx], all_uuids),
                          Substitute("Could not create $0", instance_filename));
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

Status FileBlockManager::Open() {
  vector<PathInstanceMetadataFile*> instances;
  ElementDeleter deleter(&instances);
  instances.reserve(root_paths_.size());

  for (const string& root_path : root_paths_) {
    if (!env_->FileExists(root_path)) {
      return Status::NotFound(Substitute(
          "FileBlockManager at $0 not found", root_path));
    }
    string instance_filename = JoinPathSegments(
        root_path, kInstanceMetadataFileName);
    gscoped_ptr<PathInstanceMetadataFile> metadata(
        new PathInstanceMetadataFile(env_, kBlockManagerType,
                                     instance_filename));
    RETURN_NOT_OK_PREPEND(metadata->LoadFromDisk(),
                          Substitute("Could not open $0", instance_filename));
    if (FLAGS_block_manager_lock_dirs) {
      Status s = metadata->Lock();
      if (!s.ok()) {
        Status new_status = s.CloneAndPrepend(Substitute(
            "Could not lock $0", instance_filename));
        if (read_only_) {
          // Not fatal in read-only mode.
          LOG(WARNING) << new_status.ToString();
          LOG(WARNING) << "Proceeding without lock";
        } else {
          return new_status;
        }
      }
    }

    instances.push_back(metadata.release());
  }

  RETURN_NOT_OK_PREPEND(PathInstanceMetadataFile::CheckIntegrity(instances),
                        Substitute("Could not verify integrity of files: $0",
                                   JoinStrings(root_paths_, ",")));

  PathMap instances_by_idx;
  for (PathInstanceMetadataFile* instance : instances) {
    const PathSetPB& path_set = instance->metadata()->path_set();
    uint32_t idx = -1;
    for (int i = 0; i < path_set.all_uuids_size(); i++) {
      if (path_set.uuid() == path_set.all_uuids(i)) {
        idx = i;
        break;
      }
    }
    DCHECK_NE(idx, -1); // Guaranteed by CheckIntegrity().
    if (idx > kMaxPaths) {
      return Status::NotSupported(
          Substitute("File block manager supports a maximum of $0 paths", kMaxPaths));
    }
    InsertOrDie(&instances_by_idx, idx, instance);
  }
  instances.clear();
  instances_by_idx.swap(root_paths_by_idx_);
  next_root_path_ = root_paths_by_idx_.begin();
  return Status::OK();
}

Status FileBlockManager::CreateBlock(const CreateBlockOptions& opts,
                                     gscoped_ptr<WritableBlock>* block) {
  CHECK(!read_only_);

  // Pick a root path using a simple round-robin block placement strategy.
  uint16_t root_path_idx;
  string root_path;
  {
    lock_guard<simple_spinlock> l(&lock_);
    root_path_idx = next_root_path_->first;
    root_path = next_root_path_->second->path();
    next_root_path_++;
    if (next_root_path_ == root_paths_by_idx_.end()) {
      next_root_path_ = root_paths_by_idx_.begin();
    }
  }

  string path;
  vector<string> created_dirs;
  Status s;
  internal::FileBlockLocation location;
  shared_ptr<WritableFile> writer;

  // Repeat in case of block id collisions (unlikely).
  do {
    created_dirs.clear();

    // Make sure we don't accidentally create a location using the magic
    // invalid ID value.
    BlockId id;
    do {
      id.SetId(rand_.Next64());
    } while (id.IsNull());

    location = internal::FileBlockLocation::FromParts(
        root_path, root_path_idx, id);
    path = location.GetFullPath();
    RETURN_NOT_OK_PREPEND(location.CreateBlockDir(env_, &created_dirs), path);
    WritableFileOptions wr_opts;
    wr_opts.mode = Env::CREATE_NON_EXISTING;
    s = env_util::OpenFileForWrite(wr_opts, env_, path, &writer);
  } while (PREDICT_FALSE(s.IsAlreadyPresent()));
  if (s.ok()) {
    VLOG(1) << "Creating new block " << location.block_id().ToString() << " at " << path;
    {
      // Update dirty_dirs_ with those provided as well as the block's
      // directory, which may not have been created but is definitely dirty
      // (because we added a file to it).
      lock_guard<simple_spinlock> l(&lock_);
      for (const string& created : created_dirs) {
        dirty_dirs_.insert(created);
      }
      dirty_dirs_.insert(DirName(path));
    }
    block->reset(new internal::FileWritableBlock(this, location, writer));
  }
  return s;
}

Status FileBlockManager::CreateBlock(gscoped_ptr<WritableBlock>* block) {
  return CreateBlock(CreateBlockOptions(), block);
}

Status FileBlockManager::OpenBlock(const BlockId& block_id,
                                   gscoped_ptr<ReadableBlock>* block) {
  string path;
  if (!FindBlockPath(block_id, &path)) {
    return Status::NotFound(
        Substitute("Block $0 not found", block_id.ToString()));
  }

  VLOG(1) << "Opening block with id " << block_id.ToString() << " at " << path;

  shared_ptr<RandomAccessFile> reader;
  RETURN_NOT_OK(env_util::OpenFileForRandom(env_, path, &reader));
  block->reset(new internal::FileReadableBlock(this, block_id, reader));
  return Status::OK();
}

Status FileBlockManager::DeleteBlock(const BlockId& block_id) {
  CHECK(!read_only_);

  string path;
  if (!FindBlockPath(block_id, &path)) {
    return Status::NotFound(
        Substitute("Block $0 not found", block_id.ToString()));
  }
  RETURN_NOT_OK(env_->DeleteFile(path));

  // We don't bother fsyncing the parent directory as there's nothing to be
  // gained by ensuring that the deletion is made durable. Even if we did
  // fsync it, we'd need to account for garbage at startup time (in the
  // event that we crashed just before the fsync), and with such accounting
  // fsync-as-you-delete is unnecessary.
  //
  // The block's directory hierarchy is left behind. We could prune it if
  // it's empty, but that's racy and leaving it isn't much overhead.

  return Status::OK();
}

Status FileBlockManager::CloseBlocks(const vector<WritableBlock*>& blocks) {
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

} // namespace fs
} // namespace kudu

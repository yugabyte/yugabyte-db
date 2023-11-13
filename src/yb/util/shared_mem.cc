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

#include "yb/util/shared_mem.h"

#if defined(__linux__)
#include <mntent.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#endif
#include <fcntl.h>
#include <string>
#include "yb/util/logging.h"

#include "yb/gutil/casts.h"

#include "yb/util/errno.h"
#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"

using std::string;

namespace yb {

namespace {

// File name prefix for anonymous shared memory files.
const char* const kAnonShmFilenamePrefix = "yb_shm_anon";

// Maximum number of temporary file name collisions to tolerate before giving up.
// If this limit is being reached, either the files are not being removed
// properly, or kAnonShmFilenameRandomnessSize is too small.
constexpr int kMaxAnonShmFileCollisionRetries = 20;

// The length (in characters) of the random suffix generated for temporary
// anonymous shared memory files.
constexpr int kAnonShmFilenameRandomnessSize = 16;

// Helper method to memory map a shared memory file using its file descriptor.
Result<void*> MMap(int fd, SharedMemorySegment::AccessMode access_mode, size_t segment_size) {
  void* segment_address = mmap(
      NULL /* addr */,
      segment_size,
      access_mode,
      MAP_SHARED,
      fd,
      0 /* offset */);
  if (segment_address == MAP_FAILED) {
    return STATUS_FORMAT(
        IOError,
        "Error mapping shared memory segment: errno=$0: $1",
        errno,
        ErrnoToString(errno));
  }
  return segment_address;
}

// Returns the directory in which all shared memory files should be created.
std::string GetSharedMemoryDirectory() {
  std::string directory = "/tmp";

#if defined(__linux__)
  auto* mount_file = fopen("/proc/mounts", "r");
  auto se = ScopeExit([&mount_file] {
    if (mount_file) {
      fclose(mount_file);
    }
  });

  if (mount_file) {
    while (struct mntent* mount_info = getmntent(mount_file)) {
      // We want a read-write tmpfs mount.
      if (strcmp(mount_info->mnt_type, "tmpfs") == 0
          && hasmntopt(mount_info, MNTOPT_RW) != nullptr) {
        directory = std::string(mount_info->mnt_dir);

        // Give preference to /dev/shm.
        if (directory == "/dev/shm") {
          break;
        }
      }
    }
  } else if (errno != ENOENT) {
    LOG(ERROR) << "Unexpected error when reading /proc/mounts: errno=" << errno
               << ": " << ErrnoToString(errno);
  }
#endif

  return directory;
}

#if defined(__linux__)
int memfd_create() {
  // This name doesn't really matter, it is only useful for debugging purposes.
  // See http://man7.org/linux/man-pages/man2/memfd_create.2.html.
  return narrow_cast<int>(syscall(__NR_memfd_create, kAnonShmFilenamePrefix, 0 /* flags */));
}

// Attempts to create a shared memory file using memfd_create.
// Returns the file descriptor if successful, otherwise -1 is returned.
int TryMemfdCreate() {
  int fd = -1;

  struct utsname uts_name;
  if (uname(&uts_name) == -1) {
    LOG(ERROR) << "Failed to get kernel name information: errno=" << errno
               << ": " << ErrnoToString(errno);
    return fd;
  }

  char* kernel_version = uts_name.release;
  int major_version = 0;
  int minor_version = 0;
  char garbage;

  std::stringstream version_stream;
  version_stream << kernel_version;
  version_stream >> major_version >> garbage >> minor_version;

  // Check that version is > 3.17. Note: memfd_create is available as of 3.17.8,
  // however we assume that any kernel with version 3.17.x does not have
  // memfd_create to avoid having to parse the patch version.
  auto combined_version = major_version * 1000 + minor_version;
  if (combined_version >= 3017) {
    LOG(INFO) << "Using memfd_create as a shared memory provider";

    fd = memfd_create();
    if (fd == -1) {
      LOG(ERROR) << "Error creating shared memory via memfd_create: errno=" << errno
                 << ": " << ErrnoToString(errno);
    }
  }

  return fd;
}
#endif

// Creates and unlinks a temporary file for use as a shared memory segment.
// Returns the file descriptor, or an error if the creation fails.
Result<int> CreateTempSharedMemoryFile() {
  int fd = -1;

  std::string shared_memory_dir = GetSharedMemoryDirectory();
  LOG(INFO) << "Using directory " << shared_memory_dir << " to store shared memory objects";

  for (int attempt = 0; attempt < kMaxAnonShmFileCollisionRetries; ++attempt) {
    std::string temp_file_name = string(kAnonShmFilenamePrefix) + "_"
        + RandomHumanReadableString(kAnonShmFilenameRandomnessSize);
    std::string temp_file_path = shared_memory_dir + "/" + temp_file_name;

    // We do not use shm_open here, since shm_unlink will automatically close the file
    // descriptor. We want to unlink with the ability to keep the descriptor open.
    fd = open(temp_file_path.c_str(), O_CREAT | O_RDWR | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd == -1) {
      if (errno == EEXIST) {
        // This file already exists, try another random name.
        continue;
      }

      return STATUS_FORMAT(
          IOError,
          "Error creating anonymous shared memory file: errno=$0: $1",
          errno,
          ErrnoToString(errno));
    }

    // Immediately unlink the file to so it will be removed when all file descriptors close.
    if (unlink(temp_file_path.c_str()) == -1) {
      LOG(ERROR) << "Leaking shared memory file '" << temp_file_path
                 << "' after failure to unlink: errno=" << errno
                 << ": " << ErrnoToString(errno);
    }
    break;
  }

  if (fd == -1) {
    return STATUS_FORMAT(
        InternalError,
        "Giving up creating anonymous shared memory segment after $0 failed attempts",
        kMaxAnonShmFileCollisionRetries);
  }

  return fd;
}

}  // namespace

Result<SharedMemorySegment> SharedMemorySegment::Create(size_t segment_size) {
  int fd = -1;
  bool auto_close_fd = true;
  auto se = ScopeExit([&fd, &auto_close_fd] {
    if (fd != -1 && auto_close_fd) {
      close(fd);
    }
  });

#if defined(__linux__)
  // Prefer memfd_create over creating temporary files, if available.
  fd = TryMemfdCreate();
#endif

  if (fd == -1) {
    fd = VERIFY_RESULT(CreateTempSharedMemoryFile());
  }

  // If we have made it here, we should have a valid shared memory file.
  DCHECK_NE(fd, -1);

  if (ftruncate(fd, segment_size) == -1) {
    return STATUS_FORMAT(
        IOError,
        "Error truncating shared memory segment: errno=$0: $1",
        errno,
        ErrnoToString(errno));
  }

  void* segment_address = VERIFY_RESULT(MMap(fd, AccessMode::kReadWrite, segment_size));

  auto_close_fd = false;
  return SharedMemorySegment(segment_address, fd, segment_size);
}

Result<SharedMemorySegment> SharedMemorySegment::Open(
    int fd,
    AccessMode access_mode,
    size_t segment_size) {
  void* segment_address = VERIFY_RESULT(MMap(fd, access_mode, segment_size));
  return SharedMemorySegment(DCHECK_NOTNULL(segment_address), fd, segment_size);
}

SharedMemorySegment::SharedMemorySegment(void* base_address, int fd, size_t segment_size)
    : base_address_(base_address),
      fd_(fd),
      segment_size_(segment_size) {
}

SharedMemorySegment::SharedMemorySegment(SharedMemorySegment&& other)
    : base_address_(other.base_address_),
      fd_(other.fd_),
      segment_size_(other.segment_size_) {
  other.base_address_ = nullptr;
  other.fd_ = -1;
}

SharedMemorySegment::~SharedMemorySegment() {
  if (base_address_ && munmap(base_address_, segment_size_) == -1) {
    LOG(ERROR) << "Failed to unmap shared memory segment: errno=" << errno
               << ": " << ErrnoToString(errno);
  }

  if (fd_ != -1) {
    close(fd_);
  }
}

void* SharedMemorySegment::GetAddress() const {
  return base_address_;
}

int SharedMemorySegment::GetFd() const {
  return fd_;
}

}  // namespace yb

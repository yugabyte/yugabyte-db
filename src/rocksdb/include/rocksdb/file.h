//  Copyright (c) Yugabyte, Inc.  All rights reserved.

#ifndef ROCKSDB_INCLUDE_ROCKSDB_FILE_H
#define ROCKSDB_INCLUDE_ROCKSDB_FILE_H

namespace rocksdb {

class File {
 public:
  virtual ~File() {}

  // Tries to get an unique ID for this file that will be the same each time
  // the file is opened (and will stay the same while the file is open).
  // Furthermore, it tries to make this ID at most "max_size" bytes. If such an
  // ID can be created this function returns the length of the ID and places it
  // in "id"; otherwise, this function returns 0, in which case "id"
  // may not have been modified.
  //
  // This function guarantees, for IDs from a given environment, two unique ids
  // cannot be made equal to eachother by adding arbitrary bytes to one of
  // them. That is, no unique ID is the prefix of another.
  //
  // This function guarantees that the returned ID will not be interpretable as
  // a single varint.
  //
  // Note: these IDs are only valid for the duration of the process.
  virtual size_t GetUniqueId(char *id, size_t max_size) const = 0;
};

}  // namespace rocksdb

#endif  // ROCKSDB_INCLUDE_ROCKSDB_FILE_H

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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Shared fields and methods for querying local files and directories
#pragma once

#include <memory>
#include <string>

#include "yb/util/status_fwd.h"

namespace yb {

class FsManager;
class Schema;
class RandomAccessFile;

namespace tablet {
class RaftGroupMetadata;
}

namespace tools {

struct DumpOptions {
  std::string start_key;
  std::string end_key;
  size_t nrows;
  bool metadata_only;

  DumpOptions()
      : start_key(""),
        end_key(""),
        nrows(0),
        metadata_only(false) {
  }
};

class FsTool {
 public:

  enum DetailLevel {
    MINIMUM = 0, // Minimum amount of information
    HEADERS_ONLY = 1, // Tablet/segment headers only
    MAXIMUM = 2,
  };

  explicit FsTool(DetailLevel detail_level);
  ~FsTool();

  Status Init();

  // Prints out the file system tree.
  Status FsTree();

  // Lists all log segments in the root WALs directory.
  Status ListAllLogSegments();

  // Lists all log segments for tablet 'tablet_id'.
  Status ListLogSegmentsForTablet(const std::string& tablet_id);

  // Lists all tablets in a tablet server's local file system.
  Status ListAllTablets();

  // Prints the header for a log segment residing in 'path'.
  Status PrintLogSegmentHeader(const std::string& path, int indent);

  // Prints the tablet metadata for a tablet 'tablet_id'.
  Status PrintTabletMeta(const std::string& tablet_id, int indent);

  // Dump the data stored in a tablet. The output here is much more readable
  // than DumpTabletBlocks, since it reconstructs docdb values.
  Status DumpTabletData(const std::string& tablet_id);

  // Prints the server's UUID to whom the data belongs and nothing else.
  Status PrintUUID(int indent);
 private:
  Status ListSegmentsInDir(const std::string& segments_dir);

  bool initialized_;
  const DetailLevel detail_level_;
  std::unique_ptr<FsManager> fs_manager_;
};

} // namespace tools
} // namespace yb

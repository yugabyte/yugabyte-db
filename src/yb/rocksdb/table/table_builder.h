//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "yb/rocksdb/db/table_properties_collector.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/immutable_options.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table_properties.h"
#include "yb/rocksdb/util/statistics.h"

#include "yb/util/status_fwd.h"
#include "yb/util/slice.h"

namespace rocksdb {

struct TableReaderOptions {
  // @param skip_filters Disables loading/accessing the filter block
  TableReaderOptions(const ImmutableCFOptions& _ioptions,
                     const EnvOptions& _env_options,
                     const InternalKeyComparatorPtr& _internal_comparator,
                     bool _skip_filters = false)
      : ioptions(_ioptions),
        env_options(_env_options),
        internal_comparator(_internal_comparator),
        skip_filters(_skip_filters) {}

  const ImmutableCFOptions& ioptions;
  const EnvOptions& env_options;
  const InternalKeyComparatorPtr internal_comparator;
  // This is only used for BlockBasedTable (reader)
  bool skip_filters;
};

struct TableBuilderOptions {
  TableBuilderOptions(
      const ImmutableCFOptions& _ioptions,
      const InternalKeyComparatorPtr& _internal_comparator,
      const IntTblPropCollectorFactories& _int_tbl_prop_collector_factories,
      CompressionType _compression_type,
      const CompressionOptions& _compression_opts,
      bool _skip_filters)
      : ioptions(_ioptions),
        internal_comparator(_internal_comparator),
        int_tbl_prop_collector_factories(&_int_tbl_prop_collector_factories),
        compression_type(_compression_type),
        compression_opts(_compression_opts),
        skip_filters(_skip_filters) {}

  const ImmutableCFOptions& ioptions;
  InternalKeyComparatorPtr internal_comparator;
  const IntTblPropCollectorFactories* int_tbl_prop_collector_factories;
  CompressionType compression_type;
  const CompressionOptions& compression_opts;
  // This is only used for BlockBasedTableBuilder
  bool skip_filters = false;
};

// TableBuilder provides the interface used to build a Table
// (an immutable and sorted map from keys to values).
//
// Multiple threads can invoke const methods on a TableBuilder without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same TableBuilder must use
// external synchronization.
class TableBuilder {
 public:
  // REQUIRES: Either Finish() or Abandon() has been called.
  virtual ~TableBuilder() {}

  // Get the block cache prefix as a series of bytes in hexadecimal
  virtual std::string GetBlockCachePrefixHex() const = 0;

  // Add key,value to the table being constructed.
  // REQUIRES: key is after any previously added key according to comparator.
  // REQUIRES: Finish(), Abandon() have not been called
  virtual void Add(const Slice& key, const Slice& value) = 0;

  // Return non-ok iff some error has been detected.
  virtual Status status() const = 0;

  virtual Status Finish() {
    // ... (existing code) ...

    // Log the block cache prefix
    std::string prefix_hex = GetBlockCachePrefixHex();
    ROCKS_LOG_INFO(ioptions_.info_log, "SST file created with block cache prefix: %s", 
                   prefix_hex.c_str());
  }

  // Indicate that the contents of this builder should be abandoned.
  // If the caller is not going to call Finish(), it must call Abandon()
  // before destroying this builder.
  // REQUIRES: Finish(), Abandon() have not been called
  virtual void Abandon() = 0;

  // Number of calls to Add() so far.
  virtual uint64_t NumEntries() const = 0;

  // Total size of the file(s) generated so far.  If invoked after a successful
  // Finish() call, returns the total size of the final generated file(s).
  virtual uint64_t TotalFileSize() const = 0;

  // Size of the base file generated so far.  If invoked after a successful Finish() call, returns
  // the size of the final generated base file. SST is either stored in single base file, or
  // metadata is stored in base file while data is split among data files (S-Blocks).
  virtual uint64_t BaseFileSize() const = 0;

  // Get the size of the file's metadata
  virtual uint64_t MetadataSize() const = 0;

  // Get the size of the file's data
  virtual uint64_t DataSize() const = 0;

  // If the user defined table properties collector suggest the file to
  // be further compacted.
  virtual bool NeedCompact() const { return false; }

  // Returns table properties
  virtual TableProperties GetTableProperties() const = 0;

  virtual const std::string& LastKey() const = 0;
};

}  // namespace rocksdb

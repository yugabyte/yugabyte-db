//
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
//

#pragma once

#include <memory>

#include "yb/util/clone_ptr.h"
#include "yb/util/enums.h"

namespace rocksdb {

class CompactionContext;
class CompactionFeed;
class DataBlockAwareIndexInternalIterator;
class DB;
class DirectWriteHandler;
class Env;
class MemTable;
class InternalIterator;
class Iterator;
class ReadFileFilter;
class Statistics;
class TableAwareReadFileFilter;
class TableReader;
class UserFrontier;
class UserFrontiers;
class WritableFile;
class WriteBatch;

struct BlockBasedTableOptions;
struct CompactionContextOptions;
struct CompactionInputFiles;
struct KeyValueEntry;
struct Options;
struct ReadOptions;
struct TableBuilderOptions;
struct TableProperties;

template <typename IteratorType, bool kSkipLastEntry>
class IteratorWrapperBase;
using IteratorWrapper = IteratorWrapperBase<InternalIterator, /* kSkipLastEntry = */ false>;

template <typename IteratorType>
class MergingIterator;

template <typename IteratorWrapperType>
class MergingIteratorBase;

template <typename IteratorWrapperType>
class MergeIteratorBuilderBase;
using MergeIteratorBuilder = MergeIteratorBuilderBase<IteratorWrapper>;

using CompactionContextPtr = std::unique_ptr<CompactionContext>;

class DirectWriteHandler;

YB_DEFINE_ENUM(FlushAbility, (kNoNewData)(kHasNewData)(kAlreadyFlushing));

// Frontier should be copyable, but should still preserve its polymorphic nature. We cannot use
// shared_ptr here, because we are planning to modify the copied value. If we used shared_ptr and
// modified the copied value, the original value would also change.
using UserFrontierPtr = yb::clone_ptr<UserFrontier>;

} // namespace rocksdb

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

#pragma once

#include <vector>

#include "yb/gutil/ref_counted.h"

#include "yb/util/numbered_deque.h"

namespace yb {
namespace log {

class Log;
class LogAnchorRegistry;
using LogPtr = scoped_refptr<Log>;
class LogEntryBatchPB;
class LogEntryPB;
class LogIndex;
class LogReader;
class LogSegmentFooterPB;
class LogSegmentHeaderPB;
class ReadableLogSegment;
class WritableLogSegment;

struct LogAnchor;
struct LogEntryMetadata;
struct LogIndexBlock;
struct LogIndexEntry;
struct LogMetrics;
struct LogOptions;

using LogAnchorRegistryPtr = scoped_refptr<LogAnchorRegistry>;
using ReadableLogSegmentPtr = scoped_refptr<ReadableLogSegment>;
using SegmentSequence = NumberedDeque<int64_t, ReadableLogSegmentPtr>;
using PreLogRolloverCallback = std::function<void()>;

}  // namespace log
}  // namespace yb

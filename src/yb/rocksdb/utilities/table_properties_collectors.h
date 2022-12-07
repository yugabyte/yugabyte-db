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

#pragma once
#include <memory>

#include "yb/rocksdb/table_properties.h"

namespace rocksdb {

// Creates a factory of a table property collector that marks a SST
// file as need-compaction when it observe at least "D" deletion
// entries in any "N" consecutive entires.
//
// @param sliding_window_size "N". Note that this number will be
//     round up to the smallest multiple of 128 that is no less
//     than the specified size.
// @param deletion_trigger "D".  Note that even when "N" is changed,
//     the specified number for "D" will not be changed.
extern std::shared_ptr<TablePropertiesCollectorFactory>
    NewCompactOnDeletionCollectorFactory(
        size_t sliding_window_size,
        size_t deletion_trigger);
}  // namespace rocksdb

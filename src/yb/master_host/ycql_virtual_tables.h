// Copyright (c) YugabyteDB, Inc.
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

// The YCQL virtual tables (SystemTableDefs: system.local, system_schema.tables and the rest) on a
// Rust master. The Rust leader says what each shows (YcqlVirtualTableInputPB, from its catalog and
// tserver registry); the rows are formatted here as the C++ YQL*VTable classes format them, since
// that needs the engine's schema, type and partition encodings.

#pragma once

#include <functional>
#include <memory>

#include "yb/master/master_rust_peer.pb.h"
#include "yb/master/system_tables.h"
#include "yb/master/yql_virtual_table.h"

namespace yb {

class DnsResolver;

namespace master_host {

using VirtualTableInputSource =
    std::function<Result<master::YcqlVirtualTableInputPB>(const TabletId& tablet_id)>;

// One system table's virtual table, whose rows are read from `source` on every read.
std::unique_ptr<master::YQLVirtualTable> MakeHostVirtualTable(
    const master::SystemTableDef& def, const TabletId& tablet_id,
    const scoped_refptr<MetricEntity>& metric_entity, DnsResolver* resolver,
    VirtualTableInputSource source);

}  // namespace master_host
}  // namespace yb

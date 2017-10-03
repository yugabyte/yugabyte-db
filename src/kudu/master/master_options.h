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
#ifndef KUDU_MASTER_MASTER_OPTIONS_H
#define KUDU_MASTER_MASTER_OPTIONS_H

#include <vector>

#include "kudu/server/server_base_options.h"
#include "kudu/util/net/net_util.h"

namespace kudu {
namespace master {

// Options for constructing the master.
// These are filled in by gflags by default -- see the .cc file for
// the list of options and corresponding flags.
struct MasterOptions : public server::ServerBaseOptions {
  MasterOptions();

  std::vector<HostPort> master_addresses;

  bool IsDistributed() const;
};

} // namespace master
} // namespace kudu
#endif /* KUDU_MASTER_MASTER_OPTIONS_H */

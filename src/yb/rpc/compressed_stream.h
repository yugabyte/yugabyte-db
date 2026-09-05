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
//

#pragma once

#include <boost/version.hpp>

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/mem_tracker.h"

namespace yb {
namespace rpc {

// A compressed stream is named by the layer beneath it, so a messenger offering both
// encrypted and unencrypted transports can register a compressed variant of each.
const Protocol* CompressedStreamProtocol(Encrypted encrypted);

// The protocol must match the lower layer: a stream reports it from GetProtocol(), and
// connections are cached by what they report.
StreamFactoryPtr CompressedStreamFactory(
    StreamFactoryPtr lower_layer_factory, const MemTrackerPtr& buffer_tracker,
    const Protocol* protocol);

}  // namespace rpc
}  // namespace yb

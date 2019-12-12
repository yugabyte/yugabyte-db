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
#ifndef YB_TABLET_TABLET_FWD_H
#define YB_TABLET_TABLET_FWD_H

#include <memory>

#include "yb/gutil/ref_counted.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace tablet {

namespace enterprise {
class Tablet;
}

class AbstractTablet;

class OperationDriver;
typedef scoped_refptr<OperationDriver> OperationDriverPtr;

class RaftGroupMetadata;
typedef scoped_refptr<RaftGroupMetadata> RaftGroupMetadataPtr;

class Tablet;
typedef std::shared_ptr<Tablet> TabletPtr;

class TabletPeer;
typedef std::shared_ptr<TabletPeer> TabletPeerPtr;

class TabletStatusPB;
class TabletStatusListener;
class TransactionParticipant;
class WriteOperationState;

using TabletClass = enterprise::Tablet;

YB_STRONGLY_TYPED_BOOL(RequireLease);
YB_STRONGLY_TYPED_BOOL(IsSysCatalogTablet);
YB_STRONGLY_TYPED_BOOL(TransactionsEnabled);

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_TABLET_FWD_H

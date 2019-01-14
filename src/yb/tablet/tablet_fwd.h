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

#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace tablet {

class AbstractTablet;
class TabletMetadata;
class TabletPeer;
class TabletStatusPB;
class TabletStatusListener;
class WriteOperationState;

class OperationDriver;
typedef scoped_refptr<OperationDriver> OperationDriverPtr;

class Tablet;
typedef std::shared_ptr<Tablet> TabletPtr;

class TabletPeer;
typedef std::shared_ptr<TabletPeer> TabletPeerPtr;

typedef YB_EDITION_NS_PREFIX Tablet TabletClass;
typedef YB_EDITION_NS_PREFIX TabletPeer TabletPeerClass;

YB_STRONGLY_TYPED_BOOL(RequireLease);

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_TABLET_FWD_H

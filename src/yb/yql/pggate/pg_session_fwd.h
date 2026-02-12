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

#include "yb/gutil/ref_counted.h"

#include "yb/util/enums.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::pggate {

class PgSession;
using PgSessionPtr = scoped_refptr<PgSession>;

YB_STRONGLY_TYPED_BOOL(InvalidateOnPgClient);
YB_STRONGLY_TYPED_BOOL(UseCatalogSession);
YB_STRONGLY_TYPED_BOOL(ForceNonBufferable);

YB_DEFINE_ENUM(PgSessionRunOperationMarker, (ExplicitRowLock));

} // namespace yb::pggate

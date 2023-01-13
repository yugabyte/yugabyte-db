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
// Simple header which is inserted into all of our generated protobuf code.
// We use this to hook protobuf code up to TSAN annotations.
#pragma once

#include "yb/gutil/dynamic_annotations.h"

// The protobuf internal headers are included before this, so we have to undefine
// the empty definitions first.
#undef GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN
#undef GOOGLE_SAFE_CONCURRENT_WRITES_END

#define GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN ANNOTATE_IGNORE_WRITES_BEGIN
#define GOOGLE_SAFE_CONCURRENT_WRITES_END ANNOTATE_IGNORE_WRITES_END

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
// This is an alternate varint format, borrowed from sqlite4, that differs from the
// varint in util/coding.h in that its serialized form can be compared with memcmp(),
// yielding the same result as comparing the original integers.
//
// The serialized form also has the property that multiple such varints can be strung
// together to form a composite key, which itself is memcmpable.
//
// See memcmpable_varint.cc for further description.

#pragma once

#include "yb/util/faststring.h"
#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"

namespace yb {

// Appends the varint to the end of the string.
void PutMemcmpableVarint64(std::string *dst, uint64_t value);

// Appends the varint to the end of the string.
void PutMemcmpableVarint64(faststring *dst, uint64_t value);

// Standard Get... routines parse a value from the beginning of a Slice
// and advance the slice past the parsed value.
Status GetMemcmpableVarint64(Slice *input, uint64_t *value);

} // namespace yb

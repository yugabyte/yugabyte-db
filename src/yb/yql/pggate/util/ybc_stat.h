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

// C wrappers around some YB utilities. Suitable for inclusion into C codebases such as our modified
// version of PostgreSQL.

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char *ybcstat_get_wait_event(uint32_t wait_event_info);
extern const char *ybcstat_get_wait_event_type(uint32_t wait_event_info);
extern const char *ybcstat_get_wait_event_component(uint32_t wait_event_info);

#ifdef __cplusplus
} // extern "C"
#endif
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
// Flag Tags provide a way to attach arbitrary textual tags to gflags in
// a global registry. YB uses the following flag tags:
//
// - "stable":
//         These flags are considered user-facing APIs. Therefore, the
//         semantics of the flag should not be changed except between major
//         versions. Similarly, they must not be removed except between major
//         versions.
//
// - "evolving":
//         These flags are considered user-facing APIs, but are not yet
//         locked down. For example, they may pertain to a newly introduced
//         feature that is still being actively developed. These may be changed
//         between minor versions, but should be suitably release-noted.
//
//         This is the default assumed stability level, but can be tagged
//         if you'd like to make it explicit.
//
// - "experimental":
//         These flags are considered user-facing APIs, but are related to
//         an experimental feature, or otherwise likely to change or be
//         removed at any point. Users should not expect any compatibility
//         of these flags.
//
//         TODO: we should add a new flag like -unlock_experimental_flags (NOLINT)
//         which would be required if the user wants to use any of these,
//         similar to the JVM's -XX:+UnlockExperimentalVMOptions.
//
// - "hidden":
//         These flags are for internal use only (e.g. testing) and should
//         not be included in user-facing documentation.
//
// - "advanced":
//         These flags are for advanced users or debugging purposes. While
//         they aren't likely to be actively harmful (see "unsafe" below),
//         they're also likely to be used only rarely and should be relegated
//         to more detailed sections of documentation.
//
// - "unsafe":
//         These flags are for internal use only (e.g. testing), and changing
//         them away from the defaults may result in arbitrarily bad things
//         happening. These flags are automatically excluded from user-facing
//         documentation even if they are not also marked 'hidden'.
//
//         TODO: we should add a flag -unlock_unsafe_flags which would be required (NOLINT)
//         to use any of these flags.
//
// - "runtime":
//         These flags can be safely changed at runtime via an RPC to the
//         server. Changing a flag at runtime that does not have this tag is allowed
//         only if the user specifies a "force_unsafe_change" flag in the RPC.
//
//         NOTE: because gflags are simple global variables, it's important to
//         think very carefully before tagging a flag with 'runtime'. In particular,
//         if a string-type flag is marked 'runtime', you should never access it
//         using the raw 'FLAGS_foo_bar' name. Instead, you must use the
//         google::GetCommandLineFlagInfo(...) API to make a copy of the flag value
//         under a lock. Otherwise, the 'std::string' instance could be mutated
//         underneath the reader causing a crash.
//
//         For primitive-type flags, we assume that reading a variable is atomic.
//         That is to say that a reader will either see the old value or the new
//         one, but not some invalid value. However, for the runtime change to
//         have any effect, you must be sure to use the FLAGS_foo_bar variable directly
//         rather than initializing some instance variable during program startup.
//
// - "sensitive_info":
//         These flags contain sensitive information. Avoid displaying their values anywhere.
//
// - "auto":
//         These are AutoFlags. Do not explicitly set this tag. Use DEFINE_RUNTIME_AUTO_type or
//         DEFINE_NON_RUNTIME_AUTO_type instead.
//
// - "pg":
//         These are gFlag wrappers over postgres guc variables. Only define these using the
//         DEFINE_pg_flag macro. The name and type of the flag should exactly match the guc
//         variable.
//
// - "deprecated":
//         This tag is used to indicate that a flag is deprecated. Do not explicitly set this tag.
//         Use DEPRECATE_FLAG instead.
//
//
// A given flag may have zero or more tags associated with it. The system does
// not make any attempt to check integrity of the tags - for example, it allows
// you to mark a flag as both stable and unstable, even though this makes no
// real sense. Nevertheless, you should strive to meet the following requirements:
//
// - A flag should have exactly no more than one of stable/evolving/experimental
//   indicating its stability. 'evolving' is considered the default.
// - A flag should have no more than one of advanced/hidden indicating visibility
//   in documentation. If neither is specified, the flag will be in the main
//   section of the documentation.
// - It is likely that most 'experimental' flags will also be 'advanced' or 'hidden',
//   and that 'stable' flags are not likely to be 'hidden' or 'unsafe'.
//
// To add a tag to a flag, use the TAG_FLAG macro. For example:
//
//  DEFINE_bool(sometimes_crash, false, "This flag makes YB crash a lot");
//  TAG_FLAG(sometimes_crash, unsafe);
//  TAG_FLAG(sometimes_crash, ::yb::FlagTag::kRuntime, runtime);
//
// To fetch the list of tags associated with a flag, use 'GetFlagTags'.

#pragma once

#include <string>
#include <unordered_set>
#include <boost/preprocessor/cat.hpp>
#include <gflags/gflags.h>

#include "yb/gutil/macros.h"
#include "yb/util/enums.h"

namespace yb {

YB_DEFINE_ENUM(
    FlagTag,
    (kStable)
    (kEvolving)
    (kExperimental)
    (kHidden)
    (kAdvanced)
    (kUnsafe)
    (kRuntime)
    (kSensitive_info)
    (kAuto)
    (kPg)
    (kDeprecated)
    (kPreview));

#define FLAG_TAG_stable ::yb::FlagTag::kStable
#define FLAG_TAG_evolving ::yb::FlagTag::kEvolving
#define FLAG_TAG_experimental ::yb::FlagTag::kExperimental
#define FLAG_TAG_hidden ::yb::FlagTag::kHidden
#define FLAG_TAG_advanced ::yb::FlagTag::kAdvanced
#define FLAG_TAG_unsafe ::yb::FlagTag::kUnsafe
#define FLAG_TAG_sensitive_info ::yb::FlagTag::kSensitive_info
// Disallow explicit use of the following tags
// kRuntime: Use DEFINE_RUNTIME_type macro instead
// kAuto: Use DEFINE_RUNTIME_AUTO_type or DEFINE_NON_RUNTIME_AUTO_type macros instead
// kPg: Use DEFINE_RUNTIME_pg_flag or DEFINE_NON_RUNTIME_pg_flag macros instead
// kDeprecated: Use DEPRECATE_FLAG instead

// Tag the flag 'flag_name' with the given tag 'tag'.
//
// This verifies that 'flag_name' is a valid gflag, which must be defined
// or declared above the use of the TAG_FLAG macro.
//
// This also validates that 'tag' is a valid flag as defined in the FlagTag
// enum above.
#define TAG_FLAG(flag_name, tag) _TAG_FLAG(flag_name, BOOST_PP_CAT(FLAG_TAG_, tag), tag)

// Internal only macro for tagging flags.
#define _TAG_FLAG(flag_name, tag, tag_name) \
  COMPILE_ASSERT(sizeof(BOOST_PP_CAT(FLAGS_, flag_name)), flag_does_not_exist); \
  COMPILE_ASSERT(sizeof(tag), invalid_tag); \
  namespace { \
  ::yb::flag_tags_internal::FlagTagger BOOST_PP_CAT( \
      t_, BOOST_PP_CAT(flag_name, BOOST_PP_CAT(_, tag_name)))(AS_STRING(flag_name), tag); \
  } \
  static_assert(true, "semi-colon required after this macro")

#define _TAG_FLAG_RUNTIME(flag_name) _TAG_FLAG(flag_name, ::yb::FlagTag::kRuntime, runtime)

// Fetch the list of flags associated with the given flag.
//
// If the flag is invalid or has no tags, sets 'tags' to be empty.
void GetFlagTags(const std::string& flag_name,
                 std::unordered_set<FlagTag>* tags);

// ------------------------------------------------------------
// Internal implementation details
// ------------------------------------------------------------
namespace flag_tags_internal {

class FlagTagger {
 public:
  FlagTagger(const char* name, const FlagTag& tag);
  ~FlagTagger();

 private:
  DISALLOW_COPY_AND_ASSIGN(FlagTagger);
};

} // namespace flag_tags_internal

} // namespace yb

#define DEFINE_test_flag(type, name, default_value, description) \
  BOOST_PP_CAT(DEFINE_, type)(TEST_##name, default_value, description " (For testing only!)"); \
  TAG_FLAG(BOOST_PP_CAT(TEST_, name), unsafe); \
  TAG_FLAG(BOOST_PP_CAT(TEST_, name), hidden)

// Runtime flags.
#define DEFINE_RUNTIME_bool(name, default_value, description) \
  DEFINE_bool(name, default_value, description); \
  _TAG_FLAG_RUNTIME(name)

#define DEFINE_RUNTIME_uint32(name, default_value, description) \
  DEFINE_uint32(name, default_value, description); \
  _TAG_FLAG_RUNTIME(name)

#define DEFINE_RUNTIME_int32(name, default_value, description) \
  DEFINE_int32(name, default_value, description); \
  _TAG_FLAG_RUNTIME(name)

#define DEFINE_RUNTIME_int64(name, default_value, description) \
  DEFINE_int64(name, default_value, description); \
  _TAG_FLAG_RUNTIME(name)

#define DEFINE_RUNTIME_uint64(name, default_value, description) \
  DEFINE_uint64(name, default_value, description); \
  _TAG_FLAG_RUNTIME(name)

#define DEFINE_RUNTIME_double(name, default_value, description) \
  DEFINE_double(name, default_value, description); \
  _TAG_FLAG_RUNTIME(name)

#define DEFINE_RUNTIME_string(name, default_value, description) \
  DEFINE_string(name, default_value, description); \
  _TAG_FLAG_RUNTIME(name)

// Non Runtime flags.
#define DEFINE_NON_RUNTIME_bool(name, default_value, description) \
  DEFINE_bool(name, default_value, description)

#define DEFINE_NON_RUNTIME_uint32(name, default_value, description) \
  DEFINE_uint32(name, default_value, description)

#define DEFINE_NON_RUNTIME_int32(name, default_value, description) \
  DEFINE_int32(name, default_value, description)

#define DEFINE_NON_RUNTIME_int64(name, default_value, description) \
  DEFINE_int64(name, default_value, description)

#define DEFINE_NON_RUNTIME_uint64(name, default_value, description) \
  DEFINE_uint64(name, default_value, description)

#define DEFINE_NON_RUNTIME_double(name, default_value, description) \
  DEFINE_double(name, default_value, description)

#define DEFINE_NON_RUNTIME_string(name, default_value, description) \
  DEFINE_string(name, default_value, description)

// Runtime preview flags.
#define DEFINE_RUNTIME_PREVIEW_bool(name, default_value, description) \
  DEFINE_RUNTIME_bool(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_RUNTIME_PREVIEW_uint32(name, default_value, description) \
  DEFINE_RUNTIME_uint32(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_RUNTIME_PREVIEW_int32(name, default_value, description) \
  DEFINE_RUNTIME_int32(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_RUNTIME_PREVIEW_int64(name, default_value, description) \
  DEFINE_RUNTIME_int64(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_RUNTIME_PREVIEW_uint64(name, default_value, description) \
  DEFINE_RUNTIME_uint64(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_RUNTIME_PREVIEW_double(name, default_value, description) \
  DEFINE_RUNTIME_double(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_RUNTIME_PREVIEW_string(name, default_value, description) \
  DEFINE_RUNTIME_string(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

// Non Runtime preview flags.
#define DEFINE_NON_RUNTIME_PREVIEW_bool(name, default_value, description) \
  DEFINE_NON_RUNTIME_bool(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_NON_RUNTIME_PREVIEW_uint32(name, default_value, description) \
  DEFINE_NON_RUNTIME_uint32(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_NON_RUNTIME_PREVIEW_int32(name, default_value, description) \
  DEFINE_NON_RUNTIME_int32(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_NON_RUNTIME_PREVIEW_int64(name, default_value, description) \
  DEFINE_NON_RUNTIME_int64(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_NON_RUNTIME_PREVIEW_uint64(name, default_value, description) \
  DEFINE_NON_RUNTIME_uint64(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_NON_RUNTIME_PREVIEW_double(name, default_value, description) \
  DEFINE_NON_RUNTIME_double(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

#define DEFINE_NON_RUNTIME_PREVIEW_string(name, default_value, description) \
  DEFINE_NON_RUNTIME_string(name, default_value, description); \
  _TAG_FLAG(name, ::yb::FlagTag::kPreview, preview)

// Unknown flags. !!Not to be used!!
// Older flags need to be reviewed in order to determine if they are runtime or non-runtime.
#define DEFINE_UNKNOWN_bool(name, default_value, description) \
  DEFINE_bool(name, default_value, description)

#define DEFINE_UNKNOWN_uint32(name, default_value, description) \
  DEFINE_uint32(name, default_value, description)

#define DEFINE_UNKNOWN_int32(name, default_value, description) \
  DEFINE_int32(name, default_value, description)

#define DEFINE_UNKNOWN_int64(name, default_value, description) \
  DEFINE_int64(name, default_value, description);

#define DEFINE_UNKNOWN_uint64(name, default_value, description) \
  DEFINE_uint64(name, default_value, description)

#define DEFINE_UNKNOWN_double(name, default_value, description) \
  DEFINE_double(name, default_value, description)

#define DEFINE_UNKNOWN_string(name, default_value, description) \
  DEFINE_string(name, default_value, description)

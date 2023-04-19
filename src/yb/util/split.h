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

#pragma once

#include <string>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "yb/util/status_fwd.h"
#include "yb/util/slice.h"

namespace yb {
namespace util {

// Split a line into arguments, where arguments are separated by space characters (i.e. isspace(c)
//  is true. '\0' is not considered a space character. Argument(s) can be quoted in single or
// double quotes like:
//
// foo bar "quoted string 1" 'quoted string 2'
//
// escaping characters is NOT SUPPORTED: like "\xff\x00otherstuff". The resulting parts will
// consist of continous parts of the input, and will not convert the escaped character sequences.
//
// The vector my_vector is populated with the parts.
//
// The function returns OK on success, even when the input string is empty, or
// Status::Corruption if the input contains unbalanced quotes or closed quotes followed by non
// space characters as in: "foo"bar or "foo'.
//
// Imported from the Redis source code. Slightly simpler implementation that does not deal with
// escaped hexadecimal values in the strings.
// TODO: If needed, consider supporting escaped hexadecimal values in the passed quoted strings.
//
// Variable names have been changed to adhere to the C++ format.
// Rest of the logic should closely follow "sdssplitargs" from src/redis/src/sds.c
Status SplitArgs(const Slice& line, std::vector<Slice>* out_vector);
Status SplitArgs(const Slice& line, boost::container::small_vector_base<Slice>* out_vector);

}  // namespace util
}  // namespace yb

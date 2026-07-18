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

#pragma once

#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/pop_front.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

namespace yb {

static constexpr const char* kNoIndent = "";
static constexpr const char* kTwoSpaceIndent = "  ";

// Dump the given expression to a stream, prefixed by the expression itself.
//
// Example:
//
//   LOG(INFO) << YB_EXPR_TO_STREAM(x);
//
// expands to
//
//   LOG(INFO) << "x= " << ::yb::ToString(x);
#define YB_EXPR_TO_STREAM(expr) BOOST_PP_STRINGIZE(expr) << "=" << (::yb::ToString(expr))

// ------------------------------------------------------------------------------------------------

#define YB_COMMA_SEPARATED_EXPR_SEQ_ELEM(r, data, expr) \
    << ", " YB_EXPR_TO_STREAM(expr)

// Outputs the given list of expressions to a stream, prefixed by their text representations,
// separated by commas. No trailing comma is produced.
//
// Example:
//
//   stream << YB_EXPR_TO_STREAM_COMMA_SEPARATED(a, b->field());
//
// expands to
//
//   stream << "a=" << ::yb::ToString(a) << ", b->field()=" << ::yb::ToString(b->field());

#define YB_EXPR_TO_STREAM_COMMA_SEPARATED(first_arg, ...) \
    YB_EXPR_TO_STREAM(first_arg) \
    BOOST_PP_SEQ_FOR_EACH(YB_COMMA_SEPARATED_EXPR_SEQ_ELEM, \
                          BOOST_PP_NIL, \
                          BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

// ------------------------------------------------------------------------------------------------

#define YB_ONE_PER_LINE_EXPR_SEQ_ELEM(r, indent, expr) \
    << "\n" << indent << YB_EXPR_TO_STREAM(expr)

// Outputs the given list of expressions to a stream, prefixed by their text representations,
// separated by newlines with provided indentation. No trailing newline is produced.
//
// Example:
//
//   stream << YB_EXPR_TO_STREAM_ONE_PER_LINE("  ", a, b->field());
//
// expands to
//
//   stream << "  " << "a=" << ::yb::ToString(a) << "\n"
//          << "  " << "b->field()=" << ::yb::ToString(b->field());

#define YB_EXPR_TO_STREAM_ONE_PER_LINE(indent, first_arg, ...) \
    (indent) << YB_EXPR_TO_STREAM(first_arg) \
    BOOST_PP_SEQ_FOR_EACH(YB_ONE_PER_LINE_EXPR_SEQ_ELEM, \
                          (indent), \
                          BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

}  // namespace yb

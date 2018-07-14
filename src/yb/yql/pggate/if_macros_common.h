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

// Common definitions of macros for YugaByte C API DSL. This file is shared

// Using include guards for individual macros, because this file can be included in the middle
// of another file, and the macros defined here can be undefined by including if_macros_undef.h.

// TODO: use a regular include guard here, and never undefine macros defined in this file.

// We include all Boost preprocessing library headers we need here to avoid including them
// separately in files that include this one.
//
// TODO: check if we really need all of these.

#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/push_back.hpp>
#include <boost/preprocessor/seq/push_front.hpp>
#include <boost/preprocessor/seq/transform.hpp>
#include <boost/preprocessor/stringize.hpp>


#ifndef YBC_ARG_DECLARATION
#define YBC_ARG_DECLARATION(s_ignored, data_ignored, arg_type_and_name) \
  BOOST_PP_TUPLE_ELEM(2, 0, arg_type_and_name) BOOST_PP_TUPLE_ELEM(2, 1, arg_type_and_name)
#endif

#ifndef YBC_DECLARE_ARGUMENTS
// Given an "argument description", which is a sequence ( http://bit.ly/2Ksk0vh ) of tuples
// ( http://bit.ly/2NlelF5 ), produces a list of arguments sutable to be used in a function
// declaration or definition.
#define YBC_DECLARE_ARGUMENTS(argument_descriptions) ( \
    BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_TRANSFORM(YBC_ARG_DECLARATION, ~, argument_descriptions)) \
  )
#endif

#ifndef YBC_JOIN_WITH_UNDERSCORE
#define YBC_JOIN_WITH_UNDERSCORE(a, b) BOOST_PP_CAT(BOOST_PP_CAT(a, _), b)
#endif

#ifndef YBC_GET_C_TYPE_NAME
// Turn a YB C API class name into the corresponding type we'll be using in C code. The C type is
// an opaque struct.
#define YBC_GET_C_TYPE_NAME(class_name) BOOST_PP_CAT(YBC, class_name)
#endif

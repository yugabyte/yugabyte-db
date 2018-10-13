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

// This generates implementations of a second level of C wrapper functions that get compiled
// on the PostgreSQL side. These functions are inline / header-only, and they convert the status
// returned by YB code to errors that PostgreSQL can understand.

#include "yb/yql/pggate/if_macros_common.h"
#include "yb/yql/pggate/if_macros_c_wrapper_common.h"

// This appears at the beginning of a YB C API class definition in the DSL.
#define YBC_CLASS_START
#define YBC_CLASS_START_INHERIT_FROM
#define YBC_CLASS_START_REF_COUNTED_THREAD_SAFE

// This appears at the end of a YB C API class definition.
#define YBC_CLASS_END

// Nothing to generate here as the ..._New function in the yb_pggate library is already in its final
// form.
#define YBC_CONSTRUCTOR(argument_descriptions)
#define YBC_CONSTRUCTOR_NO_ARGS

// Nothing to generate here as the function in the yb_pggate library is already in its final form.
#define YBC_METHOD(return_type, method_name, argument_descriptions)
#define YBC_METHOD_NO_ARGS(return_type, method_name)

// Just take argument name and ignore the type..
#define YBC_FORWARD_ARGUMENT(s_ignored, data_ignored, arg_type_and_name) \
    BOOST_PP_TUPLE_ELEM(2, 1, arg_type_and_name)

#define YBC_FORWARD_ARGUMENTS(argument_descriptions) \
    BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_TRANSFORM(YBC_FORWARD_ARGUMENT, ~, argument_descriptions))

// Generates a "second-level wrapper" that automatically reports YB statuses using ereport
// for a method returning a Status but with no other return value.
#define YBC_STATUS_METHOD(method_name, argument_descriptions) \
    inline static YBC_METHOD_WRAPPER_PROTOTYPE(void, method_name, argument_descriptions) { \
      YBCStatus _ybc_status = \
          YBC_FULL_C_FUNCTION_NAME(YBC_CURRENT_CLASS, BOOST_PP_CAT(method_name, _Status)) ( \
              _ybc_this_obj, YBC_FORWARD_ARGUMENTS(argument_descriptions)); \
      if (_ybc_status) { HandleYBStatus(_ybc_status); } \
    }

#define YBC_STATUS_METHOD_NO_ARGS(method_name) \
    inline static YBC_METHOD_WRAPPER_PROTOTYPE_NO_ARGS(void, method_name) { \
      YBCStatus _ybc_status = \
          YBC_FULL_C_FUNCTION_NAME(YBC_CURRENT_CLASS, BOOST_PP_CAT(method_name, _Status)) ( \
              _ybc_this_obj); \
      if (_ybc_status) { HandleYBStatus(_ybc_status); } \
    }

// Generates a "second-level wrapper" that automatically reports YB statuses using ereport.
// for a method returning a Result<...>.
#define YBC_RESULT_METHOD(return_type, method_name, argument_descriptions) \
    inline static YBC_METHOD_WRAPPER_PROTOTYPE(return_type, method_name, argument_descriptions) { \
      return_type _ybc_result; \
      YBCStatus _ybc_status = \
          YBC_FULL_C_FUNCTION_NAME(YBC_CURRENT_CLASS, BOOST_PP_CAT(method_name, _Status)) ( \
              _ybc_this_obj, YBC_FORWARD_ARGUMENTS(argument_descriptions), &_ybc_result); \
      if (_ybc_status) { HandleYBStatus(_ybc_status); } \
      return _ybc_result; \
    }

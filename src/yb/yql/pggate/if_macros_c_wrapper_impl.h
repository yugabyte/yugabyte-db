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

// Macros to generate C++ implementations of extern "C" functions that can be used to call
// member functions of a particular class from C code.

#include "yb/yql/pggate/if_macros_common.h"
#include "yb/yql/pggate/if_macros_c_wrapper_common.h"

// This appears at the beginning of a YB C API class definition in the DSL.
#define YBC_CLASS_START
#define YBC_CLASS_START_INHERIT_FROM
#define YBC_CLASS_START_REF_COUNTED_THREAD_SAFE

// This appears at the end of a YB C API class definition.
#define YBC_CLASS_END

// Given an argument type and name tuple, converts the C-provided argument to its corresponding
// C++ type using the mapper template.
#define YBC_CONVERT_ARG_TO_C(s_ignored, data_ignored, arg_type_and_name) \
    ::yb::pggate::impl::ArgTypeMapper<BOOST_PP_TUPLE_ELEM(2, 0, arg_type_and_name)>::ToCxx(\
        BOOST_PP_TUPLE_ELEM(2, 1, arg_type_and_name))

#define YBC_CONVERT_ARGS_TO_C(argument_descriptions) \
    BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_TRANSFORM(YBC_CONVERT_ARG_TO_C, ~, argument_descriptions))

// Generates a C wrapper function to create an instance of the class. E.g. YBCMyClass_New.
#define YBC_CONSTRUCTOR(argument_descriptions) \
    YBC_CONSTRUCTOR_WRAPPER_PROTOTYPE(argument_descriptions) { \
      return new ::yb::pggate::YBC_CURRENT_CLASS(YBC_CONVERT_ARGS_TO_C(argument_descriptions)); \
    }
#define YBC_CONSTRUCTOR_NO_ARGS \
    YBC_CONSTRUCTOR_WRAPPER_PROTOTYPE_NO_ARGS { \
      return new ::yb::pggate::YBC_CURRENT_CLASS());

#define YBC_METHOD_IMPL(return_type, c_method_name, cxx_method_name, argument_descriptions) \
    YBC_METHOD_WRAPPER_PROTOTYPE(return_type, c_method_name, argument_descriptions) { \
      return ::yb::pggate::impl::ReturnTypeMapper<return_type>::ToC( \
          _ybc_this_obj->cxx_method_name(YBC_CONVERT_ARGS_TO_C(argument_descriptions) \
          ) \
      ); \
    }

#define YBC_METHOD_IMPL_NO_ARGS(return_type, c_method_name, cxx_method_name) \
    YBC_METHOD_WRAPPER_PROTOTYPE_NO_ARGS(return_type, c_method_name) { \
      return ::yb::pggate::impl::ReturnTypeMapper<return_type>::ToC( \
          _ybc_this_obj->cxx_method_name() \
      ); \
    }

// Generates a C wrapper function to call a class member function. E.g. YBCMyClass_SomeFunction.
#define YBC_METHOD(return_type, method_name, argument_descriptions) \
    YBC_METHOD_IMPL(return_type, method_name, method_name, argument_descriptions)

// Same as YBC_METHOD but with no arguments.
#define YBC_METHOD_NO_ARGS(return_type, method_name) \
    YBC_METHOD_IMPL_NO_ARGS(return_type, method_name, method_name)

// Generates a C wrapper function that returns a YBCStatus, named with a special suffix, e.g.
// YBCMyClass_SomeFunction_Status. We then wrap it into an inline function on the PG side that
// automatically handles the status.
#define YBC_STATUS_METHOD(method_name, argument_descriptions) \
    YBC_METHOD_IMPL( \
        YBCStatus, BOOST_PP_CAT(method_name, _Status), method_name, argument_descriptions)

#define YBC_STATUS_METHOD_NO_ARGS(method_name) \
    YBC_METHOD_IMPL_NO_ARGS(YBCStatus, BOOST_PP_CAT(method_name, _Status), method_name)

// Generates a C wrapper function to call a class member function that returls a
// Result<return_type>. Checks the status, convertis it to a YBCStatus instance, and sets the
// out-parameter to the returned value in case of success. The generated function has a _Status
// name suffix, and we generate another function on the PostgreSQL side that handles the returned
// YBCStatus and automatically ereport's it.
#define YBC_RESULT_METHOD(return_type, method_name, argument_descriptions) \
    YBC_RESULT_METHOD_WRAPPER_PROTOTYPE(return_type, \
                                        BOOST_PP_CAT(method_name, _Status), \
                                        argument_descriptions) { \
      auto result = _ybc_this_obj->method_name(YBC_CONVERT_ARGS_TO_C(argument_descriptions)); \
      if (result.ok()) { \
        *_ybc_result = ::yb::pggate::impl::ReturnTypeMapper<return_type>::ToC(*result); \
        return nullptr; \
      } \
      return ::yb::ToYBCStatus(result.status()); \
    }

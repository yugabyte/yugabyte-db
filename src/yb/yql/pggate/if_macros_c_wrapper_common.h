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

// Macros used in both interface and implementation generation for YB C API functions wrapping
// calls to C++ classes.

// This is used in the end of a YB C API class definition.
#define YBC_CLASS_END

// This is a no-op in C generators.
#define YBC_VIRTUAL_DESTRUCTOR

// The C type name for the YB C API class currently being defined. We typedef this to a pointer to
// the real C++ class when compiling in C++ mode, and to a pointer to an anonymous struct when
// compiling in C mode.
#define YBC_CURRENT_CLASS_C_HANDLE_TYPE YBC_GET_C_TYPE_NAME(YBC_CURRENT_CLASS)

// Returns a function name of the form "YBC<class_name>_<method_name>".
#define YBC_FULL_C_FUNCTION_NAME(class_name, method_name) \
    BOOST_PP_CAT(YBC, YBC_JOIN_WITH_UNDERSCORE(class_name, method_name))

// Generates a C function prototype for a class constructor. E.g. this could generate
// YBCPgSelectStatement* YBCPgSelectStatement_New(const char* table_name)
// There is no trailing semicolon so we can use this in the implementation generator.
#define YBC_CONSTRUCTOR_WRAPPER_PROTOTYPE(argument_descriptions) \
    YBC_CURRENT_CLASS_C_HANDLE_TYPE YBC_FULL_C_FUNCTION_NAME(YBC_CURRENT_CLASS, New) \
        YBC_DECLARE_ARGUMENTS(argument_descriptions)
#define YBC_CONSTRUCTOR_WRAPPER_PROTOTYPE_NO_ARGS \
    YBC_CURRENT_CLASS_C_HANDLE_TYPE YBC_FULL_C_FUNCTION_NAME(YBC_CURRENT_CLASS, New)()


// Generates a C function prototype for a class member function. E.g. this could generate
// YBCStatus YBCPgSelectStatement_HasNextRow(bool* result)
// There is no trailing semicolon so we can use this in the implementation generator.
#define YBC_METHOD_WRAPPER_PROTOTYPE(return_type, method_name, argument_descriptions) \
    return_type \
    YBC_FULL_C_FUNCTION_NAME(YBC_CURRENT_CLASS, method_name) \
        YBC_DECLARE_ARGUMENTS( \
            BOOST_PP_SEQ_PUSH_FRONT(argument_descriptions, (YBC_CURRENT_CLASS_C_HANDLE_TYPE, \
            _ybc_this_obj)))

// The same with YBC_METHOD_WRAPPER_PROTOTYPE but with no arguments.
// There is no trailing semicolon so we can use this in the implementation generator.
#define YBC_METHOD_WRAPPER_PROTOTYPE_NO_ARGS(return_type, method_name) \
    return_type YBC_FULL_C_FUNCTION_NAME(YBC_CURRENT_CLASS, method_name)( \
        YBC_CURRENT_CLASS_C_HANDLE_TYPE _ybc_this_obj)

#define YBC_APPEND_RESULT_ARGUMENT(argument_descriptions, return_type) \
    BOOST_PP_SEQ_PUSH_BACK(argument_descriptions, (return_type*, _ybc_result))

// Similar to YBC_METHOD_WRAPPER_PROTOTYPE, but automatically appends a _ybc_result argument at the
// end of the argument list and makes the function return a YBCStatus.
#define YBC_RESULT_METHOD_WRAPPER_PROTOTYPE(return_type, method_name, argument_descriptions) \
    YBC_METHOD_WRAPPER_PROTOTYPE(YBCStatus, method_name, \
        YBC_APPEND_RESULT_ARGUMENT(argument_descriptions, return_type))

// These are only used in C++ declaration generators.
#define YBC_VIRTUAL
#define YBC_SUPERCLASS

#define YBC_ENTER_PGGATE_NAMESPACE
#define YBC_LEAVE_PGGATE_NAMESPACE

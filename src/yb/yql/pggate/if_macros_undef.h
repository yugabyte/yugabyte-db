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
//
// Undefine macros used for DSL-based code generation so we can define the same macros in a
// different way. We need to define these macros differently for C++ header, C header, and C++-based
// C function implementation.
//
// Refresh this list by running this command and copying-and-pasting its output here:
//
// grep -Poh '\bYBC_[A-Z_]+' ~/code/yugabyte/src/yb/yql/pggate/*.h |sort|uniq|sed 's/^/#undef /g'

#undef YBC_APPEND_RESULT_ARGUMENT
#undef YBC_ARG_DECLARATION
#undef YBC_CLASS_END
#undef YBC_CLASS_START
#undef YBC_CLASS_START_INHERIT_FROM
#undef YBC_CLASS_START_REF_COUNTED_THREAD_SAFE
#undef YBC_CONSTRUCTOR
#undef YBC_CONSTRUCTOR_NO_ARGS
#undef YBC_CONSTRUCTOR_WRAPPER_PROTOTYPE
#undef YBC_CONSTRUCTOR_WRAPPER_PROTOTYPE_NO_ARGS
#undef YBC_CONVERT_ARG_TO_C
#undef YBC_CONVERT_ARGS_TO_C
#undef YBC_CURRENT_CLASS
#undef YBC_CURRENT_CLASS_C_HANDLE_TYPE
#undef YBC_CXX_DECLARATION_MODE
#undef YBC_DECLARE_ARGUMENTS
#undef YBC_ENTER_PGGATE_NAMESPACE
#undef YBC_FORWARD_ARGUMENT
#undef YBC_FORWARD_ARGUMENTS
#undef YBC_FULL_C_FUNCTION_NAME
#undef YBC_GET_C_TYPE_NAME
#undef YBC_JOIN_WITH_UNDERSCORE
#undef YBC_LEAVE_PGGATE_NAMESPACE
#undef YBC_MAP_RETURN_TYPE_TO_CXX
#undef YBC_METHOD
#undef YBC_METHOD_IMPL
#undef YBC_METHOD_IMPL_NO_ARGS
#undef YBC_METHOD_NO_ARGS
#undef YBC_METHOD_WRAPPER_PROTOTYPE
#undef YBC_METHOD_WRAPPER_PROTOTYPE_NO_ARGS
#undef YBC_RESULT_METHOD
#undef YBC_RESULT_METHOD_WRAPPER_PROTOTYPE
#undef YBC_STATUS_METHOD
#undef YBC_STATUS_METHOD_NO_ARGS
#undef YBC_SUPERCLASS
#undef YBC_VIRTUAL
#undef YBC_VIRTUAL_DESTRUCTOR

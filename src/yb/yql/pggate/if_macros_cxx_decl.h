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

// YugaByte C API DSL macros for generating declarations of C++ classes.

#include "yb/yql/pggate/if_macros_common.h"
#include "yb/yql/pggate/type_mapping.h"

#include "yb/util/result.h"

#define YBC_CXX_DECLARATION_MODE 1

#define YBC_CLASS_START \
    class YBC_CURRENT_CLASS { \
      public:

#define YBC_CLASS_START_INHERIT_FROM(superclass) \
    class YBC_CURRENT_CLASS : public superclass { \
     public:

#define YBC_CLASS_START_REF_COUNTED_THREAD_SAFE \
    class YBC_CURRENT_CLASS : public RefCountedThreadSafe<YBC_CURRENT_CLASS> { \
      public:

#define YBC_VIRTUAL_DESTRUCTOR virtual ~YBC_CURRENT_CLASS();

#define YBC_CLASS_END };

#define YBC_CONSTRUCTOR(argument_descriptions) \
    YBC_CURRENT_CLASS YBC_DECLARE_ARGUMENTS(argument_descriptions);

#define YBC_CONSTRUCTOR_NO_ARGS YBC_CURRENT_CLASS();

#define YBC_MAP_RETURN_TYPE_TO_CXX(c_type) \
    ::yb::pggate::impl::ReturnTypeMapper<c_type>::cxx_ret_type

#define YBC_METHOD(return_type, method_name, argument_descriptions) \
    YBC_MAP_RETURN_TYPE_TO_CXX(return_type) method_name \
    YBC_DECLARE_ARGUMENTS(argument_descriptions);

#define YBC_METHOD_NO_ARGS(return_type, method_name) \
    YBC_MAP_RETURN_TYPE_TO_CXX(return_type) method_name();

#define YBC_STATUS_METHOD(method_name, argument_descriptions) \
    CHECKED_STATUS method_name YBC_DECLARE_ARGUMENTS(argument_descriptions);
#define YBC_STATUS_METHOD_NO_ARGS(method_name) \
    CHECKED_STATUS method_name();

#define YBC_RESULT_METHOD(return_type, method_name, argument_descriptions) \
    ::yb::Result<YBC_MAP_RETURN_TYPE_TO_CXX(return_type)> method_name \
    YBC_DECLARE_ARGUMENTS(argument_descriptions);

#define YBC_VIRTUAL virtual

#define YBC_ENTER_PGGATE_NAMESPACE \
    namespace yb { \
    namespace pggate {

#define YBC_LEAVE_PGGATE_NAMESPACE \
    } /* namespace pggate */ \
    } /* namespace yb */

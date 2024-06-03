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

#include "yb/rpc/network_error.h"

namespace yb {
namespace rpc {

static const std::string kNetworkErrorCategoryName = "network error";

static StatusCategoryRegisterer network_error_category_registerer(
    StatusCategoryDescription::Make<NetworkErrorTag>(&kNetworkErrorCategoryName));

} // namespace rpc
} // namespace yb

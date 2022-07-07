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

#include "yb/common/retryable_request.h"

#include "yb/util/result.h"

namespace yb {

const std::string kMinRunningRequestIdCategoryName = "min running request ID";

StatusCategoryRegisterer min_running_request_id_category_registerer(
    StatusCategoryDescription::Make<MinRunningRequestIdTag>(&kMinRunningRequestIdCategoryName));

YB_STRONGLY_TYPED_UUID_IMPL(ClientId);

}  // namespace yb

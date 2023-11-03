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

#include "yb/common/pgsql_error.h"

namespace yb {
namespace {

const std::string kPgsqlErrorCategoryName = "pgsql error";

StatusCategoryRegisterer pgsql_error_category_registerer(
    StatusCategoryDescription::Make<PgsqlErrorTag>(&kPgsqlErrorCategoryName));

const std::string kPgsqlRequestStatusCategoryName = "pgsql request status";

StatusCategoryRegisterer pgsql_request_status_category_registerer(
    StatusCategoryDescription::Make<PgsqlRequestStatusTag>(&kPgsqlRequestStatusCategoryName));

const std::string kOpIndexCategoryName = "op index";

StatusCategoryRegisterer op_index_category_registerer(
    StatusCategoryDescription::Make<OpIndexTag>(&kOpIndexCategoryName));

const std::string kRelationOidCategoryName = "relation oid";

StatusCategoryRegisterer relation_oid_category_registerer(
    StatusCategoryDescription::Make<RelationOidTag>(&kRelationOidCategoryName));

const std::string kAuxilaryMessageCategoryName = "aux msg";

StatusCategoryRegisterer auxilary_message_category_registerer(
    StatusCategoryDescription::Make<AuxilaryMessageTag>(&kAuxilaryMessageCategoryName));

static const std::string kPgsqlMessageArgsCategoryName = "pgsql msg args";

StatusCategoryRegisterer pgsql_message_args_category_registerer(
    StatusCategoryDescription::Make<PgsqlMessageArgsTag>(&kPgsqlMessageArgsCategoryName));

static const std::string kFuncNameCategoryName = "function name";

StatusCategoryRegisterer func_name_category_registerer(
    StatusCategoryDescription::Make<FuncNameTag>(&kFuncNameCategoryName));

} // namespace
} // namespace yb

//
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

#include "yb/tserver/tserver_error.h"

namespace yb::tserver {

static const std::string kTabletServerErrorCategoryName = "tablet server error";

static StatusCategoryRegisterer tablet_server_error_category_registerer(
    StatusCategoryDescription::Make<TabletServerErrorTag>(&kTabletServerErrorCategoryName));

static const std::string kTabletServerDelayCategoryName = "tablet server delay";

static StatusCategoryRegisterer tablet_server_delay_category_registerer(
    StatusCategoryDescription::Make<TabletServerDelayTag>(&kTabletServerDelayCategoryName));

void SetupError(TabletServerErrorPB* error, const Status& s) {
  auto ts_error = TabletServerError::FromStatus(s);
  auto code = ts_error ? ts_error->value() : TabletServerErrorPB::UNKNOWN_ERROR;
  if (code == TabletServerErrorPB::UNKNOWN_ERROR) {
    consensus::ConsensusError consensus_error(s);
    if (consensus_error.value() == consensus::ConsensusErrorPB::TABLET_SPLIT) {
      code = TabletServerErrorPB::TABLET_SPLIT;
    }
  }
  StatusToPB(s, error->mutable_status());
  error->set_code(code);
}

} // namespace yb::tserver

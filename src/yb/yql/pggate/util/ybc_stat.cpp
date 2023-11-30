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

#include "yb/yql/pggate/util/ybc_stat.h"
#include "yb/util/wait_state.h"

namespace yb {
namespace pggate {

const char* yb_stat_get_wait_event(uint32_t wait_event_info) {
  auto enum_value = static_cast<yb::util::WaitStateCode>(wait_event_info);
  return ToCString(enum_value);
}

}
}

extern "C" {

/* ----------
 * ybstat_get_wait_event_component() -
 *
 *	Return a string representing the current wait event component, YB is
 *	waiting on.
 */
const char *
ybcstat_get_wait_event_component(uint32_t wait_event_info) {
    uint32_t    componentId;
    const char *event_component;

    /* report process as not waiting. */
    if (wait_event_info == 0)
        return NULL;

    componentId = (wait_event_info & 0xF0000000) >> 28;

    switch (componentId) {
        case YB_PGGATE:
            event_component = "PGGate";
            break;
        case YB_TSERVER:
            event_component = "TServer";
            break;
        case YB_PERFORM:
            event_component = "Pg Perform";
            break;
        case YB_YBC:
            event_component = "Proxy/YBClient";
            break;
        case YB_PG:
            event_component = "PG";
            break;
        default:
            LOG(ERROR) << "Unknown componenet " << std::hex << componentId << " for wait_event " << std::hex << wait_event_info;
            event_component = "???";
            break;
    }

    return event_component;
}

/* ----------
 * ybstat_get_wait_event_type() -
 *
 *	Return a string representing the current wait event type, YB is
 *	waiting on.
 */
const char *
ybcstat_get_wait_event_type(uint32_t wait_event_info) {
    uint32_t    classId;
    const char *event_type;

    /* report process as not waiting. */
    if (wait_event_info == 0)
        return NULL;

    classId = wait_event_info & 0x0FF00000;

    switch (classId) {
        case YB_PG_WAIT_PERFORM:
            event_type = "PG Query";
            break;
        case YB_PG_CLIENT_SERVICE:
            event_type = "Perform API";
            break;
        case YB_COMMON:
            event_type = "Common";
            break;
        case YB_RPC:
            event_type = "RPC";
            break;
        case YB_TABLET_WAIT:
            event_type = "Tablet";
            break;
        case YB_CONSENSUS:
            event_type = "Consensus";
            break;
        case YB_CLIENT:
            event_type = "YBClient";
            break;
        case YB_ROCKSDB:
            event_type = "Rocksdb";
            break;
        case YB_FLUSH_AND_COMPACTION:
            event_type = "Flush and Compaction";
            break;
        case YB_CQL_WAIT_STATE:
            event_type = "CQL Query";
            break;
        default:
            LOG(ERROR) << "Unknown class " << std::hex << classId << " for wait_event " << std::hex << wait_event_info;
            event_type = "???";
            break;
    }

    return event_type;
}

/* ----------
 * ybstat_get_wait_event() -
 *
 *	Return a string representing the current wait event, backend is
 *	waiting on.
 */
const char *
ybcstat_get_wait_event(uint32_t wait_event_info)
{
    return yb::pggate::yb_stat_get_wait_event(wait_event_info & 0x0FFFFFFF);
}

}
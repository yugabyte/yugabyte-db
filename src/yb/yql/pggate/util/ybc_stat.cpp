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

extern "C" {
static const char *ybcstat_get_wait_perform(YBPgWaitEventPerform w);

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

    classId = wait_event_info & 0xFF000000;

    switch (classId) {
        case YB_PG_WAIT_PERFORM:
            event_type = "Perform";
            break;
        default:
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
    uint32_t classId;
    const char *event_name;

    /* report process as not waiting. */
    if (wait_event_info == 0)
        return NULL;

    classId = wait_event_info & 0xFF000000;

    switch (classId) {
        case YB_PG_WAIT_PERFORM: {
                YBPgWaitEventPerform w = (YBPgWaitEventPerform) wait_event_info;

                event_name = ybcstat_get_wait_perform(w);
                break;
        }
        default:
            event_name = "unknown wait event";
            break;
    }

    return event_name;
}

/* ----------
 * pgstat_get_wait_activity() -
 *
 * Convert WaitEventActivity to string.
 * ----------
 */
static const char *
ybcstat_get_wait_perform(YBPgWaitEventPerform w)
{
    const char *event_name = "unknown wait event";

    switch (w){
        case YB_PG_WAIT_EVENT_DML_READ:
            event_name = "DML Read";
            break;
        case YB_PG_WAIT_EVENT_DML_WRITE:
            event_name = "DML Write";
            break;
            // No default case so that the compiler warns.
    }
    return event_name;
}

}
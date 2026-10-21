/*----------
 * Canceling a statement at the request of a userspace OOM killer.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/yb_oom_cancel.h
 * ----------
 */

#ifndef YB_OOM_CANCEL_H
#define YB_OOM_CANCEL_H

#include <signal.h>

/*
 * The value a userspace OOM killer (earlyoom) passes to sigqueue() along with
 * SIGUSR1 to ask a backend to cancel its statement.  This is a protocol
 * constant shared with the OOM killer: never change it.
 */
#define YB_OOM_CANCEL_SIGVAL 0x59424f4d	/* "YBOM" */

extern PGDLLIMPORT volatile sig_atomic_t YbOomCancelPending;

extern void YbSetupOomCancelSignalHandler(void);
extern void YbProcessOomCancelInterrupt(bool doing_command_read);
extern void YbReleaseFreeMemoryAfterOomCancel(void);

#endif							/* YB_OOM_CANCEL_H */

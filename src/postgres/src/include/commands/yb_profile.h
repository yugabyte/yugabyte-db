/*------------------------------------------------------------------------------
 *
 * yb_profile.h
 *	  prototypes for yb_profile.c
 *
 * Copyright (c) Yugabyte, Inc.
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
 * src/include/commands/yb_profile.h
 *
 *------------------------------------------------------------------------------
 */

#ifndef YB_PROFILE_H
#define YB_PROFILE_H

#include "libpq/hba.h"
#include "nodes/parsenodes.h"

extern bool IsProfileHandlingRequired(UserAuth auth_method);

extern Oid	YbCreateProfile(YbCreateProfileStmt* stmt);
extern void YbDropProfile(YbDropProfileStmt* stmt);

extern Oid	yb_get_profile_oid(const char* prfname, bool missing_ok);
extern char* yb_get_profile_name(Oid prfid);

extern void YbCreateRoleProfile(Oid roleid, const char* rolename,
								const char* profile);
extern void YbSetRoleProfileStatus(Oid roleid, const char* rolename,
								   char status);
extern bool YbMaybeIncFailedAttemptsAndDisableProfile(Oid roleid);
extern void YbResetFailedAttemptsIfAllowed(Oid roleid);

extern HeapTuple yb_get_role_profile_tuple_by_role_oid(Oid roleid);
extern HeapTuple yb_get_role_profile_tuple_by_oid(Oid rolprfid);

extern void YbRemoveRoleProfileForRoleIfExists(Oid roleid);

#endif /* YB_PROFILE_H */

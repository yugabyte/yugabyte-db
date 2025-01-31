/*-----------------------------------------------------------------------------
 *
 * tablegroup.h
 *	  Commands to manipulate table groups
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
 * src/include/commands/tablegroup.h
 *
 *-----------------------------------------------------------------------------
 */

#ifndef TABLEGROUP_H
#define TABLEGROUP_H

#include "catalog/objectaddress.h"
#include "lib/stringinfo.h"
#include "nodes/parsenodes.h"

#define DEFAULT_TABLEGROUP_NAME	"default"

extern Oid	CreateTableGroup(YbCreateTableGroupStmt *stmt);

extern Oid	get_tablegroup_oid(const char *tablegroupname, bool missing_ok);
extern char *get_tablegroup_name(Oid grp_oid);

extern void RemoveTablegroupById(Oid grp_oid, bool remove_implicit);
extern char *get_implicit_tablegroup_name(Oid oidSuffix);
extern char *get_restore_tablegroup_name(Oid oidSuffix);
extern ObjectAddress RenameTablegroup(const char *oldname, const char *newname);
extern ObjectAddress AlterTablegroupOwner(const char *grpname, Oid newOwnerId);
extern void ybAlterTablespaceForTablegroup(const char *grpname,
										   Oid newTablespace,
										   const char *newname);

#endif							/* TABLEGROUP_H */

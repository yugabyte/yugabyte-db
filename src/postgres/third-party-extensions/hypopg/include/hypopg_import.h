/*-------------------------------------------------------------------------
 *
 * hypopg_import.h: Import of some PostgreSQL private fuctions.
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 * Copyright (c) 2008-2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#ifndef _HYPOPG_IMPORT_H_
#define _HYPOPG_IMPORT_H_

#include "commands/vacuum.h"
#include "lib/stringinfo.h"
#include "nodes/pg_list.h"
#include "optimizer/planner.h"
#include "utils/rel.h"
#include "include/hypopg_import_index.h"

extern void get_opclass_name(Oid opclass, Oid actual_datatype, StringInfo buf);

#endif		/* _HYPOPG_IMPORT_H_ */

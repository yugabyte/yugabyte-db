/*-------------------------------------------------------------------------
 *
 * hypopg_import_index.h: Import of some PostgreSQL private fuctions, used for
 *                        hypothetical index.
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 * Copyright (c) 2008-2018, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#ifndef _HYPOPG_IMPORT_INDEX_H_
#define _HYPOPG_IMPORT_INDEX_H_

/* adapted from nbtinsert.h */
#define HYPO_BTMaxItemSize \
	MAXALIGN_DOWN((BLCKSZ - \
				MAXALIGN(SizeOfPageHeaderData + 3*sizeof(ItemIdData)) - \
				MAXALIGN(sizeof(BTPageOpaqueData))) / 3)

extern List *build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation);
#if PG_VERSION_NUM < 100000
extern Oid GetIndexOpClass(List *opclass, Oid attrType,
				char *accessMethodName, Oid accessMethodId);
#endif

extern void CheckPredicate(Expr *predicate);
extern bool CheckMutability(Expr *expr);
#if PG_VERSION_NUM < 90500
extern char *get_am_name(Oid amOid);
#endif

#endif		/* _HYPOPG_IMPORT_INDEX_H_ */

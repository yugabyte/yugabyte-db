/*-------------------------------------------------------------------------
 *
 * hypopg_import.h: Import of some PostgreSQL private fuctions.
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 * Copyright (c) 2008-2018, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */


/* adapted from nbtinsert.h */
#define HYPO_BTMaxItemSize \
	MAXALIGN_DOWN((BLCKSZ - \
				MAXALIGN(SizeOfPageHeaderData + 3*sizeof(ItemIdData)) - \
				MAXALIGN(sizeof(BTPageOpaqueData))) / 3)

extern List *build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation);
extern Oid GetIndexOpClass(List *opclass, Oid attrType,
				char *accessMethodName, Oid accessMethodId);

extern void CheckPredicate(Expr *predicate);
extern bool CheckMutability(Expr *expr);
#if PG_VERSION_NUM < 90500
extern char *get_am_name(Oid amOid);
#endif
extern void get_opclass_name(Oid opclass, Oid actual_datatype, StringInfo buf);

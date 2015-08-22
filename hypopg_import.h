/*-------------------------------------------------------------------------
 *
 * hypopg_import.h: Import of some PostgreSQL private fuctions.
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */


extern List *build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation);
extern Oid GetIndexOpClass(List *opclass, Oid attrType,
				char *accessMethodName, Oid accessMethodId);

extern void CheckPredicate(Expr *predicate);
extern bool CheckMutability(Expr *expr);

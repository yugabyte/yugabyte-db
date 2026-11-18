/*--------------------------------------------------------------------------------------------------
 *
 * yb_osc_util.c
 *    Helpers shared by the online schema change feature files.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_osc_util.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/table.h"
#include "catalog/index.h"
#include "catalog/pg_index.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/partcache.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "yb_osc_internal.h"


/*
 * Return rel's quoted, schema-qualified name for generated SQL, which must not
 * depend on search_path.
 */
char *
YbOscQuoteQualifiedRelName(Relation rel)
{
	const char *nspname = get_namespace_name(RelationGetNamespace(rel));
	const char *relname = RelationGetRelationName(rel);

	return quote_qualified_identifier(nspname, relname);
}

/*
 * Return the attnums of rel's primary key columns, in index order.  Errors if
 * rel has no primary key.
 */
List *
YbOscGetPrimaryKeyAttnums(Relation rel)
{
	Oid			pkeyIndexOid = RelationGetPrimaryKeyIndex(rel, false /* deferrable_ok */ );

	if (!OidIsValid(pkeyIndexOid))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("table \"%s\" must have a primary key",
						RelationGetRelationName(rel))));

	Relation	idxRel = index_open(pkeyIndexOid, AccessShareLock);
	Form_pg_index idxForm = idxRel->rd_index;
	int			nkeys = idxForm->indnkeyatts;
	List	   *attnums = NIL;

	for (int i = 0; i < nkeys; ++i)
	{
		AttrNumber	attnum = idxForm->indkey.values[i];

		if (attnum <= 0)
			elog(ERROR, "unexpected expression in primary key of \"%s\"",
				 RelationGetRelationName(rel));
		attnums = lappend_int(attnums, attnum);
	}

	index_close(idxRel, AccessShareLock);
	return attnums;
}

/*
 * Format `attnums` of `rel` as a comma-separated column list, for an error
 * message (quoted = false) or for pasting into SQL text (quoted = true).
 */
static char *
FormatAttnumList(Relation rel, List *attnums, bool quoted)
{
	TupleDesc	tupdesc = RelationGetDescr(rel);
	StringInfoData buf;
	ListCell   *lc;
	bool		first = true;

	initStringInfo(&buf);
	foreach(lc, attnums)
	{
		const char *name = NameStr(TupleDescAttr(tupdesc, lfirst_int(lc) - 1)->attname);

		if (!first)
			appendStringInfoString(&buf, ", ");
		appendStringInfoString(&buf, quoted ? quote_identifier(name) : name);
		first = false;
	}
	return buf.data;
}

/*
 * Whether a key column has the same type, length and collation on both tables.
 * A narrowing cast (bigint to int, varchar(64) to varchar(8)) can map distinct
 * source values to one target value, and a different collation can make the
 * target treat distinct source keys as equal.
 */
static bool
SameKeyColumnType(Form_pg_attribute srcAttr, Form_pg_attribute dstAttr)
{
	return srcAttr->atttypid == dstAttr->atttypid &&
		srcAttr->atttypmod == dstAttr->atttypmod &&
		srcAttr->attcollation == dstAttr->attcollation;
}

/*
 * Whether a unique index constrains every row of its table, immediately and by
 * plain columns, so that it can be a key witness.  Otherwise *reason says why.
 *
 * The checks are those of transformFkeyCheckAttrs, which finds an index
 * proving that referenced columns identify at most one row.
 */
static bool
WitnessIndexUsable(HeapTuple idxTup, const char *idxName, char **reason)
{
	Form_pg_index idx = (Form_pg_index) GETSTRUCT(idxTup);

	/* An index left behind by a failed build guarantees nothing. */
	if (!idx->indisvalid || !idx->indisready)
	{
		*reason = psprintf("unique index \"%s\" is not valid", idxName);
		return false;
	}

	/*
	 * A DEFERRABLE constraint is checked at commit, after the change-capture
	 * trigger has run.  YB does not support DEFERRABLE unique constraints yet;
	 * this keeps the rule correct if it ever does.
	 */
	if (!idx->indimmediate)
	{
		*reason = psprintf("unique index \"%s\" is DEFERRABLE", idxName);
		return false;
	}

	/* A partial index only constrains the rows its predicate selects. */
	if (!heap_attisnull(idxTup, Anum_pg_index_indpred, NULL))
	{
		*reason = psprintf("unique index \"%s\" is partial", idxName);
		return false;
	}

	/* Expression keys have no column name to pair with a column of the other table. */
	if (!heap_attisnull(idxTup, Anum_pg_index_indexprs, NULL))
	{
		*reason = psprintf("unique index \"%s\" is on an expression", idxName);
		return false;
	}

	return true;
}

/*
 * Return true if a source index can be the job's key witness, with its key
 * columns in *witness.  Otherwise *reason explains why if the index is unique,
 * and is NULL if not, so that non-unique indexes do not clutter the error.
 *
 * The key columns need only be a subset of the target's primary key, since
 * uniqueness on a subset implies uniqueness on any superset.
 */
static bool
KeyWitnessCandidateOk(Relation srcRel, Relation dstRel, List *dstPk,
					  HeapTuple idxTup, List **witness, char **reason)
{
	Form_pg_index idx = (Form_pg_index) GETSTRUCT(idxTup);
	TupleDesc	srcDesc = RelationGetDescr(srcRel);
	TupleDesc	dstDesc = RelationGetDescr(dstRel);
	char	   *idxName;

	*witness = NIL;
	*reason = NULL;

	if (!idx->indisunique)
		return false;

	idxName = get_rel_name(idx->indexrelid);
	if (idxName == NULL)
		return false;			/* concurrently dropped */

	if (!WitnessIndexUsable(idxTup, idxName, reason))
		return false;

	/* INCLUDE columns are not covered by the uniqueness. */
	for (int i = 0; i < idx->indnkeyatts; i++)
	{
		AttrNumber	attnum = idx->indkey.values[i];
		Form_pg_attribute srcAttr;
		const char *attname;
		bool		matched = false;
		ListCell   *lc;

		srcAttr = TupleDescAttr(srcDesc, attnum - 1);
		attname = NameStr(srcAttr->attname);

		/*
		 * UNIQUE allows any number of NULLs, so a nullable column does not
		 * separate rows.  Such a row would fail the target's NOT NULL primary
		 * key, but only hours into the backfill or inside application DML, so
		 * reject the index up front.
		 */
		if (!srcAttr->attnotnull)
		{
			*reason = psprintf("column \"%s\" of unique index \"%s\" is nullable",
							   attname, idxName);
			return false;
		}

		foreach(lc, dstPk)
		{
			Form_pg_attribute dstAttr = TupleDescAttr(dstDesc, lfirst_int(lc) - 1);

			if (strcmp(attname, NameStr(dstAttr->attname)) != 0)
				continue;
			matched = true;

			if (!SameKeyColumnType(srcAttr, dstAttr))
			{
				*reason = psprintf("column \"%s\" of unique index \"%s\" does not have the "
								   "same type, length or collation as the target's \"%s\"",
								   attname, idxName, attname);
				return false;
			}
			break;
		}

		if (!matched)
		{
			*reason = psprintf("column \"%s\" of unique index \"%s\" is not part of the "
							   "target's primary key", attname, idxName);
			return false;
		}

		*witness = lappend_int(*witness, attnum);
	}

	return true;
}

/*
 * Return the target attnums of the source primary key columns that the
 * target's primary key leaves out, which a target witness must cover.  If some
 * source primary key column has no target column of the same name, type,
 * length and collation, no target index can be a witness: set *reason and
 * return NIL.  *reason is NULL otherwise.
 */
static List *
SourceKeyOnTarget(Relation srcRel, Relation dstRel, List *srcPk, List *dstPk,
				  char **reason)
{
	TupleDesc	srcDesc = RelationGetDescr(srcRel);
	TupleDesc	dstDesc = RelationGetDescr(dstRel);
	List	   *needed = NIL;
	ListCell   *lc;

	*reason = NULL;

	foreach(lc, srcPk)
	{
		Form_pg_attribute srcAttr = TupleDescAttr(srcDesc, lfirst_int(lc) - 1);
		const char *attname = NameStr(srcAttr->attname);
		AttrNumber	dstAttnum = get_attnum(RelationGetRelid(dstRel), attname);

		if (dstAttnum == InvalidAttrNumber)
		{
			*reason = psprintf("source primary key column \"%s\" is not a column of the target",
							   attname);
			return NIL;
		}
		if (!SameKeyColumnType(srcAttr, TupleDescAttr(dstDesc, dstAttnum - 1)))
		{
			*reason = psprintf("source primary key column \"%s\" does not have the same type, "
							   "length or collation on the target", attname);
			return NIL;
		}
		if (!list_member_int(dstPk, dstAttnum))
			needed = lappend_int(needed, dstAttnum);
	}

	return needed;
}

/*
 * Return true if a target index can be the job's key witness: a unique index
 * keyed on target primary key columns only, whose key and INCLUDE columns
 * cover dstNeeded (see SourceKeyOnTarget).  Otherwise *reason explains why if
 * the index is unique.
 *
 * Two source rows that reach one target key differ in some source primary key
 * column, and the target's primary key cannot hold it, so it is in dstNeeded:
 * their entries in this index share a key but not a value.  The copy writes the
 * index with the duplicate check of online CREATE INDEX, which compares the
 * values as well and fails the write (HasDuplicateUniqueIndexValue), and the
 * trigger's insert fails on the target's primary key.
 *
 * Unlike a source witness, this works when the source is partitioned on a
 * column that the target's primary key leaves out: a unique index on a
 * partitioned table must include its partition key, which the target's
 * primary key always does.
 */
static bool
TargetKeyWitnessCandidateOk(Relation dstRel, List *dstPk, List *dstNeeded,
							HeapTuple idxTup, char **reason)
{
	Form_pg_index idx = (Form_pg_index) GETSTRUCT(idxTup);
	TupleDesc	dstDesc = RelationGetDescr(dstRel);
	char	   *idxName;
	ListCell   *lc;

	*reason = NULL;

	/* The copy writes the primary key's row blindly, with no such check. */
	if (!idx->indisunique || idx->indisprimary)
		return false;

	idxName = get_rel_name(idx->indexrelid);
	if (idxName == NULL)
		return false;			/* concurrently dropped */

	if (!WitnessIndexUsable(idxTup, idxName, reason))
		return false;

	/* Rows that share a target primary key must share an index key. */
	for (int i = 0; i < idx->indnkeyatts; i++)
	{
		AttrNumber	attnum = idx->indkey.values[i];

		if (!list_member_int(dstPk, attnum))
		{
			*reason = psprintf("column \"%s\" of unique index \"%s\" is not part of the "
							   "target's primary key",
							   NameStr(TupleDescAttr(dstDesc, attnum - 1)->attname),
							   idxName);
			return false;
		}
	}

	foreach(lc, dstNeeded)
	{
		AttrNumber	attnum = lfirst_int(lc);
		bool		covered = false;

		for (int i = 0; i < idx->indnatts; i++)
		{
			if (idx->indkey.values[i] == attnum)
			{
				covered = true;
				break;
			}
		}

		if (!covered)
		{
			*reason = psprintf("unique index \"%s\" does not include source primary key "
							   "column \"%s\"",
							   idxName, NameStr(TupleDescAttr(dstDesc, attnum - 1)->attname));
			return false;
		}
	}

	return true;
}

/*
 * Whether rel could have a unique index on attnums: a unique index on a
 * partitioned table must include the partition key.
 */
static bool
UniqueIndexAllowed(Relation rel, List *attnums)
{
	PartitionKey key;

	if (rel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
		return true;

	key = RelationGetPartitionKey(rel);
	for (int i = 0; i < get_partition_natts(key); i++)
	{
		AttrNumber	attnum = get_partition_col_attnum(key, i);

		if (attnum == 0 || !list_member_int(attnums, attnum))
			return false;
	}
	return true;
}

/*
 * Complain that no index witnesses key uniqueness, saying what disqualified
 * each unique index there is and spelling out the indexes that would fix it.
 * dstNeeded is from SourceKeyOnTarget, and onTarget says whether it succeeded.
 */
pg_noreturn static void
NoKeyWitness(Relation srcRel, Relation dstRel, List *dstPk, List *dstNeeded,
			 bool onTarget, List *reasons)
{
	TupleDesc	dstDesc = RelationGetDescr(dstRel);
	StringInfoData detail;
	List	   *onSrc = NIL;
	char	   *srcFix = NULL;
	char	   *dstFix = NULL;
	ListCell   *lc;

	/* List every rejected candidate; we cannot tell which one was meant. */
	initStringInfo(&detail);
	foreach(lc, reasons)
	{
		if (detail.len > 0)
			appendStringInfoChar(&detail, '\n');
		appendStringInfoString(&detail, (char *) lfirst(lc));
	}
	if (detail.len == 0)
		appendStringInfoString(&detail,
							   "The source has no unique index whose key columns are all "
							   "named in that primary key.");

	/*
	 * A source witness can only use target primary key columns that the
	 * source also has.
	 */
	foreach(lc, dstPk)
	{
		const char *name = NameStr(TupleDescAttr(dstDesc, lfirst_int(lc) - 1)->attname);
		AttrNumber	srcAttnum = get_attnum(RelationGetRelid(srcRel), name);

		if (srcAttnum != InvalidAttrNumber)
			onSrc = lappend_int(onSrc, srcAttnum);
	}

	if (onSrc != NIL && UniqueIndexAllowed(srcRel, onSrc))
		srcFix = psprintf("CREATE UNIQUE INDEX ON %s (%s)",
						  YbOscQuoteQualifiedRelName(srcRel),
						  FormatAttnumList(srcRel, onSrc, true));

	if (onTarget)
		dstFix = psprintf("CREATE UNIQUE INDEX ON %s (%s)%s%s%s",
						  YbOscQuoteQualifiedRelName(dstRel),
						  FormatAttnumList(dstRel, dstPk, true),
						  dstNeeded != NIL ? " INCLUDE (" : "",
						  dstNeeded != NIL ? FormatAttnumList(dstRel, dstNeeded, true) : "",
						  dstNeeded != NIL ? ")" : "");

	if (srcFix != NULL && dstFix != NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("no unique index on source table \"%s\" or target table \"%s\" witnesses the target's primary key",
						RelationGetRelationName(srcRel),
						RelationGetRelationName(dstRel)),
				 errdetail("%s", detail.data),
				 errhint("Target primary key is (%s).  Add a witness on the source with: %s; "
						 "its columns must be NOT NULL and must have the same type, length and "
						 "collation on both tables.  Or add one on the target with: %s.",
						 FormatAttnumList(dstRel, dstPk, false), srcFix, dstFix)));

	if (srcFix != NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("no unique index on source table \"%s\" or target table \"%s\" witnesses the target's primary key",
						RelationGetRelationName(srcRel),
						RelationGetRelationName(dstRel)),
				 errdetail("%s", detail.data),
				 errhint("Target primary key is (%s).  Add a witness with: %s; its columns must "
						 "be NOT NULL and must have the same type, length and collation on both "
						 "tables.",
						 FormatAttnumList(dstRel, dstPk, false), srcFix)));

	if (dstFix != NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("no unique index on source table \"%s\" or target table \"%s\" witnesses the target's primary key",
						RelationGetRelationName(srcRel),
						RelationGetRelationName(dstRel)),
				 errdetail("%s", detail.data),
				 errhint("Target primary key is (%s).  Add a witness on the target with: %s.",
						 FormatAttnumList(dstRel, dstPk, false), dstFix)));

	ereport(ERROR,
			(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
			 errmsg("no unique index on source table \"%s\" or target table \"%s\" witnesses the target's primary key",
					RelationGetRelationName(srcRel),
					RelationGetRelationName(dstRel)),
			 errdetail("%s", detail.data),
			 errhint("%s, and the target does not have every source primary key column, so no "
					 "index can witness target primary key (%s).",
					 onSrc == NIL ?
					 "The target primary key names no column of the source" :
					 "A unique index on the partitioned source must include its partition key",
					 FormatAttnumList(dstRel, dstPk, false))));
}

/*
 * Return the source columns that the transform must copy unchanged into
 * same-named target columns for a unique index to prove that distinct source
 * rows reach distinct target primary keys.  Errors if no index proves it.
 *
 * Without the proof, two source rows with the same target key would silently
 * collapse into one row, since the copy's base-table write is blind, and
 * deleting either source row would delete the target row of the other.
 *
 * Preferably the witness is a unique index on the source whose key columns are
 * a subset of the target's primary key columns, matched by name, and those key
 * columns are returned; the source's primary key is tried first.  Failing
 * that, it is a unique index on the target that covers the source's primary
 * key (see TargetKeyWitnessCandidateOk), and the source's primary key columns
 * are returned.  Either proof holds only while the transform passes those
 * columns through unchanged, which begin checks in the transform's body.
 */
List *
YbOscGetKeyWitnessAttnums(Relation srcRel, Relation dstRel)
{
	List	   *dstPk = YbOscGetPrimaryKeyAttnums(dstRel);
	List	   *srcPk;
	Oid			srcPkIdx;
	List	   *candidates;
	List	   *dstNeeded;
	List	   *reasons = NIL;
	char	   *why;
	ListCell   *lc;

	/*
	 * Require a source primary key: without one, rows are identified by a
	 * ybrowid that the target cannot reproduce.
	 */
	srcPk = YbOscGetPrimaryKeyAttnums(srcRel);
	srcPkIdx = RelationGetPrimaryKeyIndex(srcRel, false /* deferrable_ok */ );

	candidates = list_make1_oid(srcPkIdx);
	foreach(lc, RelationGetIndexList(srcRel))
	{
		if (lfirst_oid(lc) != srcPkIdx)
			candidates = lappend_oid(candidates, lfirst_oid(lc));
	}

	foreach(lc, candidates)
	{
		HeapTuple	idxTup = SearchSysCache1(INDEXRELID,
											  ObjectIdGetDatum(lfirst_oid(lc)));
		List	   *witness;
		bool		ok;

		if (!HeapTupleIsValid(idxTup))
			continue;			/* concurrently dropped */

		ok = KeyWitnessCandidateOk(srcRel, dstRel, dstPk, idxTup, &witness, &why);
		ReleaseSysCache(idxTup);

		if (ok)
			return witness;
		if (why != NULL)
			reasons = lappend(reasons, why);
	}

	dstNeeded = SourceKeyOnTarget(srcRel, dstRel, srcPk, dstPk, &why);
	if (why != NULL)
	{
		reasons = lappend(reasons, why);
		NoKeyWitness(srcRel, dstRel, dstPk, NIL, false /* onTarget */ , reasons);
	}

	foreach(lc, RelationGetIndexList(dstRel))
	{
		HeapTuple	idxTup = SearchSysCache1(INDEXRELID,
											  ObjectIdGetDatum(lfirst_oid(lc)));
		bool		ok;

		if (!HeapTupleIsValid(idxTup))
			continue;			/* concurrently dropped */

		ok = TargetKeyWitnessCandidateOk(dstRel, dstPk, dstNeeded, idxTup, &why);
		ReleaseSysCache(idxTup);

		if (ok)
			return srcPk;
		if (why != NULL)
			reasons = lappend(reasons, why);
	}

	NoKeyWitness(srcRel, dstRel, dstPk, dstNeeded, true /* onTarget */ , reasons);
}

/*
 * Return the quoted, comma-separated names of rel's live columns, for the
 * column list of an INSERT into rel.  Naming the columns keeps the INSERT
 * correct when rel has dropped columns.
 */
char *
YbOscBuildColumnList(Relation rel)
{
	TupleDesc	tupdesc = RelationGetDescr(rel);
	StringInfoData buf;
	bool		first = true;

	initStringInfo(&buf);
	for (int i = 0; i < tupdesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, i);

		if (attr->attisdropped)
			continue;
		if (!first)
			appendStringInfoString(&buf, ", ");
		appendStringInfoString(&buf, quote_identifier(NameStr(attr->attname)));
		first = false;
	}
	return buf.data;
}

/*
 * Rename rel by running ALTER TABLE ... RENAME TO through SPI.  The caller
 * must already hold a lock that covers the rename.
 */
void
YbOscRenameTable(Relation rel, const char *newName)
{
	char	   *relQual = YbOscQuoteQualifiedRelName(rel);
	const char *newIdent = quote_identifier(newName);

	StringInfoData buf;

	initStringInfo(&buf);
	appendStringInfo(&buf, "ALTER TABLE %s RENAME TO %s", relQual, newIdent);

	if (SPI_connect() != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("SPI_connect failed")));

	int			rc = SPI_execute(buf.data, false, 0);

	if (rc != SPI_OK_UTILITY)
		ereport(ERROR, (errmsg("failed to rename table")));

	SPI_finish();
}

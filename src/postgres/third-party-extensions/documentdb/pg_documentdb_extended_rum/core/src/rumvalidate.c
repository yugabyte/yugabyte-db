/*-------------------------------------------------------------------------
 *
 * rumvalidate.c
 *	  Opclass validator for RUM.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/amvalidate.h"
#include "access/htup_details.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_amproc.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#if PG_VERSION_NUM >= 100000
#include "utils/regproc.h"
#endif
#include "utils/syscache.h"

#include "pg_documentdb_rum.h"

/*
 * Validator for a RUM opclass.
 */
bool
rumvalidate(Oid opclassoid)
{
	bool result = true;
	HeapTuple classtup;
	Form_pg_opclass classform;
	Oid opfamilyoid;
	Oid opcintype;
	Oid opcintype_overload;             /* used for timestamptz */
	Oid opckeytype;
	Oid opckeytype_overload;            /* used for timestamptz */
	char *opclassname;
	HeapTuple familytup;
	Form_pg_opfamily familyform;
	char *opfamilyname;
	CatCList *proclist,
			 *oprlist;
	List *grouplist;
	OpFamilyOpFuncGroup *opclassgroup;
	int i;
	ListCell *lc;
	bool hasRawOrderingProc = false;

	/* Fetch opclass information */
	classtup = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclassoid));
	if (!HeapTupleIsValid(classtup))
	{
		elog(ERROR, "cache lookup failed for operator class %u", opclassoid);
	}
	classform = (Form_pg_opclass) GETSTRUCT(classtup);

	opfamilyoid = classform->opcfamily;
	opcintype = opcintype_overload = classform->opcintype;
	opckeytype = opckeytype_overload = classform->opckeytype;
	if (!OidIsValid(opckeytype))
	{
		opckeytype = opckeytype_overload = opcintype;
	}
	opclassname = NameStr(classform->opcname);

	/* Fix type Oid for timestamptz */
	if (opcintype == TIMESTAMPTZOID)
	{
		opcintype_overload = TIMESTAMPOID;
	}
	if (opckeytype == TIMESTAMPTZOID)
	{
		opckeytype_overload = TIMESTAMPOID;
	}

	/* Fetch opfamily information */
	familytup = SearchSysCache1(OPFAMILYOID, ObjectIdGetDatum(opfamilyoid));
	if (!HeapTupleIsValid(familytup))
	{
		elog(ERROR, "cache lookup failed for operator family %u", opfamilyoid);
	}
	familyform = (Form_pg_opfamily) GETSTRUCT(familytup);

	opfamilyname = NameStr(familyform->opfname);

	/* Fetch all operators and support functions of the opfamily */
	oprlist = SearchSysCacheList1(AMOPSTRATEGY, ObjectIdGetDatum(opfamilyoid));
	proclist = SearchSysCacheList1(AMPROCNUM, ObjectIdGetDatum(opfamilyoid));

	/* Check individual support functions */
	for (i = 0; i < proclist->n_members; i++)
	{
		HeapTuple proctup = &proclist->members[i]->tuple;
		Form_pg_amproc procform = (Form_pg_amproc) GETSTRUCT(proctup);
		bool ok;

		/*
		 * All RUM support functions should be registered with matching
		 * left/right types
		 */
		if (procform->amproclefttype != procform->amprocrighttype)
		{
			ereport(INFO,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg(
						 "rum opfamily %s contains support procedure %s with cross-type registration",
						 opfamilyname,
						 format_procedure(procform->amproc))));
			result = false;
		}

		/*
		 * We can't check signatures except within the specific opclass, since
		 * we need to know the associated opckeytype in many cases.
		 */
		if (procform->amproclefttype != opcintype)
		{
			continue;
		}

		/* Verify procedure numbers and review function signatures */
		switch (procform->amprocnum)
		{
			case GIN_COMPARE_PROC:
			{
				ok = check_amproc_signature(procform->amproc, INT4OID, false,
											2, 2, opckeytype, opckeytype);
				break;
			}

			case GIN_EXTRACTVALUE_PROC:
			{
				/* Some opclasses omit nullFlags */
				ok = check_amproc_signature(procform->amproc, INTERNALOID, false,
											2, 5, opcintype_overload, INTERNALOID,
											INTERNALOID, INTERNALOID,
											INTERNALOID);
				break;
			}

			case GIN_EXTRACTQUERY_PROC:
			{
				/* Some opclasses omit nullFlags and searchMode */
				if (opcintype == TSVECTOROID)
				{
					ok = check_amproc_signature(procform->amproc, INTERNALOID, false,
												5, 7, TSQUERYOID, INTERNALOID,
												INT2OID, INTERNALOID, INTERNALOID,
												INTERNALOID, INTERNALOID);
				}
				else if (opcintype == TSQUERYOID)
				{
					ok = check_amproc_signature(procform->amproc, INTERNALOID, false,
												5, 7, TSVECTOROID, INTERNALOID,
												INT2OID, INTERNALOID, INTERNALOID,
												INTERNALOID, INTERNALOID);
				}
				else
				{
					ok = check_amproc_signature(procform->amproc, INTERNALOID, false,
												5, 7, opcintype_overload, INTERNALOID,
												INT2OID, INTERNALOID, INTERNALOID,
												INTERNALOID, INTERNALOID);
				}
				break;
			}

			case GIN_CONSISTENT_PROC:
			{
				/* Some opclasses omit queryKeys and nullFlags */
				if (opcintype == TSQUERYOID)
				{
					ok = check_amproc_signature(procform->amproc, BOOLOID, false,
												6, 8, INTERNALOID, INT2OID,
												TSVECTOROID, INT4OID,
												INTERNALOID, INTERNALOID,
												INTERNALOID, INTERNALOID);
				}
				else if (opcintype == TSVECTOROID ||
						 opcintype == TIMESTAMPOID ||
						 opcintype == TIMESTAMPTZOID ||
						 opcintype == ANYARRAYOID)
				{
					ok = check_amproc_signature(procform->amproc, BOOLOID, false,
												6, 8, INTERNALOID, INT2OID,
												opcintype_overload, INT4OID,
												INTERNALOID, INTERNALOID,
												INTERNALOID, INTERNALOID);
				}
				else
				{
					ok = check_amproc_signature(procform->amproc, BOOLOID, false,
												6, 8, INTERNALOID, INT2OID,
												INTERNALOID, INT4OID,
												INTERNALOID, INTERNALOID,
												INTERNALOID, INTERNALOID);
				}
				break;
			}

			case GIN_COMPARE_PARTIAL_PROC:
			{
				ok = check_amproc_signature(procform->amproc, INT4OID, false,
											4, 4,
											opckeytype_overload, opckeytype_overload,
											INT2OID, INTERNALOID);
				break;
			}

			case RUM_CONFIG_PROC:
			{
				ok = check_amproc_signature(procform->amproc, VOIDOID, false,
											1, 1, INTERNALOID);
				break;
			}

			case RUM_PRE_CONSISTENT_PROC:
			{
				ok = check_amproc_signature(procform->amproc, BOOLOID, false,
											8, 8, INTERNALOID, INT2OID,
											opcintype, INT4OID,
											INTERNALOID, INTERNALOID,
											INTERNALOID, INTERNALOID);
				break;
			}

			case RUM_ORDERING_PROC:
			{
				/* Two possible signatures */
				if (opcintype == TSVECTOROID ||
					opcintype == ANYARRAYOID)
				{
					ok = check_amproc_signature(procform->amproc, FLOAT8OID, false,
												9, 9, INTERNALOID, INT2OID,
												opcintype, INT4OID,
												INTERNALOID, INTERNALOID,
												INTERNALOID, INTERNALOID,
												INTERNALOID, INTERNALOID);
				}
				else
				{
					ok = check_amproc_signature(procform->amproc, opcintype, false,
												4, 4,
												opckeytype, opcintype, INT2OID,
												INTERNALOID);
					if (!ok)
					{
						ok = check_amproc_signature(procform->amproc, FLOAT8OID, false,
													3, 3,
													opcintype, opcintype, INT2OID);
					}
					else
					{
						hasRawOrderingProc = true;
					}
				}
				break;
			}

			case RUM_OUTER_ORDERING_PROC:
			{
				ok = check_amproc_signature(procform->amproc, FLOAT8OID, false,
											3, 3,
											opcintype_overload, opcintype_overload,
											INT2OID);

				if (!ok)
				{
					ok = check_amproc_signature(procform->amproc, opckeytype, false,
												4, 4,
												opckeytype, opckeytype, INT2OID,
												INTERNALOID);
				}
				break;
			}

			case RUM_ADDINFO_JOIN:
			{
				ok = check_amproc_signature(procform->amproc, BYTEAOID, false,
											2, 2, INTERNALOID, INTERNALOID);
				break;
			}

			default:
				ereport(INFO,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg(
							 "rum opfamily %s contains function %s with invalid support number %d",
							 opfamilyname,
							 format_procedure(procform->amproc),
							 procform->amprocnum)));
				result = false;
				continue;       /* don't want additional message */
		}

		if (!ok)
		{
			ereport(INFO,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg(
						 "rum opfamily %s contains function %s with wrong signature for support number %d",
						 opfamilyname,
						 format_procedure(procform->amproc),
						 procform->amprocnum)));
			result = false;
		}
	}

	/* Check individual operators */
	for (i = 0; i < oprlist->n_members; i++)
	{
		HeapTuple oprtup = &oprlist->members[i]->tuple;
		Form_pg_amop oprform = (Form_pg_amop) GETSTRUCT(oprtup);

		/* TODO: Check that only allowed strategy numbers exist */
		if (oprform->amopstrategy < 1 || oprform->amopstrategy > 63)
		{
			ereport(INFO,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg(
						 "rum opfamily %s contains operator %s with invalid strategy number %d",
						 opfamilyname,
						 format_operator(oprform->amopopr),
						 oprform->amopstrategy)));
			result = false;
		}

		/* Check ORDER BY operator signature */
		if (oprform->amoppurpose == AMOP_ORDER)
		{
			/* tsvector's distance returns float4 */
			if (oprform->amoplefttype == TSVECTOROID &&
				!check_amop_signature(oprform->amopopr, FLOAT4OID,
									  oprform->amoplefttype,
									  oprform->amoprighttype))
			{
				ereport(INFO,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg(
							 "rum opfamily %s contains invalid ORDER BY specification for operator %s",
							 opfamilyname,
							 format_operator(oprform->amopopr))));
				result = false;
			}
			else if (hasRawOrderingProc &&
					 oprform->amoplefttype != TSVECTOROID &&
					 check_amop_signature(oprform->amopopr, opcintype,
										  oprform->amoplefttype,
										  oprform->amoprighttype))
			{
				/* This is allowed */
			}

			/* other types distance returns float8 */
			else if (oprform->amoplefttype != TSVECTOROID &&
					 !check_amop_signature(oprform->amopopr, FLOAT8OID,
										   oprform->amoplefttype,
										   oprform->amoprighttype))
			{
				ereport(INFO,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg(
							 "rum opfamily %s contains invalid ORDER BY specification for operator %s",
							 opfamilyname,
							 format_operator(oprform->amopopr))));
				result = false;
			}
		}

		/* Verify alternative operator definition */
		else if (!check_amop_signature(oprform->amopopr, BOOLOID,
									   oprform->amoplefttype,
									   oprform->amoprighttype))
		{
			ereport(INFO,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("rum opfamily %s contains operator %s with wrong signature",
							opfamilyname,
							format_operator(oprform->amopopr))));
			result = false;
		}
	}

	/* Now check for inconsistent groups of operators/functions */
	grouplist = identify_opfamily_groups(oprlist, proclist);
	opclassgroup = NULL;
	foreach(lc, grouplist)
	{
		OpFamilyOpFuncGroup *thisgroup = (OpFamilyOpFuncGroup *) lfirst(lc);

		/* Remember the group exactly matching the test opclass */
		if (thisgroup->lefttype == opcintype &&
			thisgroup->righttype == opcintype)
		{
			opclassgroup = thisgroup;
		}

		/*
		 * There is not a lot we can do to check the operator sets, since each
		 * RUM opclass is more or less a law unto itself, and some contain
		 * only operators that are binary-compatible with the opclass datatype
		 * (meaning that empty operator sets can be OK).  That case also means
		 * that we shouldn't insist on nonempty function sets except for the
		 * opclass's own group.
		 */
	}

	/* Check that the originally-named opclass is complete */
	for (i = 1; i <= RUMNProcs; i++)
	{
		if (opclassgroup &&
			(opclassgroup->functionset & (((uint64) 1) << i)) != 0)
		{
			continue;           /* got it */
		}
		if (i == GIN_COMPARE_PROC ||
			i == GIN_COMPARE_PARTIAL_PROC)
		{
			continue;           /* optional method */
		}
		if (i >= RUM_CONFIG_PROC)
		{
			continue;
		}
		ereport(INFO,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("rum opclass %s is missing support function %d",
						opclassname, i)));
		result = false;
	}
	if (!opclassgroup ||
		(opclassgroup->functionset & (1 << GIN_CONSISTENT_PROC)) == 0)
	{
		ereport(INFO,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("rum opclass %s is missing support function %d",
						opclassname, GIN_CONSISTENT_PROC)));
		result = false;
	}

	ReleaseCatCacheList(proclist);
	ReleaseCatCacheList(oprlist);
	ReleaseSysCache(familytup);
	ReleaseSysCache(classtup);

	return result;
}

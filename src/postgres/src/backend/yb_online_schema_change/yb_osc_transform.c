/*--------------------------------------------------------------------------------------------------
 *
 * yb_osc_transform.c
 *    Call an online schema change job's row transform.
 *
 *    The transform is a PL/pgSQL function from the source row type to the
 *    target row type, called once per source row.  The backfill calls it in
 *    a LATERAL function scan of its INSERT ... SELECT; the change-capture
 *    trigger evaluates the call directly.
 *
 *    PL/pgSQL flushes the DocDB write buffer before each statement of a
 *    function unless yb_speculatively_execute_pl_statements is on, so both
 *    callers turn it on for the transform (YbOscForceSpeculativePl).  Its
 *    body is a single RETURN (see CheckTransformBody), which then does not
 *    flush.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_osc_transform.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_type.h"
#include "nodes/makefuncs.h"
#include "nodes/primnodes.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/ruleutils.h"
#include "yb_osc_internal.h"


/*
 * Build the call of transform function fnOid on the source row.  The caller
 * must have checked that it takes srcRel's row type and returns dstRel's.
 */
YbOscTransform *
YbOscCompileTransform(Oid fnOid, Relation srcRel, Relation dstRel)
{
	/*
	 * Begin checked the function (YbOscValidate), and it cannot change while
	 * the job runs (YbOscCheckTransformChange).
	 */
	Oid			rettype = get_func_rettype(fnOid);

	if (!OidIsValid(rettype))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("transform function with OID %u does not exist", fnOid)));

	Var		   *wholeRow = makeVar(1, InvalidAttrNumber, srcRel->rd_rel->reltype,
								   -1, InvalidOid, 0);
	YbOscTransform *xf = palloc0(sizeof(YbOscTransform));

	xf->fnOid = fnOid;
	xf->call = (Expr *) makeFuncExpr(fnOid, rettype, list_make1(wholeRow),
									 InvalidOid, InvalidOid,
									 COERCE_EXPLICIT_CALL);
	return xf;
}

/*
 * The SELECT list of the backfill's INSERT: the live columns of the
 * transform's result "t", in attnum order.
 */
char *
YbOscTransformDeparse(YbOscTransform *xf, Relation srcRel,
					  Relation dstRel)
{
	TupleDesc	dstTupdesc = RelationGetDescr(dstRel);
	StringInfoData buf;

	initStringInfo(&buf);

	for (int i = 0; i < dstTupdesc->natts; ++i)
	{
		Form_pg_attribute att = TupleDescAttr(dstTupdesc, i);

		if (att->attisdropped)
			continue;
		if (buf.len > 0)
			appendStringInfoString(&buf, ", ");
		appendStringInfo(&buf, "t.%s", quote_identifier(NameStr(att->attname)));
	}

	return buf.data;
}

/*
 * The transform's call on source alias "s", for the backfill's
 * "CROSS JOIN LATERAL <call> AS t".  In the SELECT list, "(fn(s)).*" would
 * call it once per column.  The deparser passes the row as "s.*", which a
 * source column named "s" cannot capture, and names the function so that it
 * resolves to fnOid rather than to another overload.
 */
char *
YbOscTransformDeparseCall(YbOscTransform *xf, Relation srcRel)
{
	List	   *context = deparse_context_for("s", RelationGetRelid(srcRel));

	return deparse_expression((Node *) xf->call, context,
							  true /* forceprefix */ ,
							  false /* showimplicit */ ,
							  false /* yb_pretty */ ,
							  false /* yb_maskconstants */ );
}

/*
 * Turn on yb_speculatively_execute_pl_statements until the caller's GUC nest
 * level is popped, so that the transform's assignments and RETURN do not flush
 * the write buffer on every row.  The transform has no side effects that an
 * error from a buffered write could leave behind.
 */
void
YbOscForceSpeculativePl(void)
{
	(void) set_config_option("yb_speculatively_execute_pl_statements", "on",
							 PGC_SUSET, PGC_S_SESSION,
							 GUC_ACTION_SAVE, true, 0, false);
}

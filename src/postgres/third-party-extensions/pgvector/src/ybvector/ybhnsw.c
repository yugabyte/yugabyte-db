/*--------------------------------------------------------------------------
 *
 * ybhnsw.c
 *	  Access method for Yugabyte Vector Index implementation using HNSW.
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
 * IDENTIFICATION
 *		third-party-extensions/pgvector/ybvector/ybhnsw.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/pg_opclass.h"
#include "commands/yb_cmds.h"
#include "utils/syscache.h"
#include "ybvector.h"


#define YBHNSW_DEFAULT_M 32
#define YBHNSW_MIN_M 5
#define YBHNSW_MAX_M 64

#define YBHNSW_DEFAULT_M0 0
#define YBHNSW_MIN_M0 0
#define YBHNSW_MAX_M0 (YBHNSW_MAX_M * 4)

#define YBHNSW_DEFAULT_EF_CONSTRUCTION 200
#define YBHNSW_MIN_EF_CONSTRUCTION 50
#define YBHNSW_MAX_EF_CONSTRUCTION 1000

/* Imported from pgvector defaults as of v0.8.0. */
#define YBHNSW_DEFAULT_EF_SEARCH 40
#define YBHNSW_MIN_EF_SEARCH 1
#define YBHNSW_MAX_EF_SEARCH 1000

static relopt_kind ybhnsw_relopt_kind;

int			ybhnsw_ef_search;

/* 
 * Copied from pgvector's HnswInit (as of pgvector v0.8.0).
 */
typedef struct YbHnswCreateOptions
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	int			m;				/* number of connections per node */
	int			m0;				/* number of connections per node in base level */
	int			ef_construction;	/* size of dynamic candidate list */
}			YbHnswCreateOptions;

void
YbHnswInit(void)
{
	ybhnsw_relopt_kind = add_reloption_kind();
	/* Copied from HnswInit (as of pgvector 0.8.0). */
	add_int_reloption(ybhnsw_relopt_kind, "m", "Max number of connections",
					  YBHNSW_DEFAULT_M, YBHNSW_MIN_M, YBHNSW_MAX_M,
					  AccessExclusiveLock);
	add_int_reloption(ybhnsw_relopt_kind, "m0", "Max number of connections in base level",
					  YBHNSW_DEFAULT_M0, YBHNSW_MIN_M0, YBHNSW_MAX_M0,
					  AccessExclusiveLock);
	add_int_reloption(ybhnsw_relopt_kind, "ef_construction",
					  "Size of the dynamic candidate list for construction",
					  YBHNSW_DEFAULT_EF_CONSTRUCTION,
					  YBHNSW_MIN_EF_CONSTRUCTION, YBHNSW_MAX_EF_CONSTRUCTION,
					  AccessExclusiveLock);
	/*
	 * Notes:
	 * - Both hnsw.ef_search and ybhnsw.ef_search map to the same underlying
	 *   variable. This allows both GUCs to have the same value irrespective of
	 *   which one is used.
	 * - If both GUCs are set via 'ysql_pg_conf_csv', the value of ybhnsw.ef_search
	 *   will override the value of hnsw.ef_search. This is because the new
	 *   values of the GUCs are applied in the order in which the GUCs are
	 *   defined in the extension init function (which invokes this function).
	 * - A caveat of this mechanism is that if both of these GUCs are set via
	 *   'ysql_pg_conf_csv', and the user runs a 'SHOW <guc-name>' query before
	 *   the extension is init'ed, the GUCs will display different values. This
	 *   is harmless (as one can't use the GUC meaningfully without the
	 *   extension) and will get reconciled automatically upon running a query
	 *   that causes the extension to initialize.
	 */
	DefineCustomIntVariable("hnsw.ef_search", "Sets the size of the dynamic candidate list for search",
							"Valid range is 1..1000. This parameter automatically maintains the "
							"same value as ybhnsw.ef_search. If both are set at startup, the value "
							"of ybhnsw.ef_search is prioritized over the value of hnsw.ef_search. ",
							&ybhnsw_ef_search,
							YBHNSW_DEFAULT_EF_SEARCH, YBHNSW_MIN_EF_SEARCH, YBHNSW_MAX_EF_SEARCH, PGC_USERSET, 0, NULL, NULL, NULL);
	MarkGUCPrefixReserved("hnsw");

	DefineCustomIntVariable("ybhnsw.ef_search", "Sets the size of the dynamic candidate list for search",
							"Valid range is 1..1000. This parameter automatically maintains the "
							"same value as hnsw.ef_search.", &ybhnsw_ef_search,
							YBHNSW_DEFAULT_EF_SEARCH, YBHNSW_MIN_EF_SEARCH, YBHNSW_MAX_EF_SEARCH, PGC_USERSET, 0, NULL, NULL, NULL);
	MarkGUCPrefixReserved("ybhnsw");
}

/*
 * Parse and validate the reloptions
 */
static bytea *
ybhnswoptions(Datum reloptions, bool validate)
{
	/* 
 	 * Copied from pgvector's hnswoptions (as of pgvector v0.8.0).
 	 */
	static const relopt_parse_elt tab[] = {
		{"m", RELOPT_TYPE_INT, offsetof(YbHnswCreateOptions, m)},
		{"m0", RELOPT_TYPE_INT, offsetof(YbHnswCreateOptions, m0)},
		{"ef_construction", RELOPT_TYPE_INT,
		offsetof(YbHnswCreateOptions, ef_construction)},
	};

	return (bytea *) build_reloptions(reloptions, validate,
									  ybhnsw_relopt_kind,
									  sizeof(YbHnswCreateOptions),
									  tab, lengthof(tab));
}

static void
bindYbHnswIndexOptions(YbcPgStatement handle, Datum reloptions)
{
	YbHnswCreateOptions *hnsw_options = (YbHnswCreateOptions *) ybhnswoptions(reloptions, false);
	int			m = YBHNSW_DEFAULT_M;
	int         m0 = YBHNSW_DEFAULT_M;
	int			ef_construction = YBHNSW_DEFAULT_EF_CONSTRUCTION;

	if (hnsw_options)
	{
		m = hnsw_options->m;
		m0 = hnsw_options->m0;
		if (m0 < m)
			m0 = m;
		ef_construction = hnsw_options->ef_construction;
	}
	YBCPgCreateIndexSetHnswOptions(handle, m, m0, ef_construction);
}

static void
ybhnswbindcolumnschema(YbcPgStatement handle,
					   IndexInfo *indexInfo,
					   TupleDesc indexTupleDesc,
					   int16 *coloptions,
					   Oid *opclassOids,
					   Datum reloptions)
{
	HeapTuple	ht_opc;
	Form_pg_opclass opcrec;

	Assert(indexInfo->ii_NumIndexKeyAttrs == 1);
	ht_opc = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclassOids[0]));
	if (!HeapTupleIsValid(ht_opc))
		elog(ERROR, "cache lookup failed for opclass %u", opclassOids[0]);
	opcrec = (Form_pg_opclass) GETSTRUCT(ht_opc);
	YbcPgVectorDistType dist_type;
	if (!strcmp(opcrec->opcname.data, "vector_l2_ops")) {
		dist_type = YB_VEC_DIST_L2;
	} else if (!strcmp(opcrec->opcname.data, "vector_ip_ops")) {
		dist_type = YB_VEC_DIST_IP;
	} else if (!strcmp(opcrec->opcname.data, "vector_cosine_ops")) {
		dist_type = YB_VEC_DIST_COSINE;
	} else {
		elog(ERROR, "unsupported vector index op class name %s",
			 opcrec->opcname.data);
	}
	ReleaseSysCache(ht_opc);

	bindVectorIndexOptions(handle, indexInfo, indexTupleDesc, YB_VEC_HNSW,
						   dist_type);
	bindYbHnswIndexOptions(handle, reloptions);
	YBCBindCreateIndexColumns(handle, indexInfo, indexTupleDesc, coloptions, 0);
}

static void
ybBindHnswReadOptions(YbScanDesc yb_scan)
{
	YBCPgDmlHnswSetReadOptions(yb_scan->handle, ybhnsw_ef_search);
}

/*
 * ybvectorrescan
 *		Reset temporary structures to prepare for rescan.
 */
void
ybhnswrescan(IndexScanDesc scan, ScanKey scankeys, int nscankeys,
			 ScanKey orderbys, int norderbys)
{
	ybvectorrescan(scan, scankeys, nscankeys, orderbys, norderbys);
	YbVectorScanOpaque so = (YbVectorScanOpaque) scan->opaque;
	YbScanDesc ybscan = so->yb_scan_desc;
	ybBindHnswReadOptions(ybscan);
}


/*
 * ybusearchhandler handler function: return
 * IndexAmRoutine with access method parameters and callbacks.
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(ybhnswhandler);
Datum
ybhnswhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine;
	amroutine = makeBaseYbVectorHandler(true /* is_copartitioned */ );

	amroutine->yb_ambindschema = ybhnswbindcolumnschema;
	amroutine->amoptions = ybhnswoptions;

	amroutine->amrescan = ybhnswrescan;

	PG_RETURN_POINTER(amroutine);
}

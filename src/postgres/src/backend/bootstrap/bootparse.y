%{
/*-------------------------------------------------------------------------
 *
 * bootparse.y
 *	  yacc grammar for the "bootstrap" mode (BKI file format)
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/bootstrap/bootparse.y
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <unistd.h>

#include "bootstrap/bootstrap.h"
#include "catalog/heap.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_class.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "catalog/pg_tablespace.h"
#include "catalog/toasting.h"
#include "commands/defrem.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "utils/memutils.h"

#include "pg_yb_utils.h"
#include "executor/ybModifyTable.h"
#include "bootstrap/yb_bootstrap.h"

/*
 * Bison doesn't allocate anything that needs to live across parser calls,
 * so we can easily have it use palloc instead of malloc.  This prevents
 * memory leaks if we error out during parsing.  Note this only works with
 * bison >= 2.0.  However, in bison 1.875 the default is to use alloca()
 * if possible, so there's not really much problem anyhow, at least if
 * you're building with gcc.
 */
#define YYMALLOC palloc
#define YYFREE   pfree

static MemoryContext per_line_ctx = NULL;

static void
do_start(void)
{
	Assert(GetCurrentMemoryContext() == CurTransactionContext);
	/* First time through, create the per-line working context */
	if (per_line_ctx == NULL)
		per_line_ctx = AllocSetContextCreate(CurTransactionContext,
											 "bootstrap per-line processing",
											 ALLOCSET_DEFAULT_SIZES);
	MemoryContextSwitchTo(per_line_ctx);
}


static void
do_end(void)
{
	/* Reclaim memory allocated while processing this line */
	MemoryContextSwitchTo(CurTransactionContext);
	MemoryContextReset(per_line_ctx);
	CHECK_FOR_INTERRUPTS();		/* allow SIGINT to kill bootstrap run */
	if (isatty(0))
	{
		printf("bootstrap> ");
		fflush(stdout);
	}
}


static int num_columns_read = 0;

%}

%expect 0
%name-prefix="boot_yy"

%union
{
	List		*list;
	IndexElem	*ielem;
	IndexStmt	*istmt;  /* Used for YugaByte index/pkey clauses */
	char		*str;
	const char	*kw;
	int			ival;
	Oid			oidval;
}

%type <list>  boot_index_params
%type <ielem> boot_index_param
%type <istmt> Boot_YBIndex
%type <str>   boot_ident
%type <ival>  optbootstrap optsharedrelation boot_column_nullness
%type <oidval> oidspec optrowtypeoid

%token <str> ID
%token COMMA EQUALS LPAREN RPAREN
/* NULLVAL is a reserved keyword */
%token NULLVAL
/* All the rest are unreserved, and should be handled in boot_ident! */
%token <kw> OPEN XCLOSE XCREATE INSERT_TUPLE
%token <kw> XDECLARE YBDECLARE INDEX ON USING XBUILD INDICES PRIMARY UNIQUE XTOAST
%token <kw> OBJ_ID XBOOTSTRAP XSHARED_RELATION XROWTYPE_OID
%token <kw> XFORCE XNOT XNULL
%token <kw> YBCHECKINITDBDONE

%start TopLevel

%%

TopLevel:
		  Boot_Queries
		|
		;

Boot_Queries:
		  Boot_Query
		| Boot_Queries Boot_Query
		;

Boot_Query :
		  Boot_OpenStmt
		| Boot_CloseStmt
		| Boot_CreateStmt
		| Boot_InsertStmt
		| Boot_DeclareIndexStmt
		| Boot_DeclareUniqueIndexStmt
		| Boot_DeclarePrimaryIndexStmt
		| Boot_DeclareToastStmt
		| Boot_BuildIndsStmt
    | Boot_CheckInitDbDone
		;

Boot_OpenStmt:
		  OPEN boot_ident
				{
					do_start();
					boot_openrel($2);
					do_end();
				}
		;

Boot_CloseStmt:
		  XCLOSE boot_ident
				{
					do_start();
					closerel($2);
					do_end();
				}
		;
Boot_YBIndex:
          /* EMPTY */ { $$ = NULL; }
          | YBDECLARE PRIMARY INDEX boot_ident oidspec ON boot_ident USING boot_ident
            LPAREN boot_index_params RPAREN
				{
					IndexStmt *stmt = makeNode(IndexStmt);

					do_start();

					stmt->idxname = $4;
					stmt->relation = makeRangeVar(NULL, $7, -1);
					stmt->accessMethod = $9;
					stmt->tableSpace = NULL;
					stmt->indexParams = $11;
					stmt->options = NIL;
					stmt->whereClause = NULL;
					stmt->excludeOpNames = NIL;
					stmt->idxcomment = NULL;
					stmt->indexOid = $5;
					stmt->oldNode = InvalidOid;
					stmt->unique = true;
					stmt->primary = true;
					stmt->isconstraint = false;
					stmt->deferrable = false;
					stmt->initdeferred = false;
					stmt->transformed = false;
					stmt->concurrent = false;
					stmt->if_not_exists = false;

					do_end();

					$$ = stmt;
				}
		;

Boot_CreateStmt:
		  XCREATE boot_ident oidspec optbootstrap optsharedrelation optrowtypeoid LPAREN
				{
					do_start();
					numattr = 0;
					elog(DEBUG4, "creating%s%s relation %s %u",
						 $4 ? " bootstrap" : "",
						 $5 ? " shared" : "",
						 $2,
						 $3);
				}
		  boot_column_list
				{
					do_end();
				}
		  RPAREN Boot_YBIndex
				{
					TupleDesc	tupdesc;
					bool		shared_relation;
					bool		mapped_relation;

					do_start();

					tupdesc = CreateTupleDesc(numattr, attrtypes);

					shared_relation = $5;

					/*
					 * The catalogs that use the relation mapper are the
					 * bootstrap catalogs plus the shared catalogs.  If this
					 * ever gets more complicated, we should invent a BKI
					 * keyword to mark the mapped catalogs, but for now a
					 * quick hack seems the most appropriate thing.  Note in
					 * particular that all "nailed" heap rels (see formrdesc
					 * in relcache.c) must be mapped.
					 */
					mapped_relation = ($4 || shared_relation);

					if ($4)
					{
						TransactionId relfrozenxid;
						MultiXactId relminmxid;

						if (boot_reldesc)
						{
							elog(DEBUG4, "create bootstrap: warning, open relation exists, closing first");
							closerel(NULL);
						}

						boot_reldesc = heap_create($2,
												   PG_CATALOG_NAMESPACE,
												   shared_relation ? GLOBALTABLESPACE_OID : 0,
												   InvalidOid, /* reltablegroup */
												   $3,
												   InvalidOid,
												   HEAP_TABLE_AM_OID,
												   tupdesc,
												   RELKIND_RELATION,
												   RELPERSISTENCE_PERMANENT,
												   shared_relation,
												   mapped_relation,
												   true,
												   &relfrozenxid,
												   &relminmxid,
												   true);
						elog(DEBUG4, "bootstrap relation created");
					}
					else
					{
						Oid			id;

						id = heap_create_with_catalog($2,
													  PG_CATALOG_NAMESPACE,
													  shared_relation ? GLOBALTABLESPACE_OID : 0,
													  InvalidOid, /* reltablegroup */
													  $3,
													  $6,
													  InvalidOid,
													  BOOTSTRAP_SUPERUSERID,
													  HEAP_TABLE_AM_OID,
													  tupdesc,
													  NIL,
													  RELKIND_RELATION,
													  RELPERSISTENCE_PERMANENT,
													  shared_relation,
													  mapped_relation,
													  ONCOMMIT_NOOP,
													  (Datum) 0,
													  false,
													  true,
													  false,
													  InvalidOid,
													  NULL,
													  false);
						elog(DEBUG4, "relation created with OID %u", id);
					}

					if (IsYugaByteEnabled())
					{
						YBCCreateSysCatalogTable($2, $3, tupdesc, shared_relation, $12);
					}

                    do_end();
				}
		;

Boot_InsertStmt:
		  INSERT_TUPLE
				{
					do_start();
					elog(DEBUG4, "inserting row");
					num_columns_read = 0;
				}
		  LPAREN boot_column_val_list RPAREN
				{
					if (num_columns_read != numattr)
						elog(ERROR, "incorrect number of columns in row (expected %d, got %d)",
							 numattr, num_columns_read);
					if (boot_reldesc == NULL)
						elog(FATAL, "relation not open");
					InsertOneTuple();
					do_end();
				}
		;

Boot_DeclareIndexStmt:
		  XDECLARE INDEX boot_ident oidspec ON boot_ident USING boot_ident LPAREN boot_index_params RPAREN
				{
					IndexStmt  *stmt = makeNode(IndexStmt);
					Oid			relationId;

					elog(DEBUG4, "creating index \"%s\"", $3);

					do_start();

					stmt->idxname = $3;
					stmt->relation = makeRangeVar(NULL, $6, -1);
					stmt->accessMethod = $8;
					stmt->tableSpace = NULL;
					stmt->indexParams = $10;
					stmt->indexIncludingParams = NIL;
					stmt->options = NIL;
					stmt->whereClause = NULL;
					stmt->excludeOpNames = NIL;
					stmt->idxcomment = NULL;
					stmt->indexOid = InvalidOid;
					stmt->oldNode = InvalidOid;
					stmt->oldCreateSubid = InvalidSubTransactionId;
					stmt->oldFirstRelfilenodeSubid = InvalidSubTransactionId;
					stmt->unique = false;
					stmt->primary = false;
					stmt->isconstraint = false;
					stmt->deferrable = false;
					stmt->initdeferred = false;
					stmt->transformed = false;
					stmt->concurrent = false;
					stmt->if_not_exists = false;
					stmt->reset_default_tblspc = false;

					/* locks and races need not concern us in bootstrap mode */
					relationId = RangeVarGetRelid(stmt->relation, NoLock,
												  false);

					DefineIndex(relationId,
								stmt,
								$4,
								InvalidOid,
								InvalidOid,
								false,
								false,
								false,
								true, /* skip_build */
								false);
					do_end();
				}
		;

Boot_DeclareUniqueIndexStmt:
		  XDECLARE UNIQUE INDEX boot_ident oidspec ON boot_ident USING boot_ident LPAREN boot_index_params RPAREN
				{
					IndexStmt  *stmt = makeNode(IndexStmt);
					Oid			relationId;

					elog(DEBUG4, "creating unique index \"%s\"", $4);

					do_start();

					stmt->idxname = $4;
					stmt->relation = makeRangeVar(NULL, $7, -1);
					stmt->accessMethod = $9;
					stmt->tableSpace = NULL;
					stmt->indexParams = $11;
					stmt->indexIncludingParams = NIL;
					stmt->options = NIL;
					stmt->whereClause = NULL;
					stmt->excludeOpNames = NIL;
					stmt->idxcomment = NULL;
					stmt->indexOid = InvalidOid;
					stmt->oldNode = InvalidOid;
					stmt->oldCreateSubid = InvalidSubTransactionId;
					stmt->oldFirstRelfilenodeSubid = InvalidSubTransactionId;
					stmt->unique = true;
					stmt->primary = false;
					stmt->isconstraint = false;
					stmt->deferrable = false;
					stmt->initdeferred = false;
					stmt->transformed = false;
					stmt->concurrent = false;
					stmt->if_not_exists = false;
					stmt->reset_default_tblspc = false;

					/* locks and races need not concern us in bootstrap mode */
					relationId = RangeVarGetRelid(stmt->relation, NoLock,
												  false);

					DefineIndex(relationId,
								stmt,
								$5,
								InvalidOid,
								InvalidOid,
								false,
								false,
								false,
								true, /* skip_build */
								false);
					do_end();
				}
		;

Boot_DeclarePrimaryIndexStmt:
		  XDECLARE PRIMARY INDEX boot_ident oidspec ON boot_ident USING boot_ident LPAREN boot_index_params RPAREN
				{
					IndexStmt *stmt = makeNode(IndexStmt);
					Oid		relationId;

					do_start();

					stmt->idxname = $4;
					stmt->relation = makeRangeVar(NULL, $7, -1);
					stmt->accessMethod = $9;
					stmt->tableSpace = NULL;
					stmt->indexParams = $11;
					stmt->options = NIL;
					stmt->whereClause = NULL;
					stmt->excludeOpNames = NIL;
					stmt->idxcomment = NULL;
					stmt->indexOid = InvalidOid;
					stmt->oldNode = InvalidOid;
					stmt->unique = true;
					stmt->primary = true;
					stmt->isconstraint = false;
					stmt->deferrable = false;
					stmt->initdeferred = false;
					stmt->transformed = false;
					stmt->concurrent = false;
					stmt->if_not_exists = false;

					/* locks and races need not concern us in bootstrap mode */
					relationId = RangeVarGetRelid(stmt->relation, NoLock,
												  false);

					DefineIndex(relationId,
								stmt,
								$5,
								InvalidOid,
								InvalidOid,
								false,
								false,
								false,
								true, /* skip_build */
								false);
					do_end();
				}
		;

Boot_DeclareToastStmt:
		  XDECLARE XTOAST oidspec oidspec ON boot_ident
				{
					elog(DEBUG4, "creating toast table for table \"%s\"", $6);

					do_start();

					BootstrapToastTable($6, $3, $4);
					do_end();
				}
		;

Boot_BuildIndsStmt:
		  XBUILD INDICES
				{
					do_start();
					build_indices();
					do_end();
				}
		;


boot_index_params:
		boot_index_params COMMA boot_index_param	{ $$ = lappend($1, $3); }
		| boot_index_param							{ $$ = list_make1($1); }
		;

boot_index_param:
		boot_ident boot_ident
				{
					IndexElem  *n = makeNode(IndexElem);

					n->name = $1;
					n->expr = NULL;
					n->indexcolname = NULL;
					n->collation = NIL;
					n->opclass = list_make1(makeString($2));
					n->ordering = SORTBY_DEFAULT;
					n->nulls_ordering = SORTBY_NULLS_DEFAULT;
					$$ = n;
				}
		;

optbootstrap:
			XBOOTSTRAP	{ $$ = 1; }
		|				{ $$ = 0; }
		;

optsharedrelation:
			XSHARED_RELATION	{ $$ = 1; }
		|						{ $$ = 0; }
		;

optrowtypeoid:
			XROWTYPE_OID oidspec	{ $$ = $2; }
		|							{ $$ = InvalidOid; }
		;

boot_column_list:
		  boot_column_def
		| boot_column_list COMMA boot_column_def
		;

boot_column_def:
		  boot_ident EQUALS boot_ident boot_column_nullness
				{
				   if (++numattr > MAXATTR)
						elog(FATAL, "too many columns");
				   DefineAttr($1, $3, numattr-1, $4);
				}
		;

boot_column_nullness:
			XFORCE XNOT XNULL	{ $$ = BOOTCOL_NULL_FORCE_NOT_NULL; }
		|	XFORCE XNULL		{  $$ = BOOTCOL_NULL_FORCE_NULL; }
		| { $$ = BOOTCOL_NULL_AUTO; }
		;

oidspec:
			boot_ident							{ $$ = atooid($1); }
		;

boot_column_val_list:
		   boot_column_val
		|  boot_column_val_list boot_column_val
		|  boot_column_val_list COMMA boot_column_val
		;

boot_column_val:
		  boot_ident
			{ InsertOneValue($1, num_columns_read++); }
		| NULLVAL
			{ InsertOneNull(num_columns_read++); }
		;

Boot_CheckInitDbDone:
      YBCHECKINITDBDONE
      {
				/*
				 * In the normal case, don't run initdb twice. During a ysql major version upgrade,
				 * initdb is already done for the prior version, but we need to run it for the new
				 * version.
				 */
				if (!YBIsMajorUpgradeInitDb() && YBIsInitDbAlreadyDone())
					exit(YB_INITDB_ALREADY_DONE_EXIT_CODE);
			}

boot_ident:
		  ID			{ $$ = $1; }
		| OPEN			{ $$ = pstrdup($1); }
		| XCLOSE		{ $$ = pstrdup($1); }
		| XCREATE		{ $$ = pstrdup($1); }
		| INSERT_TUPLE	{ $$ = pstrdup($1); }
		| XDECLARE		{ $$ = pstrdup($1); }
		| YBDECLARE		{ $$ = pstrdup($1); }
		| INDEX			{ $$ = pstrdup($1); }
		| ON			{ $$ = pstrdup($1); }
		| USING			{ $$ = pstrdup($1); }
		| XBUILD		{ $$ = pstrdup($1); }
		| INDICES		{ $$ = pstrdup($1); }
		| UNIQUE		{ $$ = pstrdup($1); }
		| XTOAST		{ $$ = pstrdup($1); }
		| OBJ_ID		{ $$ = pstrdup($1); }
		| XBOOTSTRAP	{ $$ = pstrdup($1); }
		| XSHARED_RELATION	{ $$ = pstrdup($1); }
		| XROWTYPE_OID	{ $$ = pstrdup($1); }
		| XFORCE		{ $$ = pstrdup($1); }
		| XNOT			{ $$ = pstrdup($1); }
		| XNULL			{ $$ = pstrdup($1); }
		| YBCHECKINITDBDONE { $$ = pstrdup($1); }
		;
%%

#include "bootscanner.c"

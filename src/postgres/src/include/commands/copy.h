/*-------------------------------------------------------------------------
 *
 * copy.h
 *	  Definitions for using the POSTGRES copy command.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/copy.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef COPY_H
#define COPY_H

#include "nodes/execnodes.h"
#include "nodes/parsenodes.h"
#include "parser/parse_node.h"
#include "tcop/dest.h"

/*
 * Represents whether a header line should be present, and whether it must
 * match the actual names (which implies "true").
 */
typedef enum CopyHeaderChoice
{
	COPY_HEADER_FALSE = 0,
	COPY_HEADER_TRUE,
	COPY_HEADER_MATCH,
} CopyHeaderChoice;

/*
 * A struct to hold COPY options, in a parsed form. All of these are related
 * to formatting, except for 'freeze', which doesn't really belong here, but
 * it's expedient to parse it along with all the other options.
 */
typedef struct CopyFormatOptions
{
	/* parameters from the COPY command */
	int			file_encoding;	/* file or remote side's character encoding,
								 * -1 if not specified */
	bool		binary;			/* binary format? */
	bool		freeze;			/* freeze rows on loading? */
	bool		csv_mode;		/* Comma Separated Value format? */
	CopyHeaderChoice header_line;	/* header line? */
	char	   *null_print;		/* NULL marker string (server encoding!) */
	int			null_print_len; /* length of same */
	char	   *null_print_client;	/* same converted to file encoding */
	char	   *delim;			/* column delimiter (must be 1 byte) */
	char	   *quote;			/* CSV quote char (must be 1 byte) */
	char	   *escape;			/* CSV escape char (must be 1 byte) */
	List	   *force_quote;	/* list of column names */
	bool		force_quote_all;	/* FORCE_QUOTE *? */
	bool	   *force_quote_flags;	/* per-column CSV FQ flags */
	List	   *force_notnull;	/* list of column names */
	bool	   *force_notnull_flags;	/* per-column CSV FNN flags */
	List	   *force_null;		/* list of column names */
	bool	   *force_null_flags;	/* per-column CSV FN flags */
	bool		convert_selectively;	/* do selective binary conversion? */
	List	   *convert_select; /* list of column names (can be NIL) */

	/* Yugabytes attributes */
	int			batch_size;		/* copy from executes in batch sizes */
	uint64		num_initial_skipped_rows;	/* rows to skip at the beginning
											 * of the file */
	bool		disable_fk_check;	/* Disable FK check? */
	OnConflictAction on_conflict_action;	/* How to handle when the new row
											 * conflicts with existing row */
} CopyFormatOptions;

/* These are private in commands/copy[from|to].c */
typedef struct CopyFromStateData *CopyFromState;
typedef struct CopyToStateData *CopyToState;

typedef int (*copy_data_source_cb) (void *outbuf, int minread, int maxread);

/*
 * This is Yugabyte macros.
 */
#define DEFAULT_BATCH_ROWS_PER_TRANSACTION  20000

/**
 * YSQL guc variables that can be used to set rows per transaciton for batch copy from.
 * e.g. 'SET yb_default_copy_from_rows_per_transaction=1000'
 * See also the corresponding entries in guc.c.
 */
extern int	yb_default_copy_from_rows_per_transaction;

extern void DoCopy(ParseState *state, const CopyStmt *stmt,
				   int stmt_location, int stmt_len,
				   uint64 *processed);

extern void ProcessCopyOptions(ParseState *pstate, CopyFormatOptions *ops_out, bool is_from, List *options);
extern CopyFromState BeginCopyFrom(ParseState *pstate, Relation rel, Node *whereClause,
								   const char *filename,
								   bool is_program, copy_data_source_cb data_source_cb, List *attnamelist, List *options);
extern void EndCopyFrom(CopyFromState cstate);
extern bool NextCopyFrom(CopyFromState cstate, ExprContext *econtext,
						 Datum *values, bool *nulls, bool skip_row);
extern bool NextCopyFromRawFields(CopyFromState cstate,
								  char ***fields, int *nfields);
extern void CopyFromErrorCallback(void *arg);

extern uint64 CopyFrom(CopyFromState cstate);

extern DestReceiver *CreateCopyDestReceiver(void);

/*
 * internal prototypes
 */
extern CopyToState BeginCopyTo(ParseState *pstate, Relation rel, RawStmt *query,
							   Oid queryRelId, const char *filename, bool is_program,
							   List *attnamelist, List *options);
extern void EndCopyTo(CopyToState cstate);
extern uint64 DoCopyTo(CopyToState cstate);
extern List *CopyGetAttnums(TupleDesc tupDesc, Relation rel,
							List *attnamelist);

#endif							/* COPY_H */

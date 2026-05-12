/*-------------------------------------------------------------------------
 *
 * rum_ts_utils.c
 *		various text-search functions
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/hash.h"
#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "tsearch/ts_type.h"
#include "tsearch/ts_utils.h"
#include "utils/array.h"
#include "utils/builtins.h"
#if PG_VERSION_NUM >= 120000
#include "utils/float.h"
#endif
#include "utils/typcache.h"

#include "pg_documentdb_rum.h"

#include <math.h>

/* Use TS_EXEC_PHRASE_AS_AND when TS_EXEC_PHRASE_NO_POS is not defined */
#ifndef TS_EXEC_PHRASE_NO_POS
#define TS_EXEC_PHRASE_NO_POS TS_EXEC_PHRASE_AS_AND
#endif

#if PG_VERSION_NUM >= 130000

/* Since v13 TS_execute flag naming and defaults have reverted:
 * - before v13 -                   - since v13 -
 * TS_EXEC_CALC_NOT (0x01)          TS_EXEC_SKIP_NOT (0x01)
 */
#define TS_EXEC_CALC_NOT (0x01) /*  Defined here for use with rum_TS_execute for
	                             *	compatibility with version < 13 where this
	                             *	flag was defined globally.
	                             *	XXX Since v13 current global flag
	                             *	TS_EXEC_SKIP_NOT has reverted meaning for
	                             *	TS_execute but TS_EXEC_CALC_NOT should still
	                             *	be passed to rum_TS_execute in unchanged (previous)
	                             *	meaning but should not be passed into TS_execute:
	                             *	(TS_execute will do 'calc not' by default, and
	                             *	if you need skip it, use new TS_EXEC_SKIP_NOT)
	                             */
typedef TSTernaryValue RumTernaryValue;
#else
typedef enum
{
	TS_NO,                      /* definitely no match */
	TS_YES,                     /* definitely does match */
	TS_MAYBE                    /* can't verify match for lack of pos data */
} RumTernaryValue;
#endif
typedef RumTernaryValue (*RumExecuteCallbackTernary) (void *arg, QueryOperand *val,
													  ExecPhraseData *data);

PG_FUNCTION_INFO_V1(documentdb_extended_rum_extract_tsvector);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_extract_tsvector_hash);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_extract_tsquery);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_extract_tsquery_hash);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_tsvector_config);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_tsquery_pre_consistent);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_tsquery_consistent);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_tsquery_timestamp_consistent);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_tsquery_distance);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_ts_distance_tt);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_ts_distance_ttf);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_ts_distance_td);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_ts_score_tt);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_ts_score_ttf);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_ts_score_td);
PG_FUNCTION_INFO_V1(documentdb_extended_rum_ts_join_pos);

PG_FUNCTION_INFO_V1(documentdb_tsquery_to_distance_query);

static unsigned int count_pos(char *ptr, int len);
static char * decompress_pos(char *ptr, WordEntryPos *pos);
static Datum build_tsvector_entry(TSVector vector, WordEntry *we);
static Datum build_tsvector_hash_entry(TSVector vector, WordEntry *we);
static Datum build_tsquery_entry(TSQuery query, QueryOperand *operand);
static Datum build_tsquery_hash_entry(TSQuery query, QueryOperand *operand);

static RumTernaryValue rum_phrase_output(ExecPhraseData *data, ExecPhraseData *Ldata,
										 ExecPhraseData *Rdata,
										 int emit,
										 int Loffset,
										 int Roffset,
										 int max_npos);
static RumTernaryValue rum_phrase_execute(QueryItem *curitem, void *arg, uint32 flags,
										  RumExecuteCallbackTernary chkcond,
										  ExecPhraseData *data);
static RumTernaryValue rum_TS_execute(QueryItem *curitem, void *arg, uint32 flags,
									  RumExecuteCallbackTernary chkcond);

typedef Datum (*TSVectorEntryBuilder)(TSVector vector, WordEntry *we);
typedef Datum (*TSQueryEntryBuilder)(TSQuery query, QueryOperand *operand);

static Datum * rum_extract_tsvector_internal(TSVector vector, int32 *nentries,
											 Datum **addInfo,
											 bool **addInfoIsNull,
											 TSVectorEntryBuilder build_tsvector_entry);
static Datum * rum_extract_tsquery_internal(TSQuery query, int32 *nentries,
											bool **ptr_partialmatch,
											Pointer **extra_data,
											int32 *searchMode,
											TSQueryEntryBuilder build_tsquery_entry);

typedef struct
{
	QueryItem *first_item;
	int *map_item_operand;
	bool *check;
	bool *need_recheck;
	Datum *addInfo;
	bool *addInfoIsNull;
	bool recheckPhrase;
}   RumChkVal;

typedef struct
{
	union
	{
		/* Used in rum_ts_distance() */
		struct
		{
			QueryItem **item;
			int16 nitem;
		} item;

		/* Used in rum_tsquery_distance() */
		struct
		{
			QueryItem *item_first;
			int32 keyn;
		} key;
	} data;
	uint8 wclass;
	int32 pos;
} DocRepresentation;

typedef struct
{
	bool operandexist;
	WordEntryPos pos;
}
QueryRepresentationOperand;

typedef struct
{
	TSQuery query;

	/* Used in rum_tsquery_distance() */
	int *map_item_operand;

	QueryRepresentationOperand *operandData;
	int length;
} QueryRepresentation;

typedef struct
{
	int pos;
	int p;
	int q;
	DocRepresentation *begin;
	DocRepresentation *end;
} Extention;

static float weights[] = { 1.0 / 0.1f, 1.0 / 0.2f, 1.0 / 0.4f, 1.0 / 1.0f };

/* A dummy WordEntryPos array to use when haspos is false */
static WordEntryPosVector POSNULL = {
	1,                          /* Number of elements that follow */
	{ 0 }
};

#define RANK_NO_NORM 0x00
#define RANK_NORM_LOGLENGTH 0x01
#define RANK_NORM_LENGTH 0x02
#define RANK_NORM_EXTDIST 0x04
#define RANK_NORM_UNIQ 0x08
#define RANK_NORM_LOGUNIQ 0x10
#define RANK_NORM_RDIVRPLUS1 0x20
#define DEF_NORM_METHOD RANK_NO_NORM

/*
 * Should not conflict with defines
 * TS_EXEC_EMPTY/TS_EXEC_CALC_NOT/TS_EXEC_PHRASE_NO_POS
 */
#define TS_EXEC_IN_NEG 0x04

#define QR_GET_OPERAND(q, v) \
	(&((q)->operandData[((QueryItem *) (v)) - GETQUERY((q)->query)]))

#if PG_VERSION_NUM >= 130000
static TSTernaryValue
#else
static bool
#endif
pre_checkcondition_rum(void *checkval, QueryOperand *val, ExecPhraseData *data)
{
	RumChkVal *gcv = (RumChkVal *) checkval;
	int j;

	/* if any val requiring a weight is used, set recheck flag */
	if (val->weight != 0 || data != NULL)
	{
		*(gcv->need_recheck) = true;
	}

	/* convert item's number to corresponding entry's (operand's) number */
	j = gcv->map_item_operand[((QueryItem *) val) - gcv->first_item];

	/* return presence of current entry in indexed value */
	#if PG_VERSION_NUM >= 130000
	return (*(gcv->need_recheck) ? TS_MAYBE : (gcv->check[j] ? TS_YES : TS_NO));
	#else
	return gcv->check[j];
	#endif
}


PGDLLEXPORT Datum
documentdb_extended_rum_tsquery_pre_consistent(PG_FUNCTION_ARGS)
{
	bool *check = (bool *) PG_GETARG_POINTER(0);
	TSQuery query = PG_GETARG_TSQUERY(2);
	Pointer *extra_data = (Pointer *) PG_GETARG_POINTER(4);
	bool recheck = false;
	bool res = false;

	if (query->size > 0)
	{
		RumChkVal gcv;

		/*
		 * check-parameter array has one entry for each value (operand) in the
		 * query.
		 */
		gcv.first_item = GETQUERY(query);
		gcv.check = check;
		gcv.map_item_operand = (int *) (extra_data[0]);
		gcv.need_recheck = &recheck;

		res = TS_execute(GETQUERY(query),
						 &gcv,
						 TS_EXEC_PHRASE_NO_POS
#if PG_VERSION_NUM >= 130000
						 | TS_EXEC_SKIP_NOT
#endif
						 ,
						 pre_checkcondition_rum);
	}
	PG_RETURN_BOOL(res);
}


static RumTernaryValue
checkcondition_rum(void *checkval, QueryOperand *val, ExecPhraseData *data)
{
	RumChkVal *gcv = (RumChkVal *) checkval;
	int j;

	/* convert item's number to corresponding entry's (operand's) number */
	j = gcv->map_item_operand[((QueryItem *) val) - gcv->first_item];

	if (!gcv->check[j])
	{
		/* lexeme not present in indexed value */
		return TS_NO;
	}
	else if (gcv->addInfo && gcv->addInfoIsNull[j] == false)
	{
		bytea *positions;
		int32 i;
		char *ptrt;
		WordEntryPos post = 0;
		int32 npos;
		int32 k = 0;

		/*
		 * we don't have positions in index because we store a timestamp in
		 * addInfo
		 */
		if (gcv->recheckPhrase)
		{
			return ((val->weight) ? TS_MAYBE : TS_YES);
		}

		positions = DatumGetByteaP(gcv->addInfo[j]);
		ptrt = (char *) VARDATA_ANY(positions);
		npos = count_pos(VARDATA_ANY(positions),
						 VARSIZE_ANY_EXHDR(positions));

		/* caller wants an array of positions (phrase search) */
		if (data)
		{
			data->pos = palloc(sizeof(*data->pos) * npos);
			data->allocated = true;

			/* Fill positions that has right weight to return to a caller */
			for (i = 0; i < npos; i++)
			{
				ptrt = decompress_pos(ptrt, &post);

				/*
				 * Weight mark is stored as 2 bits inside position mark in RUM
				 * index. We compare it to a list of requested positions in
				 * query operand (4 bits one for each weight mark).
				 */
				if ((val->weight == 0) || (val->weight >> WEP_GETWEIGHT(post)) & 1)
				{
					data->pos[k] = post;
					k++;
				}
			}
			data->npos = k;
			data->pos = repalloc(data->pos, sizeof(*data->pos) * k);
			return (k ? TS_YES : TS_NO);
		}
		/*
		 * Not phrase search. We only need to know if there's at least one
		 * position with right weight then return TS_YES, otherwise return
		 * TS_NO. For this search work without recheck we need that any
		 * negation in recursion will give TS_MAYBE and initiate recheck as
		 * "!word:A" can mean both: "word:BCВ" or "!word"
		 */
		else if (val->weight == 0)
		{
			/* Query without weights */
			return TS_YES;
		}
		else
		{
			char KeyWeightsMask = 0;

			/* Fill KeyWeightMask contains with weights from all positions */
			for (i = 0; i < npos; i++)
			{
				ptrt = decompress_pos(ptrt, &post);
				KeyWeightsMask |= 1 << WEP_GETWEIGHT(post);
			}
			return ((KeyWeightsMask & val->weight) ? TS_YES : TS_NO);
		}
	}

/* Should never come here */
	return TS_MAYBE;
}


/*
 * Compute output position list for a tsquery operator in phrase mode.
 *
 * Merge the position lists in Ldata and Rdata as specified by "emit",
 * returning the result list into *data.  The input position lists must be
 * sorted and unique, and the output will be as well.
 *
 * data: pointer to initially-all-zeroes output struct, or NULL
 * Ldata, Rdata: input position lists
 * emit: bitmask of TSPO_XXX flags
 * Loffset: offset to be added to Ldata positions before comparing/outputting
 * Roffset: offset to be added to Rdata positions before comparing/outputting
 * max_npos: maximum possible required size of output position array
 *
 * Loffset and Roffset should not be negative, else we risk trying to output
 * negative positions, which won't fit into WordEntryPos.
 *
 * The result is boolean (TS_YES or TS_NO), but for the caller's convenience
 * we return it as RumTernaryValue.
 *
 * Returns TS_YES if any positions were emitted to *data; or if data is NULL,
 * returns TS_YES if any positions would have been emitted.
 */
#define TSPO_L_ONLY 0x01        /* emit positions appearing only in L */
#define TSPO_R_ONLY 0x02        /* emit positions appearing only in R */
#define TSPO_BOTH 0x04          /* emit positions appearing in both L&R */

static RumTernaryValue
rum_phrase_output(ExecPhraseData *data,
				  ExecPhraseData *Ldata,
				  ExecPhraseData *Rdata,
				  int emit,
				  int Loffset,
				  int Roffset,
				  int max_npos)
{
	int Lindex,
		Rindex;

	/* Loop until both inputs are exhausted */
	Lindex = Rindex = 0;
	while (Lindex < Ldata->npos || Rindex < Rdata->npos)
	{
		int Lpos,
			Rpos;
		int output_pos = 0;

		/*
		 * Fetch current values to compare.  WEP_GETPOS() is needed because
		 * ExecPhraseData->data can point to a tsvector's WordEntryPosVector.
		 */
		if (Lindex < Ldata->npos)
		{
			Lpos = WEP_GETPOS(Ldata->pos[Lindex]) + Loffset;
		}
		else
		{
			/* L array exhausted, so we're done if R_ONLY isn't set */
			if (!(emit & TSPO_R_ONLY))
			{
				break;
			}
			Lpos = INT_MAX;
		}
		if (Rindex < Rdata->npos)
		{
			Rpos = WEP_GETPOS(Rdata->pos[Rindex]) + Roffset;
		}
		else
		{
			/* R array exhausted, so we're done if L_ONLY isn't set */
			if (!(emit & TSPO_L_ONLY))
			{
				break;
			}
			Rpos = INT_MAX;
		}

		/* Merge-join the two input lists */
		if (Lpos < Rpos)
		{
			/* Lpos is not matched in Rdata, should we output it? */
			if (emit & TSPO_L_ONLY)
			{
				output_pos = Lpos;
			}
			Lindex++;
		}
		else if (Lpos == Rpos)
		{
			/* Lpos and Rpos match ... should we output it? */
			if (emit & TSPO_BOTH)
			{
				output_pos = Rpos;
			}
			Lindex++;
			Rindex++;
		}
		else                    /* Lpos > Rpos */
		{
			/* Rpos is not matched in Ldata, should we output it? */
			if (emit & TSPO_R_ONLY)
			{
				output_pos = Rpos;
			}
			Rindex++;
		}

		if (output_pos > 0)
		{
			if (data)
			{
				/* Store position, first allocating output array if needed */
				if (data->pos == NULL)
				{
					data->pos = (WordEntryPos *)
								palloc(max_npos * sizeof(WordEntryPos));
					data->allocated = true;
				}
				data->pos[data->npos++] = output_pos;
			}
			else
			{
				/*
				 * Exact positions not needed, so return TS_YES as soon as we
				 * know there is at least one.
				 */
				return TS_YES;
			}
		}
	}

	if (data && data->npos > 0)
	{
		/* Let's assert we didn't overrun the array */
		Assert(data->npos <= max_npos);
		return TS_YES;
	}
	return TS_NO;
}


/*
 * Execute tsquery at or below an OP_PHRASE operator.
 *
 * This handles tsquery execution at recursion levels where we need to care
 * about match locations.
 *
 * In addition to the same arguments used for TS_execute, the caller may pass
 * a preinitialized-to-zeroes ExecPhraseData struct, to be filled with lexeme
 * match position info on success.  data == NULL if no position data need be
 * returned.  (In practice, outside callers pass NULL, and only the internal
 * recursion cases pass a data pointer.)
 * Note: the function assumes data != NULL for operators other than OP_PHRASE.
 * This is OK because an outside call always starts from an OP_PHRASE node.
 *
 * The detailed semantics of the match data, given that the function returned
 * TS_YES (successful match), are:
 *
 * npos > 0, negate = false:
 *	 query is matched at specified position(s) (and only those positions)
 * npos > 0, negate = true:
 *	 query is matched at all positions *except* specified position(s)
 * npos = 0, negate = true:
 *	 query is matched at all positions
 * npos = 0, negate = false:
 *	 disallowed (this should result in TS_NO or TS_MAYBE, as appropriate)
 *
 * Successful matches also return a "width" value which is the match width in
 * lexemes, less one.  Hence, "width" is zero for simple one-lexeme matches,
 * and is the sum of the phrase operator distances for phrase matches.  Note
 * that when width > 0, the listed positions represent the ends of matches not
 * the starts.  (This unintuitive rule is needed to avoid possibly generating
 * negative positions, which wouldn't fit into the WordEntryPos arrays.)
 *
 * If the RumExecuteCallback function reports that an operand is present
 * but fails to provide position(s) for it, we will return TS_MAYBE when
 * it is possible but not certain that the query is matched.
 *
 * When the function returns TS_NO or TS_MAYBE, it must return npos = 0,
 * negate = false (which is the state initialized by the caller); but the
 * "width" output in such cases is undefined.
 */
static RumTernaryValue
rum_phrase_execute(QueryItem *curitem, void *arg, uint32 flags,
				   RumExecuteCallbackTernary chkcond,
				   ExecPhraseData *data)
{
	ExecPhraseData Ldata,
				   Rdata;
	RumTernaryValue lmatch,
					rmatch;
	int Loffset,
		Roffset,
		maxwidth;

	/* since this function recurses, it could be driven to stack overflow */
	check_stack_depth();

	if (curitem->type == QI_VAL)
	{
		return (chkcond(arg, (QueryOperand *) curitem, data));
	}

	switch (curitem->qoperator.oper)
	{
		case OP_NOT:
		{
			/*
			 * We need not touch data->width, since a NOT operation does not
			 * change the match width.
			 */
			if (!(flags & TS_EXEC_CALC_NOT))
			{
				/* without CALC_NOT, report NOT as "match everywhere" */
				Assert(data->npos == 0 && !data->negate);
				data->negate = true;
				return TS_YES;
			}
			switch (rum_phrase_execute(curitem + 1, arg, flags, chkcond, data))
			{
				case TS_NO:
				{
					/* change "match nowhere" to "match everywhere" */
					Assert(data->npos == 0 && !data->negate);
					data->negate = true;
					return TS_YES;
				}

				case TS_YES:
				{
					if (data->npos > 0)
					{
						/* we have some positions, invert negate flag */
						data->negate = !data->negate;
						return TS_YES;
					}
					else if (data->negate)
					{
						/* change "match everywhere" to "match nowhere" */
						data->negate = false;
						return TS_NO;
					}

					/* Should not get here if result was TS_YES */
					Assert(false);
					break;
				}

				case TS_MAYBE:

					/* match positions are, and remain, uncertain */
					return TS_MAYBE;
			}
			break;
		}

		case OP_PHRASE:
		case OP_AND:
		{
			memset(&Ldata, 0, sizeof(Ldata));
			memset(&Rdata, 0, sizeof(Rdata));

			lmatch = rum_phrase_execute(curitem + curitem->qoperator.left,
										arg, flags, chkcond, &Ldata);
			if (lmatch == TS_NO)
			{
				return TS_NO;
			}

			rmatch = rum_phrase_execute(curitem + 1,
										arg, flags, chkcond, &Rdata);
			if (rmatch == TS_NO)
			{
				return TS_NO;
			}

			/*
			 * If either operand has no position information, then we can't
			 * return reliable position data, only a MAYBE result.
			 */
			if (lmatch == TS_MAYBE || rmatch == TS_MAYBE)
			{
				return TS_MAYBE;
			}

			if (curitem->qoperator.oper == OP_PHRASE)
			{
				/* In case of index where position is not available
				 * (e.g. addon_ops) output TS_MAYBE even in case both
				 * lmatch and rmatch are TS_YES. Otherwise we can lose
				 * results of phrase queries.
				 */
				if (flags & TS_EXEC_PHRASE_NO_POS)
				{
					return TS_MAYBE;
				}

				/*
				 * Compute Loffset and Roffset suitable for phrase match, and
				 * compute overall width of whole phrase match.
				 */
				Loffset = curitem->qoperator.distance + Rdata.width;
				Roffset = 0;
				if (data)
				{
					data->width = curitem->qoperator.distance +
								  Ldata.width + Rdata.width;
				}
			}
			else
			{
				/*
				 * For OP_AND, set output width and alignment like OP_OR (see
				 * comment below)
				 */
				maxwidth = Max(Ldata.width, Rdata.width);
				Loffset = maxwidth - Ldata.width;
				Roffset = maxwidth - Rdata.width;
				if (data)
				{
					data->width = maxwidth;
				}
			}

			if (Ldata.negate && Rdata.negate)
			{
				/* !L & !R: treat as !(L | R) */
				(void) rum_phrase_output(data, &Ldata, &Rdata,
										 TSPO_BOTH | TSPO_L_ONLY | TSPO_R_ONLY,
										 Loffset, Roffset,
										 Ldata.npos + Rdata.npos);
				if (data)
				{
					data->negate = true;
				}
				return TS_YES;
			}
			else if (Ldata.negate)
			{
				/* !L & R */
				return rum_phrase_output(data, &Ldata, &Rdata,
										 TSPO_R_ONLY,
										 Loffset, Roffset,
										 Rdata.npos);
			}
			else if (Rdata.negate)
			{
				/* L & !R */
				return rum_phrase_output(data, &Ldata, &Rdata,
										 TSPO_L_ONLY,
										 Loffset, Roffset,
										 Ldata.npos);
			}
			else
			{
				/* straight AND */
				return rum_phrase_output(data, &Ldata, &Rdata,
										 TSPO_BOTH,
										 Loffset, Roffset,
										 Min(Ldata.npos, Rdata.npos));
			}
		}

		case OP_OR:
		{
			memset(&Ldata, 0, sizeof(Ldata));
			memset(&Rdata, 0, sizeof(Rdata));

			lmatch = rum_phrase_execute(curitem + curitem->qoperator.left,
										arg, flags, chkcond, &Ldata);
			rmatch = rum_phrase_execute(curitem + 1,
										arg, flags, chkcond, &Rdata);

			if (lmatch == TS_NO && rmatch == TS_NO)
			{
				return TS_NO;
			}

			/*
			 * If either operand has no position information, then we can't
			 * return reliable position data, only a MAYBE result.
			 */
			if (lmatch == TS_MAYBE || rmatch == TS_MAYBE)
			{
				return TS_MAYBE;
			}

			/*
			 * Cope with undefined output width from failed submatch.  (This
			 * takes less code than trying to ensure that all failure returns
			 * et data->width to zero.)
			 */
			if (lmatch == TS_NO)
			{
				Ldata.width = 0;
			}
			if (rmatch == TS_NO)
			{
				Rdata.width = 0;
			}

			/*
			 * For OP_AND and OP_OR, report the width of the wider of the two
			 * inputs, and align the narrower input's positions to the right
			 * end of that width.  This rule deals at least somewhat
			 * reasonably with cases like "x <-> (y | z <-> q)".
			 */
			maxwidth = Max(Ldata.width, Rdata.width);
			Loffset = maxwidth - Ldata.width;
			Roffset = maxwidth - Rdata.width;
			data->width = maxwidth;

			if (Ldata.negate && Rdata.negate)
			{
				/* !L | !R: treat as !(L & R) */
				(void) rum_phrase_output(data, &Ldata, &Rdata,
										 TSPO_BOTH,
										 Loffset, Roffset,
										 Min(Ldata.npos, Rdata.npos));
				data->negate = true;
				return TS_YES;
			}
			else if (Ldata.negate)
			{
				/* !L | R: treat as !(L & !R) */
				(void) rum_phrase_output(data, &Ldata, &Rdata,
										 TSPO_L_ONLY,
										 Loffset, Roffset,
										 Ldata.npos);
				data->negate = true;
				return TS_YES;
			}
			else if (Rdata.negate)
			{
				/* L | !R: treat as !(!L & R) */
				(void) rum_phrase_output(data, &Ldata, &Rdata,
										 TSPO_R_ONLY,
										 Loffset, Roffset,
										 Rdata.npos);
				data->negate = true;
				return TS_YES;
			}
			else
			{
				/* straight OR */
				return rum_phrase_output(data, &Ldata, &Rdata,
										 TSPO_BOTH | TSPO_L_ONLY | TSPO_R_ONLY,
										 Loffset, Roffset,
										 Ldata.npos + Rdata.npos);
			}
		}

		default:
			elog(ERROR, "unrecognized operator: %d", curitem->qoperator.oper);
	}

	/* not reachable, but keep compiler quiet */
	return TS_NO;
}


/*
 * Evaluates tsquery boolean expression. It is similar to adt/tsvector_op.c
 * TS_execute_recurse() but in most cases when ! operator is used it should set
 * TS_MAYBE to recheck. The reason is that inside negation we can have one or several
 * operands with weights (which we can not easily know) and negative of them is not
 * precisely defined i.e. "!word:A" can mean "word:BCD" or "!word" (the same applies to
 * logical combination of them). One easily  only case we can avoid recheck is when before negation there
 * is QI_VAL which doesn't have weight.
 *
 * curitem: current tsquery item (initially, the first one)
 * arg: opaque value to pass through to callback function
 * flags: bitmask of flag bits shown in ts_utils.h
 * chkcond: callback function to check whether a primitive value is present
 */
static RumTernaryValue
rum_TS_execute(QueryItem *curitem, void *arg, uint32 flags,
			   RumExecuteCallbackTernary chkcond)
{
	RumTernaryValue lmatch;

	/* since this function recurses, it could be driven to stack overflow */
	check_stack_depth();

	if (curitem->type == QI_VAL)
	{
		if ((flags & TS_EXEC_IN_NEG) && curitem->qoperand.weight &&
			curitem->qoperand.weight != 15)
		{
			return TS_MAYBE;
		}
		else
		{
			return chkcond(arg, (QueryOperand *) curitem, NULL);
		}
	}

	switch (curitem->qoperator.oper)
	{
		case OP_NOT:
		{
			if (!(flags & TS_EXEC_CALC_NOT))
			{
				return TS_YES;
			}
			switch (rum_TS_execute(curitem + 1, arg, flags | TS_EXEC_IN_NEG, chkcond))
			{
				case TS_NO:
				{
					return TS_YES;
				}

				case TS_YES:
				{
					return TS_NO;
				}

				case TS_MAYBE:
					return TS_MAYBE;
			}
			break;
		}

		case OP_AND:
		{
			lmatch = rum_TS_execute(curitem + curitem->qoperator.left, arg,
									flags, chkcond);
			if (lmatch == TS_NO)
			{
				return TS_NO;
			}
			switch (rum_TS_execute(curitem + 1, arg, flags, chkcond))
			{
				case TS_NO:
				{
					return TS_NO;
				}

				case TS_YES:
				{
					return lmatch;
				}

				case TS_MAYBE:
					return TS_MAYBE;
			}
			break;
		}

		case OP_OR:
		{
			lmatch = rum_TS_execute(curitem + curitem->qoperator.left, arg,
									flags, chkcond);
			if (lmatch == TS_YES)
			{
				return TS_YES;
			}
			switch (rum_TS_execute(curitem + 1, arg, flags, chkcond))
			{
				case TS_NO:
				{
					return lmatch;
				}

				case TS_YES:
				{
					return TS_YES;
				}

				case TS_MAYBE:
					return TS_MAYBE;
			}
			break;
		}

		case OP_PHRASE:
		{
			/*
			 * If we get a MAYBE result, and the caller doesn't want that,
			 * convert it to NO.  It would be more consistent, perhaps, to
			 * return the result of TS_phrase_execute() verbatim and then
			 * convert MAYBE results at the top of the recursion.  But
			 * converting at the topmost phrase operator gives results that
			 * are bug-compatible with the old implementation, so do it like
			 * this for now.
			 *
			 * Checking for TS_EXEC_PHRASE_NO_POS has been moved inside
			 * rum_phrase_execute, otherwise we can lose results of phrase
			 * operator when position information is not available in index
			 * (e.g. index built with addon_ops)
			 */
			switch (rum_phrase_execute(curitem, arg, flags, chkcond, NULL))
			{
				case TS_NO:
				{
					return TS_NO;
				}

				case TS_YES:
				{
					return TS_YES;
				}

				case TS_MAYBE:
					return (flags & TS_EXEC_PHRASE_NO_POS) ? TS_MAYBE : TS_NO;
			}
			break;
		}

		default:
			elog(ERROR, "unrecognized operator: %d", curitem->qoperator.oper);
	}

	/* not reachable, but keep compiler quiet */
	return TS_NO;
}


PGDLLEXPORT Datum
documentdb_extended_rum_tsquery_consistent(PG_FUNCTION_ARGS)
{
	bool *check = (bool *) PG_GETARG_POINTER(0);

	/* StrategyNumber strategy = PG_GETARG_UINT16(1); */
	TSQuery query = PG_GETARG_TSQUERY(2);

	/* int32	nkeys = PG_GETARG_INT32(3); */
	Pointer *extra_data = (Pointer *) PG_GETARG_POINTER(4);
	bool *recheck = (bool *) PG_GETARG_POINTER(5);
	Datum *addInfo = (Datum *) PG_GETARG_POINTER(8);
	bool *addInfoIsNull = (bool *) PG_GETARG_POINTER(9);

	RumTernaryValue res = TS_NO;

	/*
	 * The query doesn't require recheck by default
	 */
	*recheck = false;

	if (query->size > 0)
	{
		RumChkVal gcv;

		/*
		 * check-parameter array has one entry for each value (operand) in the
		 * query.
		 */
		gcv.first_item = GETQUERY(query);
		gcv.check = check;
		gcv.map_item_operand = (int *) (extra_data[0]);
		gcv.need_recheck = recheck;
		gcv.addInfo = addInfo;
		gcv.addInfoIsNull = addInfoIsNull;
		gcv.recheckPhrase = false;

		res = rum_TS_execute(GETQUERY(query), &gcv,
							 TS_EXEC_CALC_NOT,
							 checkcondition_rum);
		if (res == TS_MAYBE)
		{
			*recheck = true;
		}
	}
	PG_RETURN_BOOL(res);
}


PGDLLEXPORT Datum
documentdb_extended_rum_tsquery_timestamp_consistent(PG_FUNCTION_ARGS)
{
	bool *check = (bool *) PG_GETARG_POINTER(0);

	/* StrategyNumber strategy = PG_GETARG_UINT16(1); */
	TSQuery query = PG_GETARG_TSQUERY(2);

	/* int32	nkeys = PG_GETARG_INT32(3); */
	Pointer *extra_data = (Pointer *) PG_GETARG_POINTER(4);
	bool *recheck = (bool *) PG_GETARG_POINTER(5);
	Datum *addInfo = (Datum *) PG_GETARG_POINTER(8);
	bool *addInfoIsNull = (bool *) PG_GETARG_POINTER(9);
	RumTernaryValue res = TS_NO;

	/*
	 * The query requires recheck only if it involves weights
	 */
	*recheck = false;

	if (query->size > 0)
	{
		RumChkVal gcv;

		/*
		 * check-parameter array has one entry for each value (operand) in the
		 * query.
		 */
		gcv.first_item = GETQUERY(query);
		gcv.check = check;
		gcv.map_item_operand = (int *) (extra_data[0]);
		gcv.need_recheck = recheck;
		gcv.addInfo = addInfo;
		gcv.addInfoIsNull = addInfoIsNull;
		gcv.recheckPhrase = true;

		res = rum_TS_execute(GETQUERY(query), &gcv,
							 TS_EXEC_CALC_NOT | TS_EXEC_PHRASE_NO_POS,
							 checkcondition_rum);
		if (res == TS_MAYBE)
		{
			*recheck = true;
		}
	}
	PG_RETURN_BOOL(res);
}


#define SIXTHBIT 0x20
#define LOWERMASK 0x1F

static unsigned int
compress_pos(char *target, WordEntryPos *pos, int npos)
{
	int i;
	uint16 prev = 0,
		   delta;
	char *ptr;

	ptr = target;
	for (i = 0; i < npos; i++)
	{
		delta = WEP_GETPOS(pos[i]) - WEP_GETPOS(prev);

		while (true)
		{
			if (delta >= SIXTHBIT)
			{
				*ptr = (delta & (~HIGHBIT)) | HIGHBIT;
				ptr++;
				delta >>= 7;
			}
			else
			{
				*ptr = delta | (WEP_GETWEIGHT(pos[i]) << 5);
				ptr++;
				break;
			}
		}
		prev = pos[i];
	}
	return ptr - target;
}


static char *
decompress_pos(char *ptr, WordEntryPos *pos)
{
	int i;
	uint8 v;
	uint16 delta = 0;

	i = 0;
	while (true)
	{
		v = *ptr;
		ptr++;
		if (v & HIGHBIT)
		{
			delta |= (v & (~HIGHBIT)) << i;
		}
		else
		{
			delta |= (v & LOWERMASK) << i;
			Assert(delta <= 0x3fff);
			*pos += delta;
			WEP_SETWEIGHT(*pos, v >> 5);
			return ptr;
		}
		i += 7;
	}
}


static unsigned int
count_pos(char *ptr, int len)
{
	int count = 0,
		i;

	for (i = 0; i < len; i++)
	{
		if (!(ptr[i] & HIGHBIT))
		{
			count++;
		}
	}
	Assert((ptr[i - 1] & HIGHBIT) == 0);
	return count;
}


static uint32
count_length(TSVector t)
{
	WordEntry *ptr = ARRPTR(t),
			  *end = (WordEntry *) STRPTR(t);
	uint32 len = 0;

	while (ptr < end)
	{
		uint32 clen = POSDATALEN(t, ptr);

		if (clen == 0)
		{
			len += 1;
		}
		else
		{
			len += clen;
		}

		ptr++;
	}

	return len;
}


/*
 * sort QueryOperands by (length, word)
 */
static int
compareQueryOperand(const void *a, const void *b, void *arg)
{
	char *operand = (char *) arg;
	QueryOperand *qa = (*(QueryOperand *const *) a);
	QueryOperand *qb = (*(QueryOperand *const *) b);

	return tsCompareString(operand + qa->distance, qa->length,
						   operand + qb->distance, qb->length,
						   false);
}


/*
 * Returns a sorted, de-duplicated array of QueryOperands in a query.
 * The returned QueryOperands are pointers to the original QueryOperands
 * in the query.
 *
 * Length of the returned array is stored in *size
 */
static QueryOperand **
SortAndUniqItems(TSQuery q, int *size)
{
	char *operand = GETOPERAND(q);
	QueryItem *item = GETQUERY(q);
	QueryOperand **res,
				 **ptr,
				 **prevptr;

	ptr = res = (QueryOperand **) palloc(sizeof(QueryOperand *) * *size);

	/* Collect all operands from the tree to res */
	while ((*size)--)
	{
		if (item->type == QI_VAL)
		{
			*ptr = (QueryOperand *) item;
			ptr++;
		}
		item++;
	}

	*size = ptr - res;
	if (*size < 2)
	{
		return res;
	}

	qsort_arg(res, *size, sizeof(QueryOperand *), compareQueryOperand, (void *) operand);

	ptr = res + 1;
	prevptr = res;

	/* remove duplicates */
	while (ptr - res < *size)
	{
		if (compareQueryOperand((void *) ptr, (void *) prevptr, (void *) operand) != 0)
		{
			prevptr++;
			*prevptr = *ptr;
		}
		ptr++;
	}

	*size = prevptr + 1 - res;
	return res;
}


/*
 * Extracting tsvector lexems.
 */

/*
 * Extracts tsvector lexemes from **vector**. Uses **build_tsvector_entry**
 * callback to extract entry.
 */
static Datum *
rum_extract_tsvector_internal(TSVector vector,
							  int32 *nentries,
							  Datum **addInfo,
							  bool **addInfoIsNull,
							  TSVectorEntryBuilder build_tsvector_entry)
{
	Datum *entries = NULL;

	*nentries = vector->size;
	if (vector->size > 0)
	{
		int i;
		WordEntry *we = ARRPTR(vector);
		WordEntryPosVector *posVec;

		entries = (Datum *) palloc(sizeof(Datum) * vector->size);
		*addInfo = (Datum *) palloc(sizeof(Datum) * vector->size);
		*addInfoIsNull = (bool *) palloc(sizeof(bool) * vector->size);

		for (i = 0; i < vector->size; i++)
		{
			bytea *posData;
			int posDataSize;

			/* Extract entry using specified method */
			entries[i] = build_tsvector_entry(vector, we);

			if (we->haspos)
			{
				posVec = _POSVECPTR(vector, we);

				/*
				 * In some cases compressed positions may take more memory than
				 * uncompressed positions. So allocate memory with a margin.
				 */
				posDataSize = VARHDRSZ + 2 * posVec->npos * sizeof(WordEntryPos);
				posData = (bytea *) palloc(posDataSize);

				posDataSize = compress_pos(posData->vl_dat, posVec->pos, posVec->npos) +
							  VARHDRSZ;
				SET_VARSIZE(posData, posDataSize);

				(*addInfo)[i] = PointerGetDatum(posData);
				(*addInfoIsNull)[i] = false;
			}
			else
			{
				(*addInfo)[i] = (Datum) 0;
				(*addInfoIsNull)[i] = true;
			}
			we++;
		}
	}

	return entries;
}


/*
 * Used as callback for rum_extract_tsvector_internal().
 * Just extract entry from tsvector.
 */
static Datum
build_tsvector_entry(TSVector vector, WordEntry *we)
{
	text *txt;

	txt = cstring_to_text_with_len(STRPTR(vector) + we->pos, we->len);
	return PointerGetDatum(txt);
}


/*
 * Used as callback for rum_extract_tsvector_internal.
 * Returns hashed entry from tsvector.
 */
static Datum
build_tsvector_hash_entry(TSVector vector, WordEntry *we)
{
	int32 hash_value;

	hash_value = hash_any((const unsigned char *) (STRPTR(vector) + we->pos),
						  we->len);
	return Int32GetDatum(hash_value);
}


/*
 * Extracts lexemes from tsvector with additional information.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_extract_tsvector(PG_FUNCTION_ARGS)
{
	TSVector vector = PG_GETARG_TSVECTOR(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);
	Datum **addInfo = (Datum **) PG_GETARG_POINTER(3);
	bool **addInfoIsNull = (bool **) PG_GETARG_POINTER(4);
	Datum *entries = NULL;

	entries = rum_extract_tsvector_internal(vector, nentries, addInfo,
											addInfoIsNull,
											build_tsvector_entry);
	PG_FREE_IF_COPY(vector, 0);
	PG_RETURN_POINTER(entries);
}


/*
 * Extracts hashed lexemes from tsvector with additional information.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_extract_tsvector_hash(PG_FUNCTION_ARGS)
{
	TSVector vector = PG_GETARG_TSVECTOR(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);
	Datum **addInfo = (Datum **) PG_GETARG_POINTER(3);
	bool **addInfoIsNull = (bool **) PG_GETARG_POINTER(4);
	Datum *entries = NULL;

	entries = rum_extract_tsvector_internal(vector, nentries, addInfo,
											addInfoIsNull,
											build_tsvector_hash_entry);

	PG_FREE_IF_COPY(vector, 0);
	PG_RETURN_POINTER(entries);
}


/*
 * Extracting tsquery lexemes.
 */

/*
 * Extracts tsquery lexemes from **query**. Uses **build_tsquery_entry**
 * callback to extract lexeme.
 */
static Datum *
rum_extract_tsquery_internal(TSQuery query,
							 int32 *nentries,
							 bool **ptr_partialmatch,
							 Pointer **extra_data,
							 int32 *searchMode,
							 TSQueryEntryBuilder build_tsquery_entry)
{
	Datum *entries = NULL;

	*nentries = 0;

	if (query->size > 0)
	{
		QueryItem *item = GETQUERY(query);
		int32 i,
			  j;
		bool *partialmatch;
		int *map_item_operand;
		char *operand = GETOPERAND(query);
		QueryOperand **operands;

		/*
		 * If the query doesn't have any required positive matches (for
		 * instance, it's something like '! foo'), we have to do a full index
		 * scan.
		 */
		if (tsquery_requires_match(item))
		{
			*searchMode = GIN_SEARCH_MODE_DEFAULT;
		}
		else
		{
			*searchMode = GIN_SEARCH_MODE_ALL;
		}

		*nentries = query->size;
		operands = SortAndUniqItems(query, nentries);

		entries = (Datum *) palloc(sizeof(Datum) * (*nentries));
		partialmatch = *ptr_partialmatch = (bool *) palloc(sizeof(bool) * (*nentries));

		/*
		 * Make map to convert item's number to corresponding operand's (the
		 * same, entry's) number. Entry's number is used in check array in
		 * consistent method. We use the same map for each entry.
		 */
		*extra_data = (Pointer *) palloc(sizeof(Pointer) * (*nentries));
		map_item_operand = (int *) palloc0(sizeof(int) * query->size);

		for (i = 0; i < (*nentries); i++)
		{
			entries[i] = build_tsquery_entry(query, operands[i]);
			partialmatch[i] = operands[i]->prefix;
			(*extra_data)[i] = (Pointer) map_item_operand;
		}

		/* Now rescan the VAL items and fill in the arrays */
		for (j = 0; j < query->size; j++)
		{
			if (item[j].type == QI_VAL)
			{
				QueryOperand *val = &item[j].qoperand;
				bool found = false;

				for (i = 0; i < (*nentries); i++)
				{
					if (!tsCompareString(operand + operands[i]->distance,
										 operands[i]->length,
										 operand + val->distance, val->length,
										 false))
					{
						map_item_operand[j] = i;
						found = true;
						break;
					}
				}

				if (!found)
				{
					elog(ERROR, "Operand not found!");
				}
			}
		}
	}

	return entries;
}


/*
 * Extract lexeme from tsquery.
 */
static Datum
build_tsquery_entry(TSQuery query, QueryOperand *operand)
{
	text *txt;

	txt = cstring_to_text_with_len(GETOPERAND(query) + operand->distance,
								   operand->length);
	return PointerGetDatum(txt);
}


/*
 * Extract hashed lexeme from tsquery.
 */
static Datum
build_tsquery_hash_entry(TSQuery query, QueryOperand *operand)
{
	int32 hash_value;

	hash_value = hash_any(
		(const unsigned char *) (GETOPERAND(query) + operand->distance),
		operand->length);
	return hash_value;
}


/*
 * Extracts lexemes from tsquery with information about prefix search syntax.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_extract_tsquery(PG_FUNCTION_ARGS)
{
	TSQuery query = PG_GETARG_TSQUERY(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);

	/* StrategyNumber strategy = PG_GETARG_UINT16(2); */
	bool **ptr_partialmatch = (bool **) PG_GETARG_POINTER(3);
	Pointer **extra_data = (Pointer **) PG_GETARG_POINTER(4);

	/* bool   **nullFlags = (bool **) PG_GETARG_POINTER(5); */
	int32 *searchMode = (int32 *) PG_GETARG_POINTER(6);
	Datum *entries = NULL;

	entries = rum_extract_tsquery_internal(query, nentries, ptr_partialmatch,
										   extra_data, searchMode,
										   build_tsquery_entry);

	PG_FREE_IF_COPY(query, 0);

	PG_RETURN_POINTER(entries);
}


/*
 * Extracts hashed lexemes from tsquery with information about prefix search
 * syntax.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_extract_tsquery_hash(PG_FUNCTION_ARGS)
{
	TSQuery query = PG_GETARG_TSQUERY(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);

	/* StrategyNumber strategy = PG_GETARG_UINT16(2); */
	bool **ptr_partialmatch = (bool **) PG_GETARG_POINTER(3);
	Pointer **extra_data = (Pointer **) PG_GETARG_POINTER(4);

	/* bool   **nullFlags = (bool **) PG_GETARG_POINTER(5); */
	int32 *searchMode = (int32 *) PG_GETARG_POINTER(6);
	Datum *entries = NULL;

	entries = rum_extract_tsquery_internal(query, nentries, ptr_partialmatch,
										   extra_data, searchMode,
										   build_tsquery_hash_entry);

	PG_FREE_IF_COPY(query, 0);

	PG_RETURN_POINTER(entries);
}


/*
 * Functions used for ranking.
 */
static int
compareDocR(const void *va, const void *vb)
{
	DocRepresentation *a = (DocRepresentation *) va;
	DocRepresentation *b = (DocRepresentation *) vb;

	if (a->pos == b->pos)
	{
		return 0;
	}
	return (a->pos > b->pos) ? 1 : -1;
}


/*
 * Be carefull: clang 11+ is very sensitive to casting function
 * with different return value.
 */
static
#if PG_VERSION_NUM >= 130000
TSTernaryValue
#else
bool
#endif
checkcondition_QueryOperand(void *checkval, QueryOperand *val,
							ExecPhraseData *data)
{
	QueryRepresentation *qr = (QueryRepresentation *) checkval;
	QueryRepresentationOperand *qro;

	/* Check for rum_tsquery_distance() */
	if (qr->map_item_operand != NULL)
	{
		qro = qr->operandData +
			  qr->map_item_operand[(QueryItem *) val - GETQUERY(qr->query)];
	}
	else
	{
		qro = QR_GET_OPERAND(qr, val);
	}

	if (data && qro->operandexist)
	{
		data->npos = 1;
		data->pos = &qro->pos;
		data->allocated = false;
	}

	return qro->operandexist
#if PG_VERSION_NUM >= 130000
		   ? TS_YES : TS_NO
#endif
	;
}


static bool
Cover(DocRepresentation *doc, uint32 len, QueryRepresentation *qr,
	  Extention *ext)
{
	DocRepresentation *ptr;
	int lastpos;
	int i;
	bool found;

restart:
	lastpos = ext->pos;
	found = false;

	memset(qr->operandData, 0, sizeof(qr->operandData[0]) * qr->length);

	ext->p = PG_INT32_MAX;
	ext->q = 0;
	ptr = doc + ext->pos;

	/* find upper bound of cover from current position, move up */
	while (ptr - doc < len)
	{
		QueryRepresentationOperand *qro;

		if (qr->map_item_operand != NULL)
		{
			qro = qr->operandData + ptr->data.key.keyn;
			qro->operandexist = true;
			WEP_SETPOS(qro->pos, ptr->pos);
			WEP_SETWEIGHT(qro->pos, ptr->wclass);
		}
		else
		{
			for (i = 0; i < ptr->data.item.nitem; i++)
			{
				qro = QR_GET_OPERAND(qr, ptr->data.item.item[i]);
				qro->operandexist = true;
				WEP_SETPOS(qro->pos, ptr->pos);
				WEP_SETWEIGHT(qro->pos, ptr->wclass);
			}
		}

		if (TS_execute(GETQUERY(qr->query), (void *) qr,
#if PG_VERSION_NUM >= 130000
					   TS_EXEC_SKIP_NOT,
#else
					   TS_EXEC_EMPTY,
#endif
					   checkcondition_QueryOperand))
		{
			if (ptr->pos > ext->q)
			{
				ext->q = ptr->pos;
				ext->end = ptr;
				lastpos = ptr - doc;
				found = true;
			}
			break;
		}
		ptr++;
	}

	if (!found)
	{
		return false;
	}

	memset(qr->operandData, 0, sizeof(qr->operandData[0]) * qr->length);

	ptr = doc + lastpos;

	/* find lower bound of cover from found upper bound, move down */
	while (ptr >= doc + ext->pos)
	{
		if (qr->map_item_operand != NULL)
		{
			qr->operandData[ptr->data.key.keyn].operandexist = true;
		}
		else
		{
			for (i = 0; i < ptr->data.item.nitem; i++)
			{
				QueryRepresentationOperand *qro =
					QR_GET_OPERAND(qr, ptr->data.item.item[i]);

				qro->operandexist = true;
				WEP_SETPOS(qro->pos, ptr->pos);
				WEP_SETWEIGHT(qro->pos, ptr->wclass);
			}
		}
		if (TS_execute(GETQUERY(qr->query), (void *) qr,
#if PG_VERSION_NUM >= 130000
					   TS_EXEC_EMPTY,
#else
					   TS_EXEC_CALC_NOT,
#endif
					   checkcondition_QueryOperand))
		{
			if (ptr->pos < ext->p)
			{
				ext->begin = ptr;
				ext->p = ptr->pos;
			}
			break;
		}
		ptr--;
	}

	if (ext->p <= ext->q)
	{
		/*
		 * set position for next try to next lexeme after begining of founded
		 * cover
		 */
		ext->pos = (ptr - doc) + 1;
		return true;
	}

	ext->pos++;
	goto restart;
}


static DocRepresentation *
get_docrep_addinfo(bool *check, QueryRepresentation *qr,
				   Datum *addInfo, bool *addInfoIsNull, uint32 *doclen)
{
	QueryItem *item = GETQUERY(qr->query);
	int32 dimt,
		  j,
		  i;
	int len = qr->query->size * 4,
		cur = 0;
	DocRepresentation *doc;
	char *ptrt;

	doc = (DocRepresentation *) palloc(sizeof(DocRepresentation) * len);

	for (i = 0; i < qr->query->size; i++)
	{
		int keyN;
		WordEntryPos post = 0;

		if (item[i].type != QI_VAL)
		{
			continue;
		}

		keyN = qr->map_item_operand[i];
		if (!check[keyN])
		{
			continue;
		}

		/*
		 * entries could be repeated in tsquery, do not visit them twice
		 * or more. Modifying of check array (entryRes) is safe
		 */
		check[keyN] = false;

		if (!addInfoIsNull[keyN])
		{
			dimt = count_pos(VARDATA_ANY(addInfo[keyN]),
							 VARSIZE_ANY_EXHDR(addInfo[keyN]));
			ptrt = (char *) VARDATA_ANY(addInfo[keyN]);
		}
		else
		{
			continue;
		}

		while (cur + dimt >= len)
		{
			len *= 2;
			doc = (DocRepresentation *) repalloc(doc, sizeof(DocRepresentation) * len);
		}

		for (j = 0; j < dimt; j++)
		{
			ptrt = decompress_pos(ptrt, &post);

			doc[cur].data.key.item_first = item + i;
			doc[cur].data.key.keyn = keyN;
			doc[cur].pos = WEP_GETPOS(post);
			doc[cur].wclass = WEP_GETWEIGHT(post);
			cur++;
		}
	}

	*doclen = cur;

	if (cur > 0)
	{
		qsort((void *) doc, cur, sizeof(DocRepresentation), compareDocR);
		return doc;
	}

	pfree(doc);
	return NULL;
}


#define WordECompareQueryItem(e, q, p, i, m) \
	tsCompareString((q) + (i)->distance, (i)->length, \
					(e) + (p)->pos, (p)->len, (m))

/*
 * Returns a pointer to a WordEntry's array corresponding to 'item' from
 * tsvector 't'. 'q' is the TSQuery containing 'item'.
 * Returns NULL if not found.
 */
static WordEntry *
find_wordentry(TSVector t, TSQuery q, QueryOperand *item, int32 *nitem)
{
	WordEntry *StopLow = ARRPTR(t);
	WordEntry *StopHigh = (WordEntry *) STRPTR(t);
	WordEntry *StopMiddle = StopHigh;
	int difference;

	*nitem = 0;

	/* Loop invariant: StopLow <= item < StopHigh */
	while (StopLow < StopHigh)
	{
		StopMiddle = StopLow + (StopHigh - StopLow) / 2;
		difference = WordECompareQueryItem(STRPTR(t), GETOPERAND(q), StopMiddle, item,
										   false);
		if (difference == 0)
		{
			StopHigh = StopMiddle;
			*nitem = 1;
			break;
		}
		else if (difference > 0)
		{
			StopLow = StopMiddle + 1;
		}
		else
		{
			StopHigh = StopMiddle;
		}
	}

	if (item->prefix == true)
	{
		if (StopLow >= StopHigh)
		{
			StopMiddle = StopHigh;
		}

		*nitem = 0;

		while (StopMiddle < (WordEntry *) STRPTR(t) &&
			   WordECompareQueryItem(STRPTR(t), GETOPERAND(q), StopMiddle, item, true) ==
			   0)
		{
			(*nitem)++;
			StopMiddle++;
		}
	}

	return (*nitem > 0) ? StopHigh : NULL;
}


static DocRepresentation *
get_docrep(TSVector txt, QueryRepresentation *qr, uint32 *doclen)
{
	QueryItem *item = GETQUERY(qr->query);
	WordEntry *entry,
			  *firstentry;
	WordEntryPos *post;
	int32 dimt,
		  j,
		  i,
		  nitem;
	int len = qr->query->size * 4,
		cur = 0;
	DocRepresentation *doc;
	char *operand;

	doc = (DocRepresentation *) palloc(sizeof(DocRepresentation) * len);
	operand = GETOPERAND(qr->query);

	for (i = 0; i < qr->query->size; i++)
	{
		QueryOperand *curoperand;

		if (item[i].type != QI_VAL)
		{
			continue;
		}

		curoperand = &item[i].qoperand;

		if (QR_GET_OPERAND(qr, &item[i])->operandexist)
		{
			continue;
		}

		firstentry = entry = find_wordentry(txt, qr->query, curoperand, &nitem);
		if (!entry)
		{
			continue;
		}

		while (entry - firstentry < nitem)
		{
			if (entry->haspos)
			{
				dimt = POSDATALEN(txt, entry);
				post = POSDATAPTR(txt, entry);
			}
			else
			{
				dimt = POSNULL.npos;
				post = POSNULL.pos;
			}

			while (cur + dimt >= len)
			{
				len *= 2;
				doc = (DocRepresentation *) repalloc(doc, sizeof(DocRepresentation) *
													 len);
			}

			for (j = 0; j < dimt; j++)
			{
				if (j == 0)
				{
					int k;

					doc[cur].data.item.nitem = 0;
					doc[cur].data.item.item = (QueryItem **) palloc(
						sizeof(QueryItem *) * qr->query->size);

					for (k = 0; k < qr->query->size; k++)
					{
						QueryOperand *kptr = &item[k].qoperand;
						QueryOperand *iptr = &item[i].qoperand;

						if (k == i ||
							(item[k].type == QI_VAL &&
							 compareQueryOperand(&kptr, &iptr, operand) == 0))
						{
							QueryRepresentationOperand *qro;

							/*
							 * if k == i, we've already checked above that
							 * it's type == Q_VAL
							 */
							doc[cur].data.item.item[doc[cur].data.item.nitem] =
								item + k;
							doc[cur].data.item.nitem++;

							qro = QR_GET_OPERAND(qr, item + k);

							qro->operandexist = true;
							qro->pos = post[j];
						}
					}
				}
				else
				{
					doc[cur].data.item.nitem = doc[cur - 1].data.item.nitem;
					doc[cur].data.item.item = doc[cur - 1].data.item.item;
				}
				doc[cur].pos = WEP_GETPOS(post[j]);
				doc[cur].wclass = WEP_GETWEIGHT(post[j]);
				cur++;
			}

			entry++;
		}
	}

	*doclen = cur;

	if (cur > 0)
	{
		qsort((void *) doc, cur, sizeof(DocRepresentation), compareDocR);
		return doc;
	}

	pfree(doc);
	return NULL;
}


static double
calc_score_docr(float4 *arrdata, DocRepresentation *doc, uint32 doclen,
				QueryRepresentation *qr, int method)
{
	int32 i;
	Extention ext;
	double Wdoc = 0.0;
	double SumDist = 0.0,
		   PrevExtPos = 0.0,
		   CurExtPos = 0.0;
	int NExtent = 0;

	/* Added by SK */
	int *cover_keys = (int *) palloc(0);
	int *cover_lengths = (int *) palloc(0);
	double *cover_ranks = (double *) palloc(0);
	int ncovers = 0;

	MemSet(&ext, 0, sizeof(Extention));
	while (Cover(doc, doclen, qr, &ext))
	{
		double Cpos = 0.0;
		double InvSum = 0.0;
		int nNoise;
		DocRepresentation *ptr = ext.begin;

		/* Added by SK */
		int new_cover_idx = 0;
		int new_cover_key = 0;
		int nitems = 0;

		while (ptr && ptr <= ext.end)
		{
			InvSum += arrdata[ptr->wclass];

			/* SK: Quick and dirty hash key. Hope collisions will be not too frequent. */
			new_cover_key = new_cover_key << 1;

			/* For rum_ts_distance() */
			if (qr->map_item_operand == NULL)
			{
				new_cover_key += (int) (uintptr_t) ptr->data.item.item;
			}
			/* For rum_tsquery_distance() */
			else
			{
				new_cover_key += (int) (uintptr_t) ptr->data.key.item_first;
			}
			ptr++;
		}

		/* Added by SK */
		/* TODO: use binary tree?.. */
		while (new_cover_idx < ncovers)
		{
			if (new_cover_key == cover_keys[new_cover_idx])
			{
				break;
			}
			new_cover_idx++;
		}

		if (new_cover_idx == ncovers)
		{
			cover_keys = (int *) repalloc(cover_keys, sizeof(int) *
										  (ncovers + 1));
			cover_lengths = (int *) repalloc(cover_lengths, sizeof(int) *
											 (ncovers + 1));
			cover_ranks = (double *) repalloc(cover_ranks, sizeof(double) *
											  (ncovers + 1));

			cover_lengths[ncovers] = 0;
			cover_ranks[ncovers] = 0;

			ncovers++;
		}

		cover_keys[new_cover_idx] = new_cover_key;

		/* Compute the number of query terms in the cover */
		for (i = 0; i < qr->length; i++)
		{
			if (qr->operandData[i].operandexist)
			{
				nitems++;
			}
		}

		Cpos = ((double) (ext.end - ext.begin + 1)) / InvSum;

		if (nitems > 0)
		{
			Cpos *= nitems;
		}

		/*
		 * if doc are big enough then ext.q may be equal to ext.p due to limit
		 * of posional information. In this case we approximate number of
		 * noise word as half cover's length
		 */
		nNoise = (ext.q - ext.p) - (ext.end - ext.begin);
		if (nNoise < 0)
		{
			nNoise = (ext.end - ext.begin) / 2;
		}

		/* SK: Wdoc += Cpos / ((double) (1 + nNoise)); */
		cover_lengths[new_cover_idx]++;
		cover_ranks[new_cover_idx] += Cpos / ((double) (1 + nNoise)) /
									  cover_lengths[new_cover_idx] /
									  cover_lengths[new_cover_idx] /
									  1.64493406685;

		CurExtPos = ((double) (ext.q + ext.p)) / 2.0;
		if (NExtent > 0 && CurExtPos > PrevExtPos       /* prevent devision by
		                                                 * zero in a case of
		                                                 * multiple lexize */)
		{
			SumDist += 1.0 / (CurExtPos - PrevExtPos);
		}

		PrevExtPos = CurExtPos;
		NExtent++;
	}

	/* Added by SK */
	for (i = 0; i < ncovers; i++)
	{
		Wdoc += cover_ranks[i];
	}

	if ((method & RANK_NORM_EXTDIST) && NExtent > 0 && SumDist > 0)
	{
		Wdoc /= ((double) NExtent) / SumDist;
	}

	if (method & RANK_NORM_RDIVRPLUS1)
	{
		Wdoc /= (Wdoc + 1);
	}

	pfree(cover_keys);
	pfree(cover_lengths);
	pfree(cover_ranks);

	return (float4) Wdoc;
}


static float4
calc_score_addinfo(float4 *arrdata, bool *check, TSQuery query,
				   int *map_item_operand, Datum *addInfo, bool *addInfoIsNull,
				   int nkeys)
{
	DocRepresentation *doc;
	uint32 doclen = 0;
	double Wdoc = 0.0;
	QueryRepresentation qr;

	qr.query = query;
	qr.map_item_operand = map_item_operand;
	qr.operandData = palloc0(sizeof(qr.operandData[0]) * nkeys);
	qr.length = nkeys;

	doc = get_docrep_addinfo(check, &qr, addInfo, addInfoIsNull, &doclen);
	if (!doc)
	{
		pfree(qr.operandData);
		return 0.0;
	}

	Wdoc = calc_score_docr(arrdata, doc, doclen, &qr, DEF_NORM_METHOD);

	pfree(doc);
	pfree(qr.operandData);

	return (float4) Wdoc;
}


static float4
calc_score(float4 *arrdata, TSVector txt, TSQuery query, int method)
{
	DocRepresentation *doc;
	uint32 len,
		   doclen = 0;
	double Wdoc = 0.0;
	QueryRepresentation qr;

	qr.query = query;
	qr.map_item_operand = NULL;
	qr.operandData = palloc0(sizeof(qr.operandData[0]) * query->size);
	qr.length = query->size;

	doc = get_docrep(txt, &qr, &doclen);
	if (!doc)
	{
		pfree(qr.operandData);
		return 0.0;
	}

	Wdoc = calc_score_docr(arrdata, doc, doclen, &qr, method);

	if ((method & RANK_NORM_LOGLENGTH) && txt->size > 0)
	{
		Wdoc /= log((double) (count_length(txt) + 1));
	}

	if (method & RANK_NORM_LENGTH)
	{
		len = count_length(txt);
		if (len > 0)
		{
			Wdoc /= (double) len;
		}
	}

	if ((method & RANK_NORM_UNIQ) && txt->size > 0)
	{
		Wdoc /= (double) (txt->size);
	}

	if ((method & RANK_NORM_LOGUNIQ) && txt->size > 0)
	{
		Wdoc /= log((double) (txt->size + 1)) / log(2.0);
	}

	pfree(doc);
	pfree(qr.operandData);

	return (float4) Wdoc;
}


/*
 * Calculates distance inside index. Uses additional information with lexemes
 * positions.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_tsquery_distance(PG_FUNCTION_ARGS)
{
	bool *check = (bool *) PG_GETARG_POINTER(0);

	TSQuery query = PG_GETARG_TSQUERY(2);
	int nkeys = PG_GETARG_INT32(3);
	Pointer *extra_data = (Pointer *) PG_GETARG_POINTER(4);
	Datum *addInfo = (Datum *) PG_GETARG_POINTER(8);
	bool *addInfoIsNull = (bool *) PG_GETARG_POINTER(9);
	float8 res;
	int *map_item_operand = (int *) (extra_data[0]);

	res = calc_score_addinfo(weights, check, query, map_item_operand,
							 addInfo, addInfoIsNull, nkeys);

	PG_FREE_IF_COPY(query, 2);
	if (res == 0)
	{
		PG_RETURN_FLOAT8(get_float8_infinity());
	}
	else
	{
		PG_RETURN_FLOAT8(1.0 / res);
	}
}


/*
 * Implementation of <=> operator. Uses default normalization method.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_ts_distance_tt(PG_FUNCTION_ARGS)
{
	TSVector txt = PG_GETARG_TSVECTOR(0);
	TSQuery query = PG_GETARG_TSQUERY(1);
	float4 res;

	res = calc_score(weights, txt, query, DEF_NORM_METHOD);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);
	if (res == 0)
	{
		PG_RETURN_FLOAT4(get_float4_infinity());
	}
	else
	{
		PG_RETURN_FLOAT4(1.0 / res);
	}
}


/*
 * Implementation of <=> operator. Uses specified normalization method.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_ts_distance_ttf(PG_FUNCTION_ARGS)
{
	TSVector txt = PG_GETARG_TSVECTOR(0);
	TSQuery query = PG_GETARG_TSQUERY(1);
	int method = PG_GETARG_INT32(2);
	float4 res;

	res = calc_score(weights, txt, query, method);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);
	if (res == 0)
	{
		PG_RETURN_FLOAT4(get_float4_infinity());
	}
	else
	{
		PG_RETURN_FLOAT4(1.0 / res);
	}
}


static float4
calc_score_parse_opt(TSVector txt, HeapTupleHeader d)
{
	Oid tupType = HeapTupleHeaderGetTypeId(d);
	int32 tupTypmod = HeapTupleHeaderGetTypMod(d);
	TupleDesc tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);
	HeapTupleData tuple;

	TSQuery query;
	int method;
	bool isnull;
	float4 res;

	tuple.t_len = HeapTupleHeaderGetDatumLength(d);
	ItemPointerSetInvalid(&(tuple.t_self));
	tuple.t_tableOid = InvalidOid;
	tuple.t_data = d;

	query = DatumGetTSQuery(fastgetattr(&tuple, 1, tupdesc, &isnull));
	if (isnull)
	{
		ReleaseTupleDesc(tupdesc);
		elog(ERROR, "NULL query value is not allowed");
	}

	method = DatumGetInt32(fastgetattr(&tuple, 2, tupdesc, &isnull));
	if (isnull)
	{
		method = 0;
	}

	res = calc_score(weights, txt, query, method);

	ReleaseTupleDesc(tupdesc);

	return res;
}


/*
 * Implementation of <=> operator. Uses specified normalization method.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_ts_distance_td(PG_FUNCTION_ARGS)
{
	TSVector txt = PG_GETARG_TSVECTOR(0);
	HeapTupleHeader d = PG_GETARG_HEAPTUPLEHEADER(1);
	float4 res;

	res = calc_score_parse_opt(txt, d);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(d, 1);

	if (res == 0)
	{
		PG_RETURN_FLOAT4(get_float4_infinity());
	}
	else
	{
		PG_RETURN_FLOAT4(1.0 / res);
	}
}


/*
 * Calculate score (inverted distance). Uses default normalization method.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_ts_score_tt(PG_FUNCTION_ARGS)
{
	TSVector txt = PG_GETARG_TSVECTOR(0);
	TSQuery query = PG_GETARG_TSQUERY(1);
	float4 res;

	res = calc_score(weights, txt, query, DEF_NORM_METHOD);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);

	PG_RETURN_FLOAT4(res);
}


/*
 * Calculate score (inverted distance). Uses specified normalization method.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_ts_score_ttf(PG_FUNCTION_ARGS)
{
	TSVector txt = PG_GETARG_TSVECTOR(0);
	TSQuery query = PG_GETARG_TSQUERY(1);
	int method = PG_GETARG_INT32(2);
	float4 res;

	res = calc_score(weights, txt, query, method);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);

	PG_RETURN_FLOAT4(res);
}


/*
 * Calculate score (inverted distance). Uses specified normalization method.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_ts_score_td(PG_FUNCTION_ARGS)
{
	TSVector txt = PG_GETARG_TSVECTOR(0);
	HeapTupleHeader d = PG_GETARG_HEAPTUPLEHEADER(1);
	float4 res;

	res = calc_score_parse_opt(txt, d);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(d, 1);

	PG_RETURN_FLOAT4(res);
}


/*
 * Casts tsquery to rum_distance_query type.
 */
PGDLLEXPORT Datum
documentdb_tsquery_to_distance_query(PG_FUNCTION_ARGS)
{
	TSQuery query = PG_GETARG_TSQUERY(0);

	TupleDesc tupdesc;
	HeapTuple htup;
	Datum values[2];
	bool nulls[2];

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
	{
		elog(ERROR, "return type must be a row type");
	}

	tupdesc = BlessTupleDesc(tupdesc);

	MemSet(nulls, 0, sizeof(nulls));
	values[0] = TSQueryGetDatum(query);
	values[1] = Int32GetDatum(DEF_NORM_METHOD);

	htup = heap_form_tuple(tupdesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(htup));
}


/*
 * Specifies additional information type for operator class.
 */
PGDLLEXPORT Datum
documentdb_extended_rum_tsvector_config(PG_FUNCTION_ARGS)
{
	RumConfig *config = (RumConfig *) PG_GETARG_POINTER(0);

	config->addInfoTypeOid = BYTEAOID;
	config->strategyInfo[0].strategy = InvalidStrategy;

	PG_RETURN_VOID();
}


PGDLLEXPORT Datum
documentdb_extended_rum_ts_join_pos(PG_FUNCTION_ARGS)
{
	Datum addInfo1 = PG_GETARG_DATUM(0);
	Datum addInfo2 = PG_GETARG_DATUM(1);
	char *in1 = VARDATA_ANY(addInfo1),
		 *in2 = VARDATA_ANY(addInfo2);
	bytea *result;
	int count1 = count_pos(in1, VARSIZE_ANY_EXHDR(addInfo1)),
		count2 = count_pos(in2, VARSIZE_ANY_EXHDR(addInfo2)),
		countRes = 0;
	int i1 = 0, i2 = 0;
	Size size,
		 size_compressed;
	WordEntryPos pos1 = 0,
				 pos2 = 0,
				 *pos;

	pos = palloc(sizeof(WordEntryPos) * (count1 + count2));

	Assert(count1 > 0 && count2 > 0);

	in1 = decompress_pos(in1, &pos1);
	in2 = decompress_pos(in2, &pos2);

	for (;;)
	{
		if (WEP_GETPOS(pos1) > WEP_GETPOS(pos2))
		{
			pos[countRes++] = pos2;
			i2++;
			if (i2 >= count2)
			{
				break;
			}
			in2 = decompress_pos(in2, &pos2);
		}
		else if (WEP_GETPOS(pos1) < WEP_GETPOS(pos2))
		{
			pos[countRes++] = pos1;
			i1++;
			if (i1 >= count1)
			{
				break;
			}
			in1 = decompress_pos(in1, &pos1);
		}
		else
		{
			pos[countRes++] = pos1;
			i1++;
			i2++;
			if (i1 < count1)
			{
				in1 = decompress_pos(in1, &pos1);
			}
			if (i2 < count2)
			{
				in2 = decompress_pos(in2, &pos2);
			}
			if (i2 >= count2 || i1 >= count1)
			{
				break;
			}
		}
	}

	if (i1 < count1)
	{
		for (;;)
		{
			pos[countRes++] = pos1;
			i1++;
			if (i1 >= count1)
			{
				break;
			}
			in1 = decompress_pos(in1, &pos1);
		}
	}
	else if (i2 < count2)
	{
		for (;;)
		{
			pos[countRes++] = pos2;
			i2++;
			if (i2 >= count2)
			{
				break;
			}
			in2 = decompress_pos(in2, &pos2);
		}
	}

	Assert(countRes <= count1 + count2);

	/*
	 * In some cases compressed positions may take more memory than
	 * uncompressed positions. So allocate memory with a margin.
	 */
	size = VARHDRSZ + 2 * sizeof(WordEntryPos) * countRes;
	result = palloc0(size);

	size_compressed = compress_pos(result->vl_dat, pos, countRes) + VARHDRSZ;
	Assert(size >= size_compressed);
	SET_VARSIZE(result, size_compressed);

	PG_RETURN_BYTEA_P(result);
}

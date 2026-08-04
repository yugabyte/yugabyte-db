/*
 * op function for ltree and lquery
 * Teodor Sigaev <teodor@stack.net>
 * contrib/ltree/lquery_op.c
 */
#include "postgres.h"

#include <ctype.h>

#include "catalog/pg_collation.h"
#include "ltree.h"
#include "miscadmin.h"
#include "utils/array.h"
#include "utils/formatting.h"

PG_FUNCTION_INFO_V1(ltq_regex);
PG_FUNCTION_INFO_V1(ltq_rregex);

PG_FUNCTION_INFO_V1(lt_q_regex);
PG_FUNCTION_INFO_V1(lt_q_rregex);

#define NEXTVAL(x) ( (lquery*)( (char*)(x) + INTALIGN( VARSIZE(x) ) ) )

static char *
getlexeme(char *start, char *end, int *len)
{
	char	   *ptr;

	while (start < end && t_iseq(start, '_'))
		start += pg_mblen_range(start, end);

	ptr = start;
	if (ptr >= end)
		return NULL;

	while (ptr < end && !t_iseq(ptr, '_'))
		ptr += pg_mblen_range(ptr, end);

	*len = ptr - start;
	return start;
}

bool
compare_subnode(ltree_level *t, char *qn, int len, bool prefix, bool ci)
{
	char	   *endt = t->name + t->len;
	char	   *endq = qn + len;
	char	   *tn;
	int			lent,
				lenq;
	bool		isok;

	while ((qn = getlexeme(qn, endq, &lenq)) != NULL)
	{
		tn = t->name;
		isok = false;
		while ((tn = getlexeme(tn, endt, &lent)) != NULL)
		{
			if (ltree_label_match(qn, lenq, tn, lent, prefix, ci))
			{
				isok = true;
				break;
			}
			tn += lent;
		}

		if (!isok)
			return false;
		qn += lenq;
	}

	return true;
}

/*
 * Check if the label matches the predicate string. If 'prefix' is true, then
 * the predicate string is treated as a prefix. If 'ci' is true, then the
 * predicate string is case-insensitive (and locale-aware).
 */
bool
ltree_label_match(const char *pred, size_t pred_len, const char *label,
				  size_t label_len, bool prefix, bool ci)
{
	static pg_locale_t locale = NULL;
	char	   *fpred;			/* casefolded predicate */
	size_t		fpred_len = pred_len;
	char	   *flabel;			/* casefolded label */
	size_t		flabel_len = label_len;
	size_t		len;
	bool		res;

	/* fast path for binary match or binary prefix match */
	if ((pred_len == label_len || (prefix && pred_len < label_len)) &&
		strncmp(pred, label, pred_len) == 0)
		return true;
	else if (!ci)
		return false;

	/*
	 * Slow path for case-insensitive comparison: case fold and then compare.
	 * This path is necessary even if pred_len > label_len, because the byte
	 * lengths may change after casefolding.
	 */
	if (!locale)
		locale = pg_database_locale();

	fpred = palloc(fpred_len + 1);
	len = pg_strfold(fpred, fpred_len + 1, pred, pred_len, locale);
	if (len > fpred_len)
	{
		/* grow buffer if needed and retry */
		fpred_len = len;
		fpred = repalloc(fpred, fpred_len + 1);
		len = pg_strfold(fpred, fpred_len + 1, pred, pred_len, locale);
	}
	Assert(len <= fpred_len);
	fpred_len = len;

	flabel = palloc(flabel_len + 1);
	len = pg_strfold(flabel, flabel_len + 1, label, label_len, locale);
	if (len > flabel_len)
	{
		/* grow buffer if needed and retry */
		flabel_len = len;
		flabel = repalloc(flabel, flabel_len + 1);
		len = pg_strfold(flabel, flabel_len + 1, label, label_len, locale);
	}
	Assert(len <= flabel_len);
	flabel_len = len;

	if ((fpred_len == flabel_len || (prefix && fpred_len < flabel_len)) &&
		strncmp(fpred, flabel, fpred_len) == 0)
		res = true;
	else
		res = false;

	pfree(fpred);
	pfree(flabel);

	return res;
}

/*
 * See if an lquery_level matches an ltree_level
 *
 * This accounts for all flags including LQL_NOT, but does not
 * consider repetition counts.
 */
static bool
checkLevel(lquery_level *curq, ltree_level *curt)
{
	lquery_variant *curvar = LQL_FIRST(curq);
	bool		success;

	success = (curq->flag & LQL_NOT) ? false : true;

	/* numvar == 0 means '*' which matches anything */
	if (curq->numvar == 0)
		return success;

	for (int i = 0; i < curq->numvar; i++)
	{
		bool		prefix = (curvar->flag & LVAR_ANYEND);
		bool		ci = (curvar->flag & LVAR_INCASE);

		if (curvar->flag & LVAR_SUBLEXEME)
		{
			if (compare_subnode(curt, curvar->name, curvar->len, prefix, ci))
				return success;
		}
		else if (ltree_label_match(curvar->name, curvar->len, curt->name,
								   curt->len, prefix, ci))
			return success;

		curvar = LVAR_NEXT(curvar);
	}
	return !success;
}

/*
 * Try to match an lquery (of qlen items) to an ltree (of tlen items)
 */
static bool
checkCond(lquery_level *curq, int qlen,
		  ltree_level *curt, int tlen)
{
	/* Since this function recurses, it could be driven to stack overflow */
	check_stack_depth();

	/* Pathological patterns could take awhile, too */
	CHECK_FOR_INTERRUPTS();

	/* Loop while we have query items to consider */
	while (qlen > 0)
	{
		int			low,
					high;
		lquery_level *nextq;

		/*
		 * Get min and max repetition counts for this query item, dealing with
		 * the backwards-compatibility hack that the low/high fields aren't
		 * meaningful for non-'*' items unless LQL_COUNT is set.
		 */
		if ((curq->flag & LQL_COUNT) || curq->numvar == 0)
			low = curq->low, high = curq->high;
		else
			low = high = 1;

		/*
		 * We may limit "high" to the remaining text length; this avoids
		 * separate tests below.
		 */
		if (high > tlen)
			high = tlen;

		/* Fail if a match of required number of items is impossible */
		if (high < low)
			return false;

		/*
		 * Recursively check the rest of the pattern against each possible
		 * start point following some of this item's match(es).
		 */
		nextq = LQL_NEXT(curq);
		qlen--;

		for (int matchcnt = 0; matchcnt < high; matchcnt++)
		{
			/*
			 * If we've consumed an acceptable number of matches of this item,
			 * and the rest of the pattern matches beginning here, we're good.
			 */
			if (matchcnt >= low && checkCond(nextq, qlen, curt, tlen))
				return true;

			/*
			 * Otherwise, try to match one more text item to this query item.
			 */
			if (!checkLevel(curq, curt))
				return false;

			curt = LEVEL_NEXT(curt);
			tlen--;
		}

		/*
		 * Once we've consumed "high" matches, we can succeed only if the rest
		 * of the pattern matches beginning here.  Loop around (if you prefer,
		 * think of this as tail recursion).
		 */
		curq = nextq;
	}

	/*
	 * Once we're out of query items, we match only if there's no remaining
	 * text either.
	 */
	return (tlen == 0);
}

Datum
ltq_regex(PG_FUNCTION_ARGS)
{
	ltree	   *tree = PG_GETARG_LTREE_P(0);
	lquery	   *query = PG_GETARG_LQUERY_P(1);
	bool		res;

	res = checkCond(LQUERY_FIRST(query), query->numlevel,
					LTREE_FIRST(tree), tree->numlevel);

	PG_FREE_IF_COPY(tree, 0);
	PG_FREE_IF_COPY(query, 1);
	PG_RETURN_BOOL(res);
}

Datum
ltq_rregex(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(DirectFunctionCall2(ltq_regex,
										PG_GETARG_DATUM(1),
										PG_GETARG_DATUM(0)
										));
}

Datum
lt_q_regex(PG_FUNCTION_ARGS)
{
	ltree	   *tree = PG_GETARG_LTREE_P(0);
	ArrayType  *_query = PG_GETARG_ARRAYTYPE_P(1);
	lquery	   *query = (lquery *) ARR_DATA_PTR(_query);
	bool		res = false;
	int			num = ArrayGetNItems(ARR_NDIM(_query), ARR_DIMS(_query));

	if (ARR_NDIM(_query) > 1)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("array must be one-dimensional")));
	if (array_contains_nulls(_query))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("array must not contain nulls")));

	while (num > 0)
	{
		if (DatumGetBool(DirectFunctionCall2(ltq_regex,
											 PointerGetDatum(tree), PointerGetDatum(query))))
		{

			res = true;
			break;
		}
		num--;
		query = NEXTVAL(query);
	}

	PG_FREE_IF_COPY(tree, 0);
	PG_FREE_IF_COPY(_query, 1);
	PG_RETURN_BOOL(res);
}

Datum
lt_q_rregex(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(DirectFunctionCall2(lt_q_regex,
										PG_GETARG_DATUM(1),
										PG_GETARG_DATUM(0)
										));
}

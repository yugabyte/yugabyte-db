/*
 * op function for ltree
 * Teodor Sigaev <teodor@stack.net>
 * contrib/ltree/ltree_op.c
 */
#include "postgres.h"

#include <ctype.h>

#include "access/htup_details.h"
#include "catalog/pg_statistic.h"
#include "ltree.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/selfuncs.h"

PG_MODULE_MAGIC;

/* compare functions */
PG_FUNCTION_INFO_V1(ltree_cmp);
PG_FUNCTION_INFO_V1(ltree_lt);
PG_FUNCTION_INFO_V1(ltree_le);
PG_FUNCTION_INFO_V1(ltree_eq);
PG_FUNCTION_INFO_V1(ltree_ne);
PG_FUNCTION_INFO_V1(ltree_ge);
PG_FUNCTION_INFO_V1(ltree_gt);
PG_FUNCTION_INFO_V1(nlevel);
PG_FUNCTION_INFO_V1(ltree_isparent);
PG_FUNCTION_INFO_V1(ltree_risparent);
PG_FUNCTION_INFO_V1(subltree);
PG_FUNCTION_INFO_V1(subpath);
PG_FUNCTION_INFO_V1(ltree_index);
PG_FUNCTION_INFO_V1(ltree_addltree);
PG_FUNCTION_INFO_V1(ltree_addtext);
PG_FUNCTION_INFO_V1(ltree_textadd);
PG_FUNCTION_INFO_V1(lca);
PG_FUNCTION_INFO_V1(ltree2text);
PG_FUNCTION_INFO_V1(text2ltree);
PG_FUNCTION_INFO_V1(ltreeparentsel);

int
ltree_compare(const ltree *a, const ltree *b)
{
	ltree_level *al = LTREE_FIRST(a);
	ltree_level *bl = LTREE_FIRST(b);
	int			an = a->numlevel;
	int			bn = b->numlevel;

	while (an > 0 && bn > 0)
	{
		int			res;

		if ((res = memcmp(al->name, bl->name, Min(al->len, bl->len))) == 0)
		{
			if (al->len != bl->len)
				return (al->len - bl->len) * 10 * (an + 1);
		}
		else
		{
			if (res < 0)
				res = -1;
			else
				res = 1;
			return res * 10 * (an + 1);
		}

		an--;
		bn--;
		al = LEVEL_NEXT(al);
		bl = LEVEL_NEXT(bl);
	}

	return (a->numlevel - b->numlevel) * 10 * (an + 1);
}

#define RUNCMP						\
ltree *a = PG_GETARG_LTREE_P(0);	\
ltree *b = PG_GETARG_LTREE_P(1);	\
int res = ltree_compare(a,b);		\
PG_FREE_IF_COPY(a,0);				\
PG_FREE_IF_COPY(b,1)

Datum
ltree_cmp(PG_FUNCTION_ARGS)
{
	RUNCMP;
	PG_RETURN_INT32(res);
}

Datum
ltree_lt(PG_FUNCTION_ARGS)
{
	RUNCMP;
	PG_RETURN_BOOL(res < 0);
}

Datum
ltree_le(PG_FUNCTION_ARGS)
{
	RUNCMP;
	PG_RETURN_BOOL(res <= 0);
}

Datum
ltree_eq(PG_FUNCTION_ARGS)
{
	RUNCMP;
	PG_RETURN_BOOL(res == 0);
}

Datum
ltree_ge(PG_FUNCTION_ARGS)
{
	RUNCMP;
	PG_RETURN_BOOL(res >= 0);
}

Datum
ltree_gt(PG_FUNCTION_ARGS)
{
	RUNCMP;
	PG_RETURN_BOOL(res > 0);
}

Datum
ltree_ne(PG_FUNCTION_ARGS)
{
	RUNCMP;
	PG_RETURN_BOOL(res != 0);
}

Datum
nlevel(PG_FUNCTION_ARGS)
{
	ltree	   *a = PG_GETARG_LTREE_P(0);
	int			res = a->numlevel;

	PG_FREE_IF_COPY(a, 0);
	PG_RETURN_INT32(res);
}

bool
inner_isparent(const ltree *c, const ltree *p)
{
	ltree_level *cl = LTREE_FIRST(c);
	ltree_level *pl = LTREE_FIRST(p);
	int			pn = p->numlevel;

	if (pn > c->numlevel)
		return false;

	while (pn > 0)
	{
		if (cl->len != pl->len)
			return false;
		if (memcmp(cl->name, pl->name, cl->len) != 0)
			return false;

		pn--;
		cl = LEVEL_NEXT(cl);
		pl = LEVEL_NEXT(pl);
	}
	return true;
}

Datum
ltree_isparent(PG_FUNCTION_ARGS)
{
	ltree	   *c = PG_GETARG_LTREE_P(1);
	ltree	   *p = PG_GETARG_LTREE_P(0);
	bool		res = inner_isparent(c, p);

	PG_FREE_IF_COPY(c, 1);
	PG_FREE_IF_COPY(p, 0);
	PG_RETURN_BOOL(res);
}

Datum
ltree_risparent(PG_FUNCTION_ARGS)
{
	ltree	   *c = PG_GETARG_LTREE_P(0);
	ltree	   *p = PG_GETARG_LTREE_P(1);
	bool		res = inner_isparent(c, p);

	PG_FREE_IF_COPY(c, 0);
	PG_FREE_IF_COPY(p, 1);
	PG_RETURN_BOOL(res);
}


static ltree *
inner_subltree(ltree *t, int32 startpos, int32 endpos)
{
	char	   *start = NULL,
			   *end = NULL;
	ltree_level *ptr = LTREE_FIRST(t);
	ltree	   *res;
	int			i;

	if (startpos < 0 || endpos < 0 || startpos >= t->numlevel || startpos > endpos)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid positions")));

	if (endpos > t->numlevel)
		endpos = t->numlevel;

	start = end = (char *) ptr;
	for (i = 0; i < endpos; i++)
	{
		if (i == startpos)
			start = (char *) ptr;
		if (i == endpos - 1)
		{
			end = (char *) LEVEL_NEXT(ptr);
			break;
		}
		ptr = LEVEL_NEXT(ptr);
	}

	res = (ltree *) palloc0(LTREE_HDRSIZE + (end - start));
	SET_VARSIZE(res, LTREE_HDRSIZE + (end - start));
	res->numlevel = endpos - startpos;

	memcpy(LTREE_FIRST(res), start, end - start);

	return res;
}

Datum
subltree(PG_FUNCTION_ARGS)
{
	ltree	   *t = PG_GETARG_LTREE_P(0);
	ltree	   *res = inner_subltree(t, PG_GETARG_INT32(1), PG_GETARG_INT32(2));

	PG_FREE_IF_COPY(t, 0);
	PG_RETURN_POINTER(res);
}

Datum
subpath(PG_FUNCTION_ARGS)
{
	ltree	   *t = PG_GETARG_LTREE_P(0);
	int32		start = PG_GETARG_INT32(1);
	int32		len = (fcinfo->nargs == 3) ? PG_GETARG_INT32(2) : 0;
	int32		end;
	ltree	   *res;

	end = start + len;

	if (start < 0)
	{
		start = t->numlevel + start;
		end = start + len;
	}
	if (start < 0)
	{							/* start > t->numlevel */
		start = t->numlevel + start;
		end = start + len;
	}

	if (len < 0)
		end = t->numlevel + len;
	else if (len == 0)
		end = (fcinfo->nargs == 3) ? start : 0xffff;

	res = inner_subltree(t, start, end);

	PG_FREE_IF_COPY(t, 0);
	PG_RETURN_POINTER(res);
}

static ltree *
ltree_concat(ltree *a, ltree *b)
{
	ltree	   *r;
	int			numlevel = (int) a->numlevel + b->numlevel;

	if (numlevel > LTREE_MAX_LEVELS)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("number of ltree levels (%d) exceeds the maximum allowed (%d)",
						numlevel, LTREE_MAX_LEVELS)));

	r = (ltree *) palloc0(VARSIZE(a) + VARSIZE(b) - LTREE_HDRSIZE);
	SET_VARSIZE(r, VARSIZE(a) + VARSIZE(b) - LTREE_HDRSIZE);
	r->numlevel = (uint16) numlevel;

	memcpy(LTREE_FIRST(r), LTREE_FIRST(a), VARSIZE(a) - LTREE_HDRSIZE);
	memcpy(((char *) LTREE_FIRST(r)) + VARSIZE(a) - LTREE_HDRSIZE,
		   LTREE_FIRST(b),
		   VARSIZE(b) - LTREE_HDRSIZE);
	return r;
}

Datum
ltree_addltree(PG_FUNCTION_ARGS)
{
	ltree	   *a = PG_GETARG_LTREE_P(0);
	ltree	   *b = PG_GETARG_LTREE_P(1);
	ltree	   *r;

	r = ltree_concat(a, b);
	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_POINTER(r);
}

Datum
ltree_addtext(PG_FUNCTION_ARGS)
{
	ltree	   *a = PG_GETARG_LTREE_P(0);
	text	   *b = PG_GETARG_TEXT_PP(1);
	char	   *s;
	ltree	   *r,
			   *tmp;

	s = text_to_cstring(b);

	tmp = (ltree *) DatumGetPointer(DirectFunctionCall1(ltree_in,
														PointerGetDatum(s)));

	pfree(s);

	r = ltree_concat(a, tmp);

	pfree(tmp);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_POINTER(r);
}

Datum
ltree_index(PG_FUNCTION_ARGS)
{
	ltree	   *a = PG_GETARG_LTREE_P(0);
	ltree	   *b = PG_GETARG_LTREE_P(1);
	int			start = (fcinfo->nargs == 3) ? PG_GETARG_INT32(2) : 0;
	int			i,
				j;
	ltree_level *startptr,
			   *aptr,
			   *bptr;
	bool		found = false;

	if (start < 0)
	{
		if (-start >= a->numlevel)
			start = 0;
		else
			start = (int) (a->numlevel) + start;
	}

	if (a->numlevel - start < b->numlevel || a->numlevel == 0 || b->numlevel == 0)
	{
		PG_FREE_IF_COPY(a, 0);
		PG_FREE_IF_COPY(b, 1);
		PG_RETURN_INT32(-1);
	}

	startptr = LTREE_FIRST(a);
	for (i = 0; i <= a->numlevel - b->numlevel; i++)
	{
		if (i >= start)
		{
			aptr = startptr;
			bptr = LTREE_FIRST(b);
			for (j = 0; j < b->numlevel; j++)
			{
				if (!(aptr->len == bptr->len && memcmp(aptr->name, bptr->name, aptr->len) == 0))
					break;
				aptr = LEVEL_NEXT(aptr);
				bptr = LEVEL_NEXT(bptr);
			}

			if (j == b->numlevel)
			{
				found = true;
				break;
			}
		}
		startptr = LEVEL_NEXT(startptr);
	}

	if (!found)
		i = -1;

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_INT32(i);
}

Datum
ltree_textadd(PG_FUNCTION_ARGS)
{
	ltree	   *a = PG_GETARG_LTREE_P(1);
	text	   *b = PG_GETARG_TEXT_PP(0);
	char	   *s;
	ltree	   *r,
			   *tmp;

	s = text_to_cstring(b);

	tmp = (ltree *) DatumGetPointer(DirectFunctionCall1(ltree_in,
														PointerGetDatum(s)));

	pfree(s);

	r = ltree_concat(tmp, a);

	pfree(tmp);

	PG_FREE_IF_COPY(a, 1);
	PG_FREE_IF_COPY(b, 0);
	PG_RETURN_POINTER(r);
}

/*
 * Common code for variants of lca(), find longest common ancestor of inputs
 *
 * Returns NULL if there is no common ancestor, ie, the longest common
 * prefix is empty.
 */
ltree *
lca_inner(ltree **a, int len)
{
	int			tmp,
				num,
				i,
				reslen;
	ltree	  **ptr;
	ltree_level *l1,
			   *l2;
	ltree	   *res;

	if (len <= 0)
		return NULL;			/* no inputs? */
	if ((*a)->numlevel == 0)
		return NULL;			/* any empty input means NULL result */

	/* num is the length of the longest common ancestor so far */
	num = (*a)->numlevel - 1;

	/* Compare each additional input to *a */
	ptr = a + 1;
	while (ptr - a < len)
	{
		if ((*ptr)->numlevel == 0)
			return NULL;
		else if ((*ptr)->numlevel == 1)
			num = 0;
		else
		{
			l1 = LTREE_FIRST(*a);
			l2 = LTREE_FIRST(*ptr);
			tmp = Min(num, (*ptr)->numlevel - 1);
			num = 0;
			for (i = 0; i < tmp; i++)
			{
				if (l1->len == l2->len &&
					memcmp(l1->name, l2->name, l1->len) == 0)
					num = i + 1;
				else
					break;
				l1 = LEVEL_NEXT(l1);
				l2 = LEVEL_NEXT(l2);
			}
		}
		ptr++;
	}

	/* Now compute size of result ... */
	reslen = LTREE_HDRSIZE;
	l1 = LTREE_FIRST(*a);
	for (i = 0; i < num; i++)
	{
		reslen += MAXALIGN(l1->len + LEVEL_HDRSIZE);
		l1 = LEVEL_NEXT(l1);
	}

	/* ... and construct it by copying from *a */
	res = (ltree *) palloc0(reslen);
	SET_VARSIZE(res, reslen);
	res->numlevel = num;

	l1 = LTREE_FIRST(*a);
	l2 = LTREE_FIRST(res);

	for (i = 0; i < num; i++)
	{
		memcpy(l2, l1, MAXALIGN(l1->len + LEVEL_HDRSIZE));
		l1 = LEVEL_NEXT(l1);
		l2 = LEVEL_NEXT(l2);
	}

	return res;
}

Datum
lca(PG_FUNCTION_ARGS)
{
	int			i;
	ltree	  **a,
			   *res;

	a = (ltree **) palloc(sizeof(ltree *) * fcinfo->nargs);
	for (i = 0; i < fcinfo->nargs; i++)
		a[i] = PG_GETARG_LTREE_P(i);
	res = lca_inner(a, (int) fcinfo->nargs);
	for (i = 0; i < fcinfo->nargs; i++)
		PG_FREE_IF_COPY(a[i], i);
	pfree(a);

	if (res)
		PG_RETURN_POINTER(res);
	else
		PG_RETURN_NULL();
}

Datum
text2ltree(PG_FUNCTION_ARGS)
{
	text	   *in = PG_GETARG_TEXT_PP(0);
	char	   *s;
	ltree	   *out;

	s = text_to_cstring(in);

	out = (ltree *) DatumGetPointer(DirectFunctionCall1(ltree_in,
														PointerGetDatum(s)));
	pfree(s);
	PG_FREE_IF_COPY(in, 0);
	PG_RETURN_POINTER(out);
}


Datum
ltree2text(PG_FUNCTION_ARGS)
{
	ltree	   *in = PG_GETARG_LTREE_P(0);
	char	   *ptr;
	int			i;
	ltree_level *curlevel;
	text	   *out;

	out = (text *) palloc(VARSIZE(in) + VARHDRSZ);
	ptr = VARDATA(out);
	curlevel = LTREE_FIRST(in);
	for (i = 0; i < in->numlevel; i++)
	{
		if (i != 0)
		{
			*ptr = '.';
			ptr++;
		}
		memcpy(ptr, curlevel->name, curlevel->len);
		ptr += curlevel->len;
		curlevel = LEVEL_NEXT(curlevel);
	}

	SET_VARSIZE(out, ptr - ((char *) out));
	PG_FREE_IF_COPY(in, 0);

	PG_RETURN_POINTER(out);
}


/*
 *	ltreeparentsel - Selectivity of parent relationship for ltree data types.
 *
 * This function is not used anymore, if the ltree extension has been
 * updated to 1.2 or later.
 */
Datum
ltreeparentsel(PG_FUNCTION_ARGS)
{
	PlannerInfo *root = (PlannerInfo *) PG_GETARG_POINTER(0);
	Oid			operator = PG_GETARG_OID(1);
	List	   *args = (List *) PG_GETARG_POINTER(2);
	int			varRelid = PG_GETARG_INT32(3);
	double		selec;

	/* Use generic restriction selectivity logic, with default 0.001. */
	selec = generic_restriction_selectivity(root, operator, InvalidOid,
											args, varRelid,
											0.001);

	PG_RETURN_FLOAT8((float8) selec);
}

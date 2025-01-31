/*
 * in/out function for ltree and lquery
 * Teodor Sigaev <teodor@stack.net>
 * contrib/ltree/ltree_io.c
 */
#include "postgres.h"

#include <ctype.h>

#include "crc32.h"
#include "libpq/pqformat.h"
#include "ltree.h"
#include "utils/memutils.h"


typedef struct
{
	const char *start;
	int			len;			/* length in bytes */
	int			flag;
	int			wlen;			/* length in characters */
} nodeitem;

#define LTPRS_WAITNAME	0
#define LTPRS_WAITDELIM 1

static void finish_nodeitem(nodeitem *lptr, const char *ptr,
							bool is_lquery, int pos);


/*
 * expects a null terminated string
 * returns an ltree
 */
static ltree *
parse_ltree(const char *buf)
{
	const char *ptr;
	nodeitem   *list,
			   *lptr;
	int			num = 0,
				totallen = 0;
	int			state = LTPRS_WAITNAME;
	ltree	   *result;
	ltree_level *curlevel;
	int			charlen;
	int			pos = 1;		/* character position for error messages */

#define UNCHAR ereport(ERROR, \
					   errcode(ERRCODE_SYNTAX_ERROR), \
					   errmsg("ltree syntax error at character %d", \
							  pos))

	ptr = buf;
	while (*ptr)
	{
		charlen = pg_mblen(ptr);
		if (t_iseq(ptr, '.'))
			num++;
		ptr += charlen;
	}

	if (num + 1 > LTREE_MAX_LEVELS)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("number of ltree labels (%d) exceeds the maximum allowed (%d)",
						num + 1, LTREE_MAX_LEVELS)));
	list = lptr = (nodeitem *) palloc(sizeof(nodeitem) * (num + 1));
	ptr = buf;
	while (*ptr)
	{
		charlen = pg_mblen(ptr);

		switch (state)
		{
			case LTPRS_WAITNAME:
				if (ISALNUM(ptr))
				{
					lptr->start = ptr;
					lptr->wlen = 0;
					state = LTPRS_WAITDELIM;
				}
				else
					UNCHAR;
				break;
			case LTPRS_WAITDELIM:
				if (t_iseq(ptr, '.'))
				{
					finish_nodeitem(lptr, ptr, false, pos);
					totallen += MAXALIGN(lptr->len + LEVEL_HDRSIZE);
					lptr++;
					state = LTPRS_WAITNAME;
				}
				else if (!ISALNUM(ptr))
					UNCHAR;
				break;
			default:
				elog(ERROR, "internal error in ltree parser");
		}

		ptr += charlen;
		lptr->wlen++;
		pos++;
	}

	if (state == LTPRS_WAITDELIM)
	{
		finish_nodeitem(lptr, ptr, false, pos);
		totallen += MAXALIGN(lptr->len + LEVEL_HDRSIZE);
		lptr++;
	}
	else if (!(state == LTPRS_WAITNAME && lptr == list))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("ltree syntax error"),
				 errdetail("Unexpected end of input.")));

	result = (ltree *) palloc0(LTREE_HDRSIZE + totallen);
	SET_VARSIZE(result, LTREE_HDRSIZE + totallen);
	result->numlevel = lptr - list;
	curlevel = LTREE_FIRST(result);
	lptr = list;
	while (lptr - list < result->numlevel)
	{
		curlevel->len = (uint16) lptr->len;
		memcpy(curlevel->name, lptr->start, lptr->len);
		curlevel = LEVEL_NEXT(curlevel);
		lptr++;
	}

	pfree(list);
	return result;

#undef UNCHAR
}

/*
 * expects an ltree
 * returns a null terminated string
 */
static char *
deparse_ltree(const ltree *in)
{
	char	   *buf,
			   *ptr;
	int			i;
	ltree_level *curlevel;

	ptr = buf = (char *) palloc(VARSIZE(in));
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

	*ptr = '\0';
	return buf;
}

/*
 * Basic ltree I/O functions
 */
PG_FUNCTION_INFO_V1(ltree_in);
Datum
ltree_in(PG_FUNCTION_ARGS)
{
	char	   *buf = (char *) PG_GETARG_POINTER(0);

	PG_RETURN_POINTER(parse_ltree(buf));
}

PG_FUNCTION_INFO_V1(ltree_out);
Datum
ltree_out(PG_FUNCTION_ARGS)
{
	ltree	   *in = PG_GETARG_LTREE_P(0);

	PG_RETURN_POINTER(deparse_ltree(in));
}

/*
 * ltree type send function
 *
 * The type is sent as text in binary mode, so this is almost the same
 * as the output function, but it's prefixed with a version number so we
 * can change the binary format sent in future if necessary. For now,
 * only version 1 is supported.
 */
PG_FUNCTION_INFO_V1(ltree_send);
Datum
ltree_send(PG_FUNCTION_ARGS)
{
	ltree	   *in = PG_GETARG_LTREE_P(0);
	StringInfoData buf;
	int			version = 1;
	char	   *res = deparse_ltree(in);

	pq_begintypsend(&buf);
	pq_sendint8(&buf, version);
	pq_sendtext(&buf, res, strlen(res));
	pfree(res);

	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*
 * ltree type recv function
 *
 * The type is sent as text in binary mode, so this is almost the same
 * as the input function, but it's prefixed with a version number so we
 * can change the binary format sent in future if necessary. For now,
 * only version 1 is supported.
 */
PG_FUNCTION_INFO_V1(ltree_recv);
Datum
ltree_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	int			version = pq_getmsgint(buf, 1);
	char	   *str;
	int			nbytes;
	ltree	   *res;

	if (version != 1)
		elog(ERROR, "unsupported ltree version number %d", version);

	str = pq_getmsgtext(buf, buf->len - buf->cursor, &nbytes);
	res = parse_ltree(str);
	pfree(str);

	PG_RETURN_POINTER(res);
}


#define LQPRS_WAITLEVEL 0
#define LQPRS_WAITDELIM 1
#define LQPRS_WAITOPEN	2
#define LQPRS_WAITFNUM	3
#define LQPRS_WAITSNUM	4
#define LQPRS_WAITND	5
#define LQPRS_WAITCLOSE 6
#define LQPRS_WAITEND	7
#define LQPRS_WAITVAR	8


#define GETVAR(x) ( *((nodeitem**)LQL_FIRST(x)) )
#define ITEMSIZE	MAXALIGN(LQL_HDRSIZE+sizeof(nodeitem*))
#define NEXTLEV(x) ( (lquery_level*)( ((char*)(x)) + ITEMSIZE) )

/*
 * expects a null terminated string
 * returns an lquery
 */
static lquery *
parse_lquery(const char *buf)
{
	const char *ptr;
	int			num = 0,
				totallen = 0,
				numOR = 0;
	int			state = LQPRS_WAITLEVEL;
	lquery	   *result;
	nodeitem   *lptr = NULL;
	lquery_level *cur,
			   *curqlevel,
			   *tmpql;
	lquery_variant *lrptr = NULL;
	bool		hasnot = false;
	bool		wasbad = false;
	int			charlen;
	int			pos = 1;		/* character position for error messages */

#define UNCHAR ereport(ERROR, \
					   errcode(ERRCODE_SYNTAX_ERROR), \
					   errmsg("lquery syntax error at character %d", \
							  pos))

	ptr = buf;
	while (*ptr)
	{
		charlen = pg_mblen(ptr);

		if (t_iseq(ptr, '.'))
			num++;
		else if (t_iseq(ptr, '|'))
			numOR++;

		ptr += charlen;
	}

	num++;
	if (num > LQUERY_MAX_LEVELS)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("number of lquery items (%d) exceeds the maximum allowed (%d)",
						num, LQUERY_MAX_LEVELS)));
	curqlevel = tmpql = (lquery_level *) palloc0(ITEMSIZE * num);
	ptr = buf;
	while (*ptr)
	{
		charlen = pg_mblen(ptr);

		switch (state)
		{
			case LQPRS_WAITLEVEL:
				if (ISALNUM(ptr))
				{
					GETVAR(curqlevel) = lptr = (nodeitem *) palloc0(sizeof(nodeitem) * (numOR + 1));
					lptr->start = ptr;
					state = LQPRS_WAITDELIM;
					curqlevel->numvar = 1;
				}
				else if (t_iseq(ptr, '!'))
				{
					GETVAR(curqlevel) = lptr = (nodeitem *) palloc0(sizeof(nodeitem) * (numOR + 1));
					lptr->start = ptr + 1;
					lptr->wlen = -1;	/* compensate for counting ! below */
					state = LQPRS_WAITDELIM;
					curqlevel->numvar = 1;
					curqlevel->flag |= LQL_NOT;
					hasnot = true;
				}
				else if (t_iseq(ptr, '*'))
					state = LQPRS_WAITOPEN;
				else
					UNCHAR;
				break;
			case LQPRS_WAITVAR:
				if (ISALNUM(ptr))
				{
					lptr++;
					lptr->start = ptr;
					state = LQPRS_WAITDELIM;
					curqlevel->numvar++;
				}
				else
					UNCHAR;
				break;
			case LQPRS_WAITDELIM:
				if (t_iseq(ptr, '@'))
				{
					lptr->flag |= LVAR_INCASE;
					curqlevel->flag |= LVAR_INCASE;
				}
				else if (t_iseq(ptr, '*'))
				{
					lptr->flag |= LVAR_ANYEND;
					curqlevel->flag |= LVAR_ANYEND;
				}
				else if (t_iseq(ptr, '%'))
				{
					lptr->flag |= LVAR_SUBLEXEME;
					curqlevel->flag |= LVAR_SUBLEXEME;
				}
				else if (t_iseq(ptr, '|'))
				{
					finish_nodeitem(lptr, ptr, true, pos);
					state = LQPRS_WAITVAR;
				}
				else if (t_iseq(ptr, '{'))
				{
					finish_nodeitem(lptr, ptr, true, pos);
					curqlevel->flag |= LQL_COUNT;
					state = LQPRS_WAITFNUM;
				}
				else if (t_iseq(ptr, '.'))
				{
					finish_nodeitem(lptr, ptr, true, pos);
					state = LQPRS_WAITLEVEL;
					curqlevel = NEXTLEV(curqlevel);
				}
				else if (ISALNUM(ptr))
				{
					/* disallow more chars after a flag */
					if (lptr->flag)
						UNCHAR;
				}
				else
					UNCHAR;
				break;
			case LQPRS_WAITOPEN:
				if (t_iseq(ptr, '{'))
					state = LQPRS_WAITFNUM;
				else if (t_iseq(ptr, '.'))
				{
					/* We only get here for '*', so these are correct defaults */
					curqlevel->low = 0;
					curqlevel->high = LTREE_MAX_LEVELS;
					curqlevel = NEXTLEV(curqlevel);
					state = LQPRS_WAITLEVEL;
				}
				else
					UNCHAR;
				break;
			case LQPRS_WAITFNUM:
				if (t_iseq(ptr, ','))
					state = LQPRS_WAITSNUM;
				else if (t_isdigit(ptr))
				{
					int			low = atoi(ptr);

					if (low < 0 || low > LTREE_MAX_LEVELS)
						ereport(ERROR,
								(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
								 errmsg("lquery syntax error"),
								 errdetail("Low limit (%d) exceeds the maximum allowed (%d), at character %d.",
										   low, LTREE_MAX_LEVELS, pos)));

					curqlevel->low = (uint16) low;
					state = LQPRS_WAITND;
				}
				else
					UNCHAR;
				break;
			case LQPRS_WAITSNUM:
				if (t_isdigit(ptr))
				{
					int			high = atoi(ptr);

					if (high < 0 || high > LTREE_MAX_LEVELS)
						ereport(ERROR,
								(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
								 errmsg("lquery syntax error"),
								 errdetail("High limit (%d) exceeds the maximum allowed (%d), at character %d.",
										   high, LTREE_MAX_LEVELS, pos)));
					else if (curqlevel->low > high)
						ereport(ERROR,
								(errcode(ERRCODE_SYNTAX_ERROR),
								 errmsg("lquery syntax error"),
								 errdetail("Low limit (%d) is greater than high limit (%d), at character %d.",
										   curqlevel->low, high, pos)));

					curqlevel->high = (uint16) high;
					state = LQPRS_WAITCLOSE;
				}
				else if (t_iseq(ptr, '}'))
				{
					curqlevel->high = LTREE_MAX_LEVELS;
					state = LQPRS_WAITEND;
				}
				else
					UNCHAR;
				break;
			case LQPRS_WAITCLOSE:
				if (t_iseq(ptr, '}'))
					state = LQPRS_WAITEND;
				else if (!t_isdigit(ptr))
					UNCHAR;
				break;
			case LQPRS_WAITND:
				if (t_iseq(ptr, '}'))
				{
					curqlevel->high = curqlevel->low;
					state = LQPRS_WAITEND;
				}
				else if (t_iseq(ptr, ','))
					state = LQPRS_WAITSNUM;
				else if (!t_isdigit(ptr))
					UNCHAR;
				break;
			case LQPRS_WAITEND:
				if (t_iseq(ptr, '.'))
				{
					state = LQPRS_WAITLEVEL;
					curqlevel = NEXTLEV(curqlevel);
				}
				else
					UNCHAR;
				break;
			default:
				elog(ERROR, "internal error in lquery parser");
		}

		ptr += charlen;
		if (state == LQPRS_WAITDELIM)
			lptr->wlen++;
		pos++;
	}

	if (state == LQPRS_WAITDELIM)
		finish_nodeitem(lptr, ptr, true, pos);
	else if (state == LQPRS_WAITOPEN)
		curqlevel->high = LTREE_MAX_LEVELS;
	else if (state != LQPRS_WAITEND)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("lquery syntax error"),
				 errdetail("Unexpected end of input.")));

	curqlevel = tmpql;
	totallen = LQUERY_HDRSIZE;
	while ((char *) curqlevel - (char *) tmpql < num * ITEMSIZE)
	{
		totallen += LQL_HDRSIZE;
		if (curqlevel->numvar)
		{
			lptr = GETVAR(curqlevel);
			while (lptr - GETVAR(curqlevel) < curqlevel->numvar)
			{
				totallen += MAXALIGN(LVAR_HDRSIZE + lptr->len);
				lptr++;
			}
		}
		curqlevel = NEXTLEV(curqlevel);
	}

	result = (lquery *) palloc0(totallen);
	SET_VARSIZE(result, totallen);
	result->numlevel = num;
	result->firstgood = 0;
	result->flag = 0;
	if (hasnot)
		result->flag |= LQUERY_HASNOT;
	cur = LQUERY_FIRST(result);
	curqlevel = tmpql;
	while ((char *) curqlevel - (char *) tmpql < num * ITEMSIZE)
	{
		memcpy(cur, curqlevel, LQL_HDRSIZE);
		cur->totallen = LQL_HDRSIZE;
		if (curqlevel->numvar)
		{
			lrptr = LQL_FIRST(cur);
			lptr = GETVAR(curqlevel);
			while (lptr - GETVAR(curqlevel) < curqlevel->numvar)
			{
				cur->totallen += MAXALIGN(LVAR_HDRSIZE + lptr->len);
				lrptr->len = lptr->len;
				lrptr->flag = lptr->flag;
				lrptr->val = ltree_crc32_sz(lptr->start, lptr->len);
				memcpy(lrptr->name, lptr->start, lptr->len);
				lptr++;
				lrptr = LVAR_NEXT(lrptr);
			}
			pfree(GETVAR(curqlevel));
			if (cur->numvar > 1 || cur->flag != 0)
			{
				/* Not a simple match */
				wasbad = true;
			}
			else if (wasbad == false)
			{
				/* count leading simple matches */
				(result->firstgood)++;
			}
		}
		else
		{
			/* '*', so this isn't a simple match */
			wasbad = true;
		}
		curqlevel = NEXTLEV(curqlevel);
		cur = LQL_NEXT(cur);
	}

	pfree(tmpql);
	return result;

#undef UNCHAR
}

/*
 * Close out parsing an ltree or lquery nodeitem:
 * compute the correct length, and complain if it's not OK
 */
static void
finish_nodeitem(nodeitem *lptr, const char *ptr, bool is_lquery, int pos)
{
	if (is_lquery)
	{
		/*
		 * Back up over any flag characters, and discount them from length and
		 * position.
		 */
		while (ptr > lptr->start && strchr("@*%", ptr[-1]) != NULL)
		{
			ptr--;
			lptr->wlen--;
			pos--;
		}
	}

	/* Now compute the byte length, which we weren't tracking before. */
	lptr->len = ptr - lptr->start;

	/* Complain if it's empty or too long */
	if (lptr->len == 0)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 is_lquery ?
				 errmsg("lquery syntax error at character %d", pos) :
				 errmsg("ltree syntax error at character %d", pos),
				 errdetail("Empty labels are not allowed.")));
	if (lptr->wlen > LTREE_LABEL_MAX_CHARS)
		ereport(ERROR,
				(errcode(ERRCODE_NAME_TOO_LONG),
				 errmsg("label string is too long"),
				 errdetail("Label length is %d, must be at most %d, at character %d.",
						   lptr->wlen, LTREE_LABEL_MAX_CHARS, pos)));
}

/*
 * expects an lquery
 * returns a null terminated string
 */
static char *
deparse_lquery(const lquery *in)
{
	char	   *buf,
			   *ptr;
	int			i,
				j,
				totallen = 1;
	lquery_level *curqlevel;
	lquery_variant *curtlevel;

	curqlevel = LQUERY_FIRST(in);
	for (i = 0; i < in->numlevel; i++)
	{
		totallen++;
		if (curqlevel->numvar)
		{
			totallen += 1 + (curqlevel->numvar * 4) + curqlevel->totallen;
			if (curqlevel->flag & LQL_COUNT)
				totallen += 2 * 11 + 3;
		}
		else
			totallen += 2 * 11 + 4;
		curqlevel = LQL_NEXT(curqlevel);
	}

	ptr = buf = (char *) palloc(totallen);
	curqlevel = LQUERY_FIRST(in);
	for (i = 0; i < in->numlevel; i++)
	{
		if (i != 0)
		{
			*ptr = '.';
			ptr++;
		}
		if (curqlevel->numvar)
		{
			if (curqlevel->flag & LQL_NOT)
			{
				*ptr = '!';
				ptr++;
			}
			curtlevel = LQL_FIRST(curqlevel);
			for (j = 0; j < curqlevel->numvar; j++)
			{
				if (j != 0)
				{
					*ptr = '|';
					ptr++;
				}
				memcpy(ptr, curtlevel->name, curtlevel->len);
				ptr += curtlevel->len;
				if ((curtlevel->flag & LVAR_SUBLEXEME))
				{
					*ptr = '%';
					ptr++;
				}
				if ((curtlevel->flag & LVAR_INCASE))
				{
					*ptr = '@';
					ptr++;
				}
				if ((curtlevel->flag & LVAR_ANYEND))
				{
					*ptr = '*';
					ptr++;
				}
				curtlevel = LVAR_NEXT(curtlevel);
			}
		}
		else
		{
			*ptr = '*';
			ptr++;
		}

		if ((curqlevel->flag & LQL_COUNT) || curqlevel->numvar == 0)
		{
			if (curqlevel->low == curqlevel->high)
			{
				sprintf(ptr, "{%d}", curqlevel->low);
			}
			else if (curqlevel->low == 0)
			{
				if (curqlevel->high == LTREE_MAX_LEVELS)
				{
					if (curqlevel->numvar == 0)
					{
						/* This is default for '*', so print nothing */
						*ptr = '\0';
					}
					else
						sprintf(ptr, "{,}");
				}
				else
					sprintf(ptr, "{,%d}", curqlevel->high);
			}
			else if (curqlevel->high == LTREE_MAX_LEVELS)
			{
				sprintf(ptr, "{%d,}", curqlevel->low);
			}
			else
				sprintf(ptr, "{%d,%d}", curqlevel->low, curqlevel->high);
			ptr = strchr(ptr, '\0');
		}

		curqlevel = LQL_NEXT(curqlevel);
	}

	*ptr = '\0';
	return buf;
}

/*
 * Basic lquery I/O functions
 */
PG_FUNCTION_INFO_V1(lquery_in);
Datum
lquery_in(PG_FUNCTION_ARGS)
{
	char	   *buf = (char *) PG_GETARG_POINTER(0);

	PG_RETURN_POINTER(parse_lquery(buf));
}

PG_FUNCTION_INFO_V1(lquery_out);
Datum
lquery_out(PG_FUNCTION_ARGS)
{
	lquery	   *in = PG_GETARG_LQUERY_P(0);

	PG_RETURN_POINTER(deparse_lquery(in));
}

/*
 * lquery type send function
 *
 * The type is sent as text in binary mode, so this is almost the same
 * as the output function, but it's prefixed with a version number so we
 * can change the binary format sent in future if necessary. For now,
 * only version 1 is supported.
 */
PG_FUNCTION_INFO_V1(lquery_send);
Datum
lquery_send(PG_FUNCTION_ARGS)
{
	lquery	   *in = PG_GETARG_LQUERY_P(0);
	StringInfoData buf;
	int			version = 1;
	char	   *res = deparse_lquery(in);

	pq_begintypsend(&buf);
	pq_sendint8(&buf, version);
	pq_sendtext(&buf, res, strlen(res));
	pfree(res);

	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*
 * lquery type recv function
 *
 * The type is sent as text in binary mode, so this is almost the same
 * as the input function, but it's prefixed with a version number so we
 * can change the binary format sent in future if necessary. For now,
 * only version 1 is supported.
 */
PG_FUNCTION_INFO_V1(lquery_recv);
Datum
lquery_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	int			version = pq_getmsgint(buf, 1);
	char	   *str;
	int			nbytes;
	lquery	   *res;

	if (version != 1)
		elog(ERROR, "unsupported lquery version number %d", version);

	str = pq_getmsgtext(buf, buf->len - buf->cursor, &nbytes);
	res = parse_lquery(str);
	pfree(str);

	PG_RETURN_POINTER(res);
}

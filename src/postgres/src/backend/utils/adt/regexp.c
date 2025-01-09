/*-------------------------------------------------------------------------
 *
 * regexp.c
 *	  Postgres' interface to the regular expression package.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/regexp.c
 *
 *		Alistair Crooks added the code for the regex caching
 *		agc - cached the regular expressions used - there's a good chance
 *		that we'll get a hit, so this saves a compile step for every
 *		attempted match. I haven't actually measured the speed improvement,
 *		but it `looks' a lot quicker visually when watching regression
 *		test output.
 *
 *		agc - incorporated Keith Bostic's Berkeley regex code into
 *		the tree for all ports. To distinguish this regex code from any that
 *		is existent on a platform, I've prepended the string "pg_" to
 *		the functions regcomp, regerror, regexec and regfree.
 *		Fixed a bug that was originally a typo by me, where `i' was used
 *		instead of `oldest' when compiling regular expressions - benign
 *		results mostly, although occasionally it bit you...
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_type.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "regex/regex.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/varlena.h"

/* YB includes. */
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/ybc_pggate.h"

#define PG_GETARG_TEXT_PP_IF_EXISTS(_n) \
	(PG_NARGS() > (_n) ? PG_GETARG_TEXT_PP(_n) : NULL)


/* all the options of interest for regex functions */
typedef struct pg_re_flags
{
	int			cflags;			/* compile flags for Spencer's regex code */
	bool		glob;			/* do it globally (for each occurrence) */
} pg_re_flags;

/* cross-call state for regexp_match and regexp_split functions */
typedef struct regexp_matches_ctx
{
	text	   *orig_str;		/* data string in original TEXT form */
	int			nmatches;		/* number of places where pattern matched */
	int			npatterns;		/* number of capturing subpatterns */
	/* We store start char index and end+1 char index for each match */
	/* so the number of entries in match_locs is nmatches * npatterns * 2 */
	int		   *match_locs;		/* 0-based character indexes */
	int			next_match;		/* 0-based index of next match to process */
	/* workspace for build_regexp_match_result() */
	Datum	   *elems;			/* has npatterns elements */
	bool	   *nulls;			/* has npatterns elements */
	pg_wchar   *wide_str;		/* wide-char version of original string */
	char	   *conv_buf;		/* conversion buffer, if needed */
	int			conv_bufsiz;	/* size thereof */
} regexp_matches_ctx;

/*
 * We cache precompiled regular expressions using a "self organizing list"
 * structure, in which recently-used items tend to be near the front.
 * Whenever we use an entry, it's moved up to the front of the list.
 * Over time, an item's average position corresponds to its frequency of use.
 *
 * When we first create an entry, it's inserted at the front of
 * the array, dropping the entry at the end of the array if necessary to
 * make room.  (This might seem to be weighting the new entry too heavily,
 * but if we insert new entries further back, we'll be unable to adjust to
 * a sudden shift in the query mix where we are presented with MAX_CACHED_RES
 * never-before-seen items used circularly.  We ought to be able to handle
 * that case, so we have to insert at the front.)
 *
 * Knuth mentions a variant strategy in which a used item is moved up just
 * one place in the list.  Although he says this uses fewer comparisons on
 * average, it seems not to adapt very well to the situation where you have
 * both some reusable patterns and a steady stream of non-reusable patterns.
 * A reusable pattern that isn't used at least as often as non-reusable
 * patterns are seen will "fail to keep up" and will drop off the end of the
 * cache.  With move-to-front, a reusable pattern is guaranteed to stay in
 * the cache as long as it's used at least once in every MAX_CACHED_RES uses.
 */

/* this is the maximum number of cached regular expressions */
#ifndef MAX_CACHED_RES
#define MAX_CACHED_RES	32
#endif

/* this structure describes one cached regular expression */
typedef struct cached_re_str
{
	char	   *cre_pat;		/* original RE (not null terminated!) */
	int			cre_pat_len;	/* length of original RE, in bytes */
	int			cre_flags;		/* compile flags: extended,icase etc */
	Oid			cre_collation;	/* collation to use */
	regex_t		cre_re;			/* the compiled regular expression */
} cached_re_str;

/*
 * YB: A single static cache works in Postgres, but is not safe in a
 * multi-threaded environment when we pushdown regex filters to the tserver.
 * The following infrastructure is necessary to create a thread local cache
 * for regex compilation.
 */
typedef struct YbReCacheInfo
{
	int *num;
	cached_re_str *array;
} YbReCacheInfo;

static void YbFreeRe(cached_re_str *re)
{
	pg_regfree(&re->cre_re);
	free(re->cre_pat);
}

static void
YbFreeReCache(YBCPgThreadLocalRegexpCache *cache)
{
	Assert(cache && cache->array);
	cached_re_str *re = cache->array;
	for (cached_re_str *re_end = re + cache->num; re != re_end; ++re)
		YbFreeRe(re);
}

static YbReCacheInfo
YbGetReCacheInfo()
{
	if (IsMultiThreadedMode())
	{
		YBCPgThreadLocalRegexpCache* cache = YBCPgGetThreadLocalRegexpCache();
		if (!cache)
		{
			cache = YBCPgInitThreadLocalRegexpCache((sizeof(cached_re_str) *
													 MAX_CACHED_RES),
													&YbFreeReCache);
			Assert(cache && cache->array);
		}

		return (YbReCacheInfo){
			.num = &cache->num,
			.array = (cached_re_str *) cache->array};
	}

	static int	num_res = 0;		/* # of cached re's */
	static cached_re_str re_array[MAX_CACHED_RES];	/* cached re's */

	return (YbReCacheInfo) {.num = &num_res, .array = re_array};
}

/* Local functions */
static regexp_matches_ctx *setup_regexp_matches(text *orig_str, text *pattern,
												pg_re_flags *flags,
												int start_search,
												Oid collation,
												bool use_subpatterns,
												bool ignore_degenerate,
												bool fetching_unmatched);
static ArrayType *build_regexp_match_result(regexp_matches_ctx *matchctx);
static Datum build_regexp_split_result(regexp_matches_ctx *splitctx);


/*
 * RE_compile_and_cache - compile a RE, caching if possible
 *
 * Returns regex_t *
 *
 *	text_re --- the pattern, expressed as a TEXT object
 *	cflags --- compile options for the pattern
 *	collation --- collation to use for LC_CTYPE-dependent behavior
 *
 * Pattern is given in the database encoding.  We internally convert to
 * an array of pg_wchar, which is what Spencer's regex package wants.
 */
regex_t *
RE_compile_and_cache(text *text_re, int cflags, Oid collation)
{
	int			text_re_len = VARSIZE_ANY_EXHDR(text_re);
	char	   *text_re_val = VARDATA_ANY(text_re);
	pg_wchar   *pattern;
	int			pattern_len;
	int			i;
	int			regcomp_result;
	cached_re_str re_temp;
	char		errMsg[100];

	YbReCacheInfo yb_re_cache_info = YbGetReCacheInfo();
	int *num_res = yb_re_cache_info.num;
	cached_re_str *re_array = yb_re_cache_info.array;

	/*
	 * Look for a match among previously compiled REs.  Since the data
	 * structure is self-organizing with most-used entries at the front, our
	 * search strategy can just be to scan from the front.
	 */
	for (i = 0; i < *num_res; i++)
	{
		if (re_array[i].cre_pat_len == text_re_len &&
			re_array[i].cre_flags == cflags &&
			re_array[i].cre_collation == collation &&
			memcmp(re_array[i].cre_pat, text_re_val, text_re_len) == 0)
		{
			/*
			 * Found a match; move it to front if not there already.
			 */
			if (i > 0)
			{
				re_temp = re_array[i];
				memmove(&re_array[1], &re_array[0], i * sizeof(cached_re_str));
				re_array[0] = re_temp;
			}

			return &re_array[0].cre_re;
		}
	}

	/*
	 * Couldn't find it, so try to compile the new RE.  To avoid leaking
	 * resources on failure, we build into the re_temp local.
	 */

	/* Convert pattern string to wide characters */
	pattern = (pg_wchar *) palloc((text_re_len + 1) * sizeof(pg_wchar));
	pattern_len = pg_mb2wchar_with_len(text_re_val,
									   pattern,
									   text_re_len);

	regcomp_result = pg_regcomp(&re_temp.cre_re,
								pattern,
								pattern_len,
								cflags,
								collation);

	pfree(pattern);

	if (regcomp_result != REG_OKAY)
	{
		/* re didn't compile (no need for pg_regfree, if so) */

		/*
		 * Here and in other places in this file, do CHECK_FOR_INTERRUPTS
		 * before reporting a regex error.  This is so that if the regex
		 * library aborts and returns REG_CANCEL, we don't print an error
		 * message that implies the regex was invalid.
		 */
		CHECK_FOR_INTERRUPTS();

		pg_regerror(regcomp_result, &re_temp.cre_re, errMsg, sizeof(errMsg));
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
				 errmsg("invalid regular expression: %s", errMsg)));
	}

	/*
	 * We use malloc/free for the cre_pat field because the storage has to
	 * persist across transactions, and because we want to get control back on
	 * out-of-memory.  The Max() is because some malloc implementations return
	 * NULL for malloc(0).
	 */
	re_temp.cre_pat = malloc(Max(text_re_len, 1));
	if (re_temp.cre_pat == NULL)
	{
		pg_regfree(&re_temp.cre_re);
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory")));
	}
	memcpy(re_temp.cre_pat, text_re_val, text_re_len);
	re_temp.cre_pat_len = text_re_len;
	re_temp.cre_flags = cflags;
	re_temp.cre_collation = collation;

	/*
	 * Okay, we have a valid new item in re_temp; insert it into the storage
	 * array.  Discard last entry if needed.
	 */
	if (*num_res >= MAX_CACHED_RES)
	{
		--*num_res;
		Assert(*num_res < MAX_CACHED_RES);
		YbFreeRe(&re_array[*num_res]);
	}

	if (*num_res > 0)
		memmove(&re_array[1], &re_array[0], *num_res * sizeof(cached_re_str));

	re_array[0] = re_temp;
	(*num_res)++;

	return &re_array[0].cre_re;
}

/*
 * RE_wchar_execute - execute a RE on pg_wchar data
 *
 * Returns true on match, false on no match
 *
 *	re --- the compiled pattern as returned by RE_compile_and_cache
 *	data --- the data to match against (need not be null-terminated)
 *	data_len --- the length of the data string
 *	start_search -- the offset in the data to start searching
 *	nmatch, pmatch	--- optional return area for match details
 *
 * Data is given as array of pg_wchar which is what Spencer's regex package
 * wants.
 */
static bool
RE_wchar_execute(regex_t *re, pg_wchar *data, int data_len,
				 int start_search, int nmatch, regmatch_t *pmatch)
{
	int			regexec_result;
	char		errMsg[100];

	/* Perform RE match and return result */
	regexec_result = pg_regexec(re,
								data,
								data_len,
								start_search,
								NULL,	/* no details */
								nmatch,
								pmatch,
								0);

	if (regexec_result != REG_OKAY && regexec_result != REG_NOMATCH)
	{
		/* re failed??? */
		CHECK_FOR_INTERRUPTS();
		pg_regerror(regexec_result, re, errMsg, sizeof(errMsg));
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
				 errmsg("regular expression failed: %s", errMsg)));
	}

	return (regexec_result == REG_OKAY);
}

/*
 * RE_execute - execute a RE
 *
 * Returns true on match, false on no match
 *
 *	re --- the compiled pattern as returned by RE_compile_and_cache
 *	dat --- the data to match against (need not be null-terminated)
 *	dat_len --- the length of the data string
 *	nmatch, pmatch	--- optional return area for match details
 *
 * Data is given in the database encoding.  We internally
 * convert to array of pg_wchar which is what Spencer's regex package wants.
 */
static bool
RE_execute(regex_t *re, char *dat, int dat_len,
		   int nmatch, regmatch_t *pmatch)
{
	pg_wchar   *data;
	int			data_len;
	bool		match;

	/* Convert data string to wide characters */
	data = (pg_wchar *) palloc((dat_len + 1) * sizeof(pg_wchar));
	data_len = pg_mb2wchar_with_len(dat, data, dat_len);

	/* Perform RE match and return result */
	match = RE_wchar_execute(re, data, data_len, 0, nmatch, pmatch);

	pfree(data);
	return match;
}

/*
 * RE_compile_and_execute - compile and execute a RE
 *
 * Returns true on match, false on no match
 *
 *	text_re --- the pattern, expressed as a TEXT object
 *	dat --- the data to match against (need not be null-terminated)
 *	dat_len --- the length of the data string
 *	cflags --- compile options for the pattern
 *	collation --- collation to use for LC_CTYPE-dependent behavior
 *	nmatch, pmatch	--- optional return area for match details
 *
 * Both pattern and data are given in the database encoding.  We internally
 * convert to array of pg_wchar which is what Spencer's regex package wants.
 */
bool
RE_compile_and_execute(text *text_re, char *dat, int dat_len,
					   int cflags, Oid collation,
					   int nmatch, regmatch_t *pmatch)
{
	regex_t    *re;

	/* Use REG_NOSUB if caller does not want sub-match details */
	if (nmatch < 2)
		cflags |= REG_NOSUB;

	/* Compile RE */
	re = RE_compile_and_cache(text_re, cflags, collation);

	return RE_execute(re, dat, dat_len, nmatch, pmatch);
}


/*
 * parse_re_flags - parse the options argument of regexp_match and friends
 *
 *	flags --- output argument, filled with desired options
 *	opts --- TEXT object, or NULL for defaults
 *
 * This accepts all the options allowed by any of the callers; callers that
 * don't want some have to reject them after the fact.
 */
static void
parse_re_flags(pg_re_flags *flags, text *opts)
{
	/* regex flavor is always folded into the compile flags */
	flags->cflags = REG_ADVANCED;
	flags->glob = false;

	if (opts)
	{
		char	   *opt_p = VARDATA_ANY(opts);
		int			opt_len = VARSIZE_ANY_EXHDR(opts);
		int			i;

		for (i = 0; i < opt_len; i++)
		{
			switch (opt_p[i])
			{
				case 'g':
					flags->glob = true;
					break;
				case 'b':		/* BREs (but why???) */
					flags->cflags &= ~(REG_ADVANCED | REG_EXTENDED | REG_QUOTE);
					break;
				case 'c':		/* case sensitive */
					flags->cflags &= ~REG_ICASE;
					break;
				case 'e':		/* plain EREs */
					flags->cflags |= REG_EXTENDED;
					flags->cflags &= ~(REG_ADVANCED | REG_QUOTE);
					break;
				case 'i':		/* case insensitive */
					flags->cflags |= REG_ICASE;
					break;
				case 'm':		/* Perloid synonym for n */
				case 'n':		/* \n affects ^ $ . [^ */
					flags->cflags |= REG_NEWLINE;
					break;
				case 'p':		/* ~Perl, \n affects . [^ */
					flags->cflags |= REG_NLSTOP;
					flags->cflags &= ~REG_NLANCH;
					break;
				case 'q':		/* literal string */
					flags->cflags |= REG_QUOTE;
					flags->cflags &= ~(REG_ADVANCED | REG_EXTENDED);
					break;
				case 's':		/* single line, \n ordinary */
					flags->cflags &= ~REG_NEWLINE;
					break;
				case 't':		/* tight syntax */
					flags->cflags &= ~REG_EXPANDED;
					break;
				case 'w':		/* weird, \n affects ^ $ only */
					flags->cflags &= ~REG_NLSTOP;
					flags->cflags |= REG_NLANCH;
					break;
				case 'x':		/* expanded syntax */
					flags->cflags |= REG_EXPANDED;
					break;
				default:
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("invalid regular expression option: \"%.*s\"",
									pg_mblen(opt_p + i), opt_p + i)));
					break;
			}
		}
	}
}


/*
 *	interface routines called by the function manager
 */

Datum
nameregexeq(PG_FUNCTION_ARGS)
{
	Name		n = PG_GETARG_NAME(0);
	text	   *p = PG_GETARG_TEXT_PP(1);

	PG_RETURN_BOOL(RE_compile_and_execute(p,
										  NameStr(*n),
										  strlen(NameStr(*n)),
										  REG_ADVANCED,
										  PG_GET_COLLATION(),
										  0, NULL));
}

Datum
nameregexne(PG_FUNCTION_ARGS)
{
	Name		n = PG_GETARG_NAME(0);
	text	   *p = PG_GETARG_TEXT_PP(1);

	PG_RETURN_BOOL(!RE_compile_and_execute(p,
										   NameStr(*n),
										   strlen(NameStr(*n)),
										   REG_ADVANCED,
										   PG_GET_COLLATION(),
										   0, NULL));
}

Datum
textregexeq(PG_FUNCTION_ARGS)
{
	text	   *s = PG_GETARG_TEXT_PP(0);
	text	   *p = PG_GETARG_TEXT_PP(1);

	PG_RETURN_BOOL(RE_compile_and_execute(p,
										  VARDATA_ANY(s),
										  VARSIZE_ANY_EXHDR(s),
										  REG_ADVANCED,
										  PG_GET_COLLATION(),
										  0, NULL));
}

Datum
textregexne(PG_FUNCTION_ARGS)
{
	text	   *s = PG_GETARG_TEXT_PP(0);
	text	   *p = PG_GETARG_TEXT_PP(1);

	PG_RETURN_BOOL(!RE_compile_and_execute(p,
										   VARDATA_ANY(s),
										   VARSIZE_ANY_EXHDR(s),
										   REG_ADVANCED,
										   PG_GET_COLLATION(),
										   0, NULL));
}


/*
 *	routines that use the regexp stuff, but ignore the case.
 *	for this, we use the REG_ICASE flag to pg_regcomp
 */


Datum
nameicregexeq(PG_FUNCTION_ARGS)
{
	Name		n = PG_GETARG_NAME(0);
	text	   *p = PG_GETARG_TEXT_PP(1);

	PG_RETURN_BOOL(RE_compile_and_execute(p,
										  NameStr(*n),
										  strlen(NameStr(*n)),
										  REG_ADVANCED | REG_ICASE,
										  PG_GET_COLLATION(),
										  0, NULL));
}

Datum
nameicregexne(PG_FUNCTION_ARGS)
{
	Name		n = PG_GETARG_NAME(0);
	text	   *p = PG_GETARG_TEXT_PP(1);

	PG_RETURN_BOOL(!RE_compile_and_execute(p,
										   NameStr(*n),
										   strlen(NameStr(*n)),
										   REG_ADVANCED | REG_ICASE,
										   PG_GET_COLLATION(),
										   0, NULL));
}

Datum
texticregexeq(PG_FUNCTION_ARGS)
{
	text	   *s = PG_GETARG_TEXT_PP(0);
	text	   *p = PG_GETARG_TEXT_PP(1);

	PG_RETURN_BOOL(RE_compile_and_execute(p,
										  VARDATA_ANY(s),
										  VARSIZE_ANY_EXHDR(s),
										  REG_ADVANCED | REG_ICASE,
										  PG_GET_COLLATION(),
										  0, NULL));
}

Datum
texticregexne(PG_FUNCTION_ARGS)
{
	text	   *s = PG_GETARG_TEXT_PP(0);
	text	   *p = PG_GETARG_TEXT_PP(1);

	PG_RETURN_BOOL(!RE_compile_and_execute(p,
										   VARDATA_ANY(s),
										   VARSIZE_ANY_EXHDR(s),
										   REG_ADVANCED | REG_ICASE,
										   PG_GET_COLLATION(),
										   0, NULL));
}


/*
 * textregexsubstr()
 *		Return a substring matched by a regular expression.
 */
Datum
textregexsubstr(PG_FUNCTION_ARGS)
{
	text	   *s = PG_GETARG_TEXT_PP(0);
	text	   *p = PG_GETARG_TEXT_PP(1);
	regex_t    *re;
	regmatch_t	pmatch[2];
	int			so,
				eo;

	/* Compile RE */
	re = RE_compile_and_cache(p, REG_ADVANCED, PG_GET_COLLATION());

	/*
	 * We pass two regmatch_t structs to get info about the overall match and
	 * the match for the first parenthesized subexpression (if any). If there
	 * is a parenthesized subexpression, we return what it matched; else
	 * return what the whole regexp matched.
	 */
	if (!RE_execute(re,
					VARDATA_ANY(s), VARSIZE_ANY_EXHDR(s),
					2, pmatch))
		PG_RETURN_NULL();		/* definitely no match */

	if (re->re_nsub > 0)
	{
		/* has parenthesized subexpressions, use the first one */
		so = pmatch[1].rm_so;
		eo = pmatch[1].rm_eo;
	}
	else
	{
		/* no parenthesized subexpression, use whole match */
		so = pmatch[0].rm_so;
		eo = pmatch[0].rm_eo;
	}

	/*
	 * It is possible to have a match to the whole pattern but no match for a
	 * subexpression; for example 'foo(bar)?' is considered to match 'foo' but
	 * there is no subexpression match.  So this extra test for match failure
	 * is not redundant.
	 */
	if (so < 0 || eo < 0)
		PG_RETURN_NULL();

	return DirectFunctionCall3(text_substr,
							   PointerGetDatum(s),
							   Int32GetDatum(so + 1),
							   Int32GetDatum(eo - so));
}

/*
 * textregexreplace_noopt()
 *		Return a string matched by a regular expression, with replacement.
 *
 * This version doesn't have an option argument: we default to case
 * sensitive match, replace the first instance only.
 */
Datum
textregexreplace_noopt(PG_FUNCTION_ARGS)
{
	text	   *s = PG_GETARG_TEXT_PP(0);
	text	   *p = PG_GETARG_TEXT_PP(1);
	text	   *r = PG_GETARG_TEXT_PP(2);

	PG_RETURN_TEXT_P(replace_text_regexp(s, p, r,
										 REG_ADVANCED, PG_GET_COLLATION(),
										 0, 1));
}

/*
 * textregexreplace()
 *		Return a string matched by a regular expression, with replacement.
 */
Datum
textregexreplace(PG_FUNCTION_ARGS)
{
	text	   *s = PG_GETARG_TEXT_PP(0);
	text	   *p = PG_GETARG_TEXT_PP(1);
	text	   *r = PG_GETARG_TEXT_PP(2);
	text	   *opt = PG_GETARG_TEXT_PP(3);
	pg_re_flags flags;

	/*
	 * regexp_replace() with four arguments will be preferentially resolved as
	 * this form when the fourth argument is of type UNKNOWN.  However, the
	 * user might have intended to call textregexreplace_extended_no_n.  If we
	 * see flags that look like an integer, emit the same error that
	 * parse_re_flags would, but add a HINT about how to fix it.
	 */
	if (VARSIZE_ANY_EXHDR(opt) > 0)
	{
		char	   *opt_p = VARDATA_ANY(opt);

		if (*opt_p >= '0' && *opt_p <= '9')
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid regular expression option: \"%.*s\"",
							pg_mblen(opt_p), opt_p),
					 errhint("If you meant to use regexp_replace() with a start parameter, cast the fourth argument to integer explicitly.")));
	}

	parse_re_flags(&flags, opt);

	PG_RETURN_TEXT_P(replace_text_regexp(s, p, r,
										 flags.cflags, PG_GET_COLLATION(),
										 0, flags.glob ? 0 : 1));
}

/*
 * textregexreplace_extended()
 *		Return a string matched by a regular expression, with replacement.
 *		Extends textregexreplace by allowing a start position and the
 *		choice of the occurrence to replace (0 means all occurrences).
 */
Datum
textregexreplace_extended(PG_FUNCTION_ARGS)
{
	text	   *s = PG_GETARG_TEXT_PP(0);
	text	   *p = PG_GETARG_TEXT_PP(1);
	text	   *r = PG_GETARG_TEXT_PP(2);
	int			start = 1;
	int			n = 1;
	text	   *flags = PG_GETARG_TEXT_PP_IF_EXISTS(5);
	pg_re_flags re_flags;

	/* Collect optional parameters */
	if (PG_NARGS() > 3)
	{
		start = PG_GETARG_INT32(3);
		if (start <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"start", start)));
	}
	if (PG_NARGS() > 4)
	{
		n = PG_GETARG_INT32(4);
		if (n < 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"n", n)));
	}

	/* Determine options */
	parse_re_flags(&re_flags, flags);

	/* If N was not specified, deduce it from the 'g' flag */
	if (PG_NARGS() <= 4)
		n = re_flags.glob ? 0 : 1;

	/* Do the replacement(s) */
	PG_RETURN_TEXT_P(replace_text_regexp(s, p, r,
										 re_flags.cflags, PG_GET_COLLATION(),
										 start - 1, n));
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
textregexreplace_extended_no_n(PG_FUNCTION_ARGS)
{
	return textregexreplace_extended(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
textregexreplace_extended_no_flags(PG_FUNCTION_ARGS)
{
	return textregexreplace_extended(fcinfo);
}

/*
 * similar_to_escape(), similar_escape()
 *
 * Convert a SQL "SIMILAR TO" regexp pattern to POSIX style, so it can be
 * used by our regexp engine.
 *
 * similar_escape_internal() is the common workhorse for three SQL-exposed
 * functions.  esc_text can be passed as NULL to select the default escape
 * (which is '\'), or as an empty string to select no escape character.
 */
static text *
similar_escape_internal(text *pat_text, text *esc_text)
{
	text	   *result;
	char	   *p,
			   *e,
			   *r;
	int			plen,
				elen;
	bool		afterescape = false;
	bool		incharclass = false;
	int			nquotes = 0;

	p = VARDATA_ANY(pat_text);
	plen = VARSIZE_ANY_EXHDR(pat_text);
	if (esc_text == NULL)
	{
		/* No ESCAPE clause provided; default to backslash as escape */
		e = "\\";
		elen = 1;
	}
	else
	{
		e = VARDATA_ANY(esc_text);
		elen = VARSIZE_ANY_EXHDR(esc_text);
		if (elen == 0)
			e = NULL;			/* no escape character */
		else if (elen > 1)
		{
			int			escape_mblen = pg_mbstrlen_with_len(e, elen);

			if (escape_mblen > 1)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
						 errmsg("invalid escape string"),
						 errhint("Escape string must be empty or one character.")));
		}
	}

	/*----------
	 * We surround the transformed input string with
	 *			^(?: ... )$
	 * which requires some explanation.  We need "^" and "$" to force
	 * the pattern to match the entire input string as per the SQL spec.
	 * The "(?:" and ")" are a non-capturing set of parens; we have to have
	 * parens in case the string contains "|", else the "^" and "$" will
	 * be bound into the first and last alternatives which is not what we
	 * want, and the parens must be non capturing because we don't want them
	 * to count when selecting output for SUBSTRING.
	 *
	 * When the pattern is divided into three parts by escape-double-quotes,
	 * what we emit is
	 *			^(?:part1){1,1}?(part2){1,1}(?:part3)$
	 * which requires even more explanation.  The "{1,1}?" on part1 makes it
	 * non-greedy so that it will match the smallest possible amount of text
	 * not the largest, as required by SQL.  The plain parens around part2
	 * are capturing parens so that that part is what controls the result of
	 * SUBSTRING.  The "{1,1}" forces part2 to be greedy, so that it matches
	 * the largest possible amount of text; hence part3 must match the
	 * smallest amount of text, as required by SQL.  We don't need an explicit
	 * greediness marker on part3.  Note that this also confines the effects
	 * of any "|" characters to the respective part, which is what we want.
	 *
	 * The SQL spec says that SUBSTRING's pattern must contain exactly two
	 * escape-double-quotes, but we only complain if there's more than two.
	 * With none, we act as though part1 and part3 are empty; with one, we
	 * act as though part3 is empty.  Both behaviors fall out of omitting
	 * the relevant part separators in the above expansion.  If the result
	 * of this function is used in a plain regexp match (SIMILAR TO), the
	 * escape-double-quotes have no effect on the match behavior.
	 *----------
	 */

	/*
	 * We need room for the prefix/postfix and part separators, plus as many
	 * as 3 output bytes per input byte; since the input is at most 1GB this
	 * can't overflow size_t.
	 */
	result = (text *) palloc(VARHDRSZ + 23 + 3 * (size_t) plen);
	r = VARDATA(result);

	*r++ = '^';
	*r++ = '(';
	*r++ = '?';
	*r++ = ':';

	while (plen > 0)
	{
		char		pchar = *p;

		/*
		 * If both the escape character and the current character from the
		 * pattern are multi-byte, we need to take the slow path.
		 *
		 * But if one of them is single-byte, we can process the pattern one
		 * byte at a time, ignoring multi-byte characters.  (This works
		 * because all server-encodings have the property that a valid
		 * multi-byte character representation cannot contain the
		 * representation of a valid single-byte character.)
		 */

		if (elen > 1)
		{
			int			mblen = pg_mblen(p);

			if (mblen > 1)
			{
				/* slow, multi-byte path */
				if (afterescape)
				{
					*r++ = '\\';
					memcpy(r, p, mblen);
					r += mblen;
					afterescape = false;
				}
				else if (e && elen == mblen && memcmp(e, p, mblen) == 0)
				{
					/* SQL escape character; do not send to output */
					afterescape = true;
				}
				else
				{
					/*
					 * We know it's a multi-byte character, so we don't need
					 * to do all the comparisons to single-byte characters
					 * that we do below.
					 */
					memcpy(r, p, mblen);
					r += mblen;
				}

				p += mblen;
				plen -= mblen;

				continue;
			}
		}

		/* fast path */
		if (afterescape)
		{
			if (pchar == '"' && !incharclass)	/* escape-double-quote? */
			{
				/* emit appropriate part separator, per notes above */
				if (nquotes == 0)
				{
					*r++ = ')';
					*r++ = '{';
					*r++ = '1';
					*r++ = ',';
					*r++ = '1';
					*r++ = '}';
					*r++ = '?';
					*r++ = '(';
				}
				else if (nquotes == 1)
				{
					*r++ = ')';
					*r++ = '{';
					*r++ = '1';
					*r++ = ',';
					*r++ = '1';
					*r++ = '}';
					*r++ = '(';
					*r++ = '?';
					*r++ = ':';
				}
				else
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_USE_OF_ESCAPE_CHARACTER),
							 errmsg("SQL regular expression may not contain more than two escape-double-quote separators")));
				nquotes++;
			}
			else
			{
				/*
				 * We allow any character at all to be escaped; notably, this
				 * allows access to POSIX character-class escapes such as
				 * "\d".  The SQL spec is considerably more restrictive.
				 */
				*r++ = '\\';
				*r++ = pchar;
			}
			afterescape = false;
		}
		else if (e && pchar == *e)
		{
			/* SQL escape character; do not send to output */
			afterescape = true;
		}
		else if (incharclass)
		{
			if (pchar == '\\')
				*r++ = '\\';
			*r++ = pchar;
			if (pchar == ']')
				incharclass = false;
		}
		else if (pchar == '[')
		{
			*r++ = pchar;
			incharclass = true;
		}
		else if (pchar == '%')
		{
			*r++ = '.';
			*r++ = '*';
		}
		else if (pchar == '_')
			*r++ = '.';
		else if (pchar == '(')
		{
			/* convert to non-capturing parenthesis */
			*r++ = '(';
			*r++ = '?';
			*r++ = ':';
		}
		else if (pchar == '\\' || pchar == '.' ||
				 pchar == '^' || pchar == '$')
		{
			*r++ = '\\';
			*r++ = pchar;
		}
		else
			*r++ = pchar;
		p++, plen--;
	}

	*r++ = ')';
	*r++ = '$';

	SET_VARSIZE(result, r - ((char *) result));

	return result;
}

/*
 * similar_to_escape(pattern, escape)
 */
Datum
similar_to_escape_2(PG_FUNCTION_ARGS)
{
	text	   *pat_text = PG_GETARG_TEXT_PP(0);
	text	   *esc_text = PG_GETARG_TEXT_PP(1);
	text	   *result;

	result = similar_escape_internal(pat_text, esc_text);

	PG_RETURN_TEXT_P(result);
}

/*
 * similar_to_escape(pattern)
 * Inserts a default escape character.
 */
Datum
similar_to_escape_1(PG_FUNCTION_ARGS)
{
	text	   *pat_text = PG_GETARG_TEXT_PP(0);
	text	   *result;

	result = similar_escape_internal(pat_text, NULL);

	PG_RETURN_TEXT_P(result);
}

/*
 * similar_escape(pattern, escape)
 *
 * Legacy function for compatibility with views stored using the
 * pre-v13 expansion of SIMILAR TO.  Unlike the above functions, this
 * is non-strict, which leads to not-per-spec handling of "ESCAPE NULL".
 */
Datum
similar_escape(PG_FUNCTION_ARGS)
{
	text	   *pat_text;
	text	   *esc_text;
	text	   *result;

	/* This function is not strict, so must test explicitly */
	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();
	pat_text = PG_GETARG_TEXT_PP(0);

	if (PG_ARGISNULL(1))
		esc_text = NULL;		/* use default escape character */
	else
		esc_text = PG_GETARG_TEXT_PP(1);

	result = similar_escape_internal(pat_text, esc_text);

	PG_RETURN_TEXT_P(result);
}

/*
 * regexp_count()
 *		Return the number of matches of a pattern within a string.
 */
Datum
regexp_count(PG_FUNCTION_ARGS)
{
	text	   *str = PG_GETARG_TEXT_PP(0);
	text	   *pattern = PG_GETARG_TEXT_PP(1);
	int			start = 1;
	text	   *flags = PG_GETARG_TEXT_PP_IF_EXISTS(3);
	pg_re_flags re_flags;
	regexp_matches_ctx *matchctx;

	/* Collect optional parameters */
	if (PG_NARGS() > 2)
	{
		start = PG_GETARG_INT32(2);
		if (start <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"start", start)));
	}

	/* Determine options */
	parse_re_flags(&re_flags, flags);
	/* User mustn't specify 'g' */
	if (re_flags.glob)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		/* translator: %s is a SQL function name */
				 errmsg("%s does not support the \"global\" option",
						"regexp_count()")));
	/* But we find all the matches anyway */
	re_flags.glob = true;

	/* Do the matching */
	matchctx = setup_regexp_matches(str, pattern, &re_flags, start - 1,
									PG_GET_COLLATION(),
									false,	/* can ignore subexprs */
									false, false);

	PG_RETURN_INT32(matchctx->nmatches);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_count_no_start(PG_FUNCTION_ARGS)
{
	return regexp_count(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_count_no_flags(PG_FUNCTION_ARGS)
{
	return regexp_count(fcinfo);
}

/*
 * regexp_instr()
 *		Return the match's position within the string
 */
Datum
regexp_instr(PG_FUNCTION_ARGS)
{
	text	   *str = PG_GETARG_TEXT_PP(0);
	text	   *pattern = PG_GETARG_TEXT_PP(1);
	int			start = 1;
	int			n = 1;
	int			endoption = 0;
	text	   *flags = PG_GETARG_TEXT_PP_IF_EXISTS(5);
	int			subexpr = 0;
	int			pos;
	pg_re_flags re_flags;
	regexp_matches_ctx *matchctx;

	/* Collect optional parameters */
	if (PG_NARGS() > 2)
	{
		start = PG_GETARG_INT32(2);
		if (start <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"start", start)));
	}
	if (PG_NARGS() > 3)
	{
		n = PG_GETARG_INT32(3);
		if (n <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"n", n)));
	}
	if (PG_NARGS() > 4)
	{
		endoption = PG_GETARG_INT32(4);
		if (endoption != 0 && endoption != 1)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"endoption", endoption)));
	}
	if (PG_NARGS() > 6)
	{
		subexpr = PG_GETARG_INT32(6);
		if (subexpr < 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"subexpr", subexpr)));
	}

	/* Determine options */
	parse_re_flags(&re_flags, flags);
	/* User mustn't specify 'g' */
	if (re_flags.glob)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		/* translator: %s is a SQL function name */
				 errmsg("%s does not support the \"global\" option",
						"regexp_instr()")));
	/* But we find all the matches anyway */
	re_flags.glob = true;

	/* Do the matching */
	matchctx = setup_regexp_matches(str, pattern, &re_flags, start - 1,
									PG_GET_COLLATION(),
									(subexpr > 0),	/* need submatches? */
									false, false);

	/* When n exceeds matches return 0 (includes case of no matches) */
	if (n > matchctx->nmatches)
		PG_RETURN_INT32(0);

	/* When subexpr exceeds number of subexpressions return 0 */
	if (subexpr > matchctx->npatterns)
		PG_RETURN_INT32(0);

	/* Select the appropriate match position to return */
	pos = (n - 1) * matchctx->npatterns;
	if (subexpr > 0)
		pos += subexpr - 1;
	pos *= 2;
	if (endoption == 1)
		pos += 1;

	if (matchctx->match_locs[pos] >= 0)
		PG_RETURN_INT32(matchctx->match_locs[pos] + 1);
	else
		PG_RETURN_INT32(0);		/* position not identifiable */
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_instr_no_start(PG_FUNCTION_ARGS)
{
	return regexp_instr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_instr_no_n(PG_FUNCTION_ARGS)
{
	return regexp_instr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_instr_no_endoption(PG_FUNCTION_ARGS)
{
	return regexp_instr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_instr_no_flags(PG_FUNCTION_ARGS)
{
	return regexp_instr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_instr_no_subexpr(PG_FUNCTION_ARGS)
{
	return regexp_instr(fcinfo);
}

/*
 * regexp_like()
 *		Test for a pattern match within a string.
 */
Datum
regexp_like(PG_FUNCTION_ARGS)
{
	text	   *str = PG_GETARG_TEXT_PP(0);
	text	   *pattern = PG_GETARG_TEXT_PP(1);
	text	   *flags = PG_GETARG_TEXT_PP_IF_EXISTS(2);
	pg_re_flags re_flags;

	/* Determine options */
	parse_re_flags(&re_flags, flags);
	/* User mustn't specify 'g' */
	if (re_flags.glob)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		/* translator: %s is a SQL function name */
				 errmsg("%s does not support the \"global\" option",
						"regexp_like()")));

	/* Otherwise it's like textregexeq/texticregexeq */
	PG_RETURN_BOOL(RE_compile_and_execute(pattern,
										  VARDATA_ANY(str),
										  VARSIZE_ANY_EXHDR(str),
										  re_flags.cflags,
										  PG_GET_COLLATION(),
										  0, NULL));
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_like_no_flags(PG_FUNCTION_ARGS)
{
	return regexp_like(fcinfo);
}

/*
 * regexp_match()
 *		Return the first substring(s) matching a pattern within a string.
 */
Datum
regexp_match(PG_FUNCTION_ARGS)
{
	text	   *orig_str = PG_GETARG_TEXT_PP(0);
	text	   *pattern = PG_GETARG_TEXT_PP(1);
	text	   *flags = PG_GETARG_TEXT_PP_IF_EXISTS(2);
	pg_re_flags re_flags;
	regexp_matches_ctx *matchctx;

	/* Determine options */
	parse_re_flags(&re_flags, flags);
	/* User mustn't specify 'g' */
	if (re_flags.glob)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		/* translator: %s is a SQL function name */
				 errmsg("%s does not support the \"global\" option",
						"regexp_match()"),
				 errhint("Use the regexp_matches function instead.")));

	matchctx = setup_regexp_matches(orig_str, pattern, &re_flags, 0,
									PG_GET_COLLATION(), true, false, false);

	if (matchctx->nmatches == 0)
		PG_RETURN_NULL();

	Assert(matchctx->nmatches == 1);

	/* Create workspace that build_regexp_match_result needs */
	matchctx->elems = (Datum *) palloc(sizeof(Datum) * matchctx->npatterns);
	matchctx->nulls = (bool *) palloc(sizeof(bool) * matchctx->npatterns);

	PG_RETURN_DATUM(PointerGetDatum(build_regexp_match_result(matchctx)));
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_match_no_flags(PG_FUNCTION_ARGS)
{
	return regexp_match(fcinfo);
}

/*
 * regexp_matches()
 *		Return a table of all matches of a pattern within a string.
 */
Datum
regexp_matches(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	regexp_matches_ctx *matchctx;

	if (SRF_IS_FIRSTCALL())
	{
		text	   *pattern = PG_GETARG_TEXT_PP(1);
		text	   *flags = PG_GETARG_TEXT_PP_IF_EXISTS(2);
		pg_re_flags re_flags;
		MemoryContext oldcontext;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* Determine options */
		parse_re_flags(&re_flags, flags);

		/* be sure to copy the input string into the multi-call ctx */
		matchctx = setup_regexp_matches(PG_GETARG_TEXT_P_COPY(0), pattern,
										&re_flags, 0,
										PG_GET_COLLATION(),
										true, false, false);

		/* Pre-create workspace that build_regexp_match_result needs */
		matchctx->elems = (Datum *) palloc(sizeof(Datum) * matchctx->npatterns);
		matchctx->nulls = (bool *) palloc(sizeof(bool) * matchctx->npatterns);

		MemoryContextSwitchTo(oldcontext);
		funcctx->user_fctx = (void *) matchctx;
	}

	funcctx = SRF_PERCALL_SETUP();
	matchctx = (regexp_matches_ctx *) funcctx->user_fctx;

	if (matchctx->next_match < matchctx->nmatches)
	{
		ArrayType  *result_ary;

		result_ary = build_regexp_match_result(matchctx);
		matchctx->next_match++;
		SRF_RETURN_NEXT(funcctx, PointerGetDatum(result_ary));
	}

	SRF_RETURN_DONE(funcctx);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_matches_no_flags(PG_FUNCTION_ARGS)
{
	return regexp_matches(fcinfo);
}

/*
 * setup_regexp_matches --- do the initial matching for regexp_match,
 *		regexp_split, and related functions
 *
 * To avoid having to re-find the compiled pattern on each call, we do
 * all the matching in one swoop.  The returned regexp_matches_ctx contains
 * the locations of all the substrings matching the pattern.
 *
 * start_search: the character (not byte) offset in orig_str at which to
 * begin the search.  Returned positions are relative to orig_str anyway.
 * use_subpatterns: collect data about matches to parenthesized subexpressions.
 * ignore_degenerate: ignore zero-length matches.
 * fetching_unmatched: caller wants to fetch unmatched substrings.
 *
 * We don't currently assume that fetching_unmatched is exclusive of fetching
 * the matched text too; if it's set, the conversion buffer is large enough to
 * fetch any single matched or unmatched string, but not any larger
 * substring.  (In practice, when splitting the matches are usually small
 * anyway, and it didn't seem worth complicating the code further.)
 */
static regexp_matches_ctx *
setup_regexp_matches(text *orig_str, text *pattern, pg_re_flags *re_flags,
					 int start_search,
					 Oid collation,
					 bool use_subpatterns,
					 bool ignore_degenerate,
					 bool fetching_unmatched)
{
	regexp_matches_ctx *matchctx = palloc0(sizeof(regexp_matches_ctx));
	int			eml = pg_database_encoding_max_length();
	int			orig_len;
	pg_wchar   *wide_str;
	int			wide_len;
	int			cflags;
	regex_t    *cpattern;
	regmatch_t *pmatch;
	int			pmatch_len;
	int			array_len;
	int			array_idx;
	int			prev_match_end;
	int			prev_valid_match_end;
	int			maxlen = 0;		/* largest fetch length in characters */

	/* save original string --- we'll extract result substrings from it */
	matchctx->orig_str = orig_str;

	/* convert string to pg_wchar form for matching */
	orig_len = VARSIZE_ANY_EXHDR(orig_str);
	wide_str = (pg_wchar *) palloc(sizeof(pg_wchar) * (orig_len + 1));
	wide_len = pg_mb2wchar_with_len(VARDATA_ANY(orig_str), wide_str, orig_len);

	/* set up the compiled pattern */
	cflags = re_flags->cflags;
	if (!use_subpatterns)
		cflags |= REG_NOSUB;
	cpattern = RE_compile_and_cache(pattern, cflags, collation);

	/* do we want to remember subpatterns? */
	if (use_subpatterns && cpattern->re_nsub > 0)
	{
		matchctx->npatterns = cpattern->re_nsub;
		pmatch_len = cpattern->re_nsub + 1;
	}
	else
	{
		use_subpatterns = false;
		matchctx->npatterns = 1;
		pmatch_len = 1;
	}

	/* temporary output space for RE package */
	pmatch = palloc(sizeof(regmatch_t) * pmatch_len);

	/*
	 * the real output space (grown dynamically if needed)
	 *
	 * use values 2^n-1, not 2^n, so that we hit the limit at 2^28-1 rather
	 * than at 2^27
	 */
	array_len = re_flags->glob ? 255 : 31;
	matchctx->match_locs = (int *) palloc(sizeof(int) * array_len);
	array_idx = 0;

	/* search for the pattern, perhaps repeatedly */
	prev_match_end = 0;
	prev_valid_match_end = 0;
	while (RE_wchar_execute(cpattern, wide_str, wide_len, start_search,
							pmatch_len, pmatch))
	{
		/*
		 * If requested, ignore degenerate matches, which are zero-length
		 * matches occurring at the start or end of a string or just after a
		 * previous match.
		 */
		if (!ignore_degenerate ||
			(pmatch[0].rm_so < wide_len &&
			 pmatch[0].rm_eo > prev_match_end))
		{
			/* enlarge output space if needed */
			while (array_idx + matchctx->npatterns * 2 + 1 > array_len)
			{
				array_len += array_len + 1; /* 2^n-1 => 2^(n+1)-1 */
				if (array_len > MaxAllocSize / sizeof(int))
					ereport(ERROR,
							(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
							 errmsg("too many regular expression matches")));
				matchctx->match_locs = (int *) repalloc(matchctx->match_locs,
														sizeof(int) * array_len);
			}

			/* save this match's locations */
			if (use_subpatterns)
			{
				int			i;

				for (i = 1; i <= matchctx->npatterns; i++)
				{
					int			so = pmatch[i].rm_so;
					int			eo = pmatch[i].rm_eo;

					matchctx->match_locs[array_idx++] = so;
					matchctx->match_locs[array_idx++] = eo;
					if (so >= 0 && eo >= 0 && (eo - so) > maxlen)
						maxlen = (eo - so);
				}
			}
			else
			{
				int			so = pmatch[0].rm_so;
				int			eo = pmatch[0].rm_eo;

				matchctx->match_locs[array_idx++] = so;
				matchctx->match_locs[array_idx++] = eo;
				if (so >= 0 && eo >= 0 && (eo - so) > maxlen)
					maxlen = (eo - so);
			}
			matchctx->nmatches++;

			/*
			 * check length of unmatched portion between end of previous valid
			 * (nondegenerate, or degenerate but not ignored) match and start
			 * of current one
			 */
			if (fetching_unmatched &&
				pmatch[0].rm_so >= 0 &&
				(pmatch[0].rm_so - prev_valid_match_end) > maxlen)
				maxlen = (pmatch[0].rm_so - prev_valid_match_end);
			prev_valid_match_end = pmatch[0].rm_eo;
		}
		prev_match_end = pmatch[0].rm_eo;

		/* if not glob, stop after one match */
		if (!re_flags->glob)
			break;

		/*
		 * Advance search position.  Normally we start the next search at the
		 * end of the previous match; but if the match was of zero length, we
		 * have to advance by one character, or we'd just find the same match
		 * again.
		 */
		start_search = prev_match_end;
		if (pmatch[0].rm_so == pmatch[0].rm_eo)
			start_search++;
		if (start_search > wide_len)
			break;
	}

	/*
	 * check length of unmatched portion between end of last match and end of
	 * input string
	 */
	if (fetching_unmatched &&
		(wide_len - prev_valid_match_end) > maxlen)
		maxlen = (wide_len - prev_valid_match_end);

	/*
	 * Keep a note of the end position of the string for the benefit of
	 * splitting code.
	 */
	matchctx->match_locs[array_idx] = wide_len;

	if (eml > 1)
	{
		int64		maxsiz = eml * (int64) maxlen;
		int			conv_bufsiz;

		/*
		 * Make the conversion buffer large enough for any substring of
		 * interest.
		 *
		 * Worst case: assume we need the maximum size (maxlen*eml), but take
		 * advantage of the fact that the original string length in bytes is
		 * an upper bound on the byte length of any fetched substring (and we
		 * know that len+1 is safe to allocate because the varlena header is
		 * longer than 1 byte).
		 */
		if (maxsiz > orig_len)
			conv_bufsiz = orig_len + 1;
		else
			conv_bufsiz = maxsiz + 1;	/* safe since maxsiz < 2^30 */

		matchctx->conv_buf = palloc(conv_bufsiz);
		matchctx->conv_bufsiz = conv_bufsiz;
		matchctx->wide_str = wide_str;
	}
	else
	{
		/* No need to keep the wide string if we're in a single-byte charset. */
		pfree(wide_str);
		matchctx->wide_str = NULL;
		matchctx->conv_buf = NULL;
		matchctx->conv_bufsiz = 0;
	}

	/* Clean up temp storage */
	pfree(pmatch);

	return matchctx;
}

/*
 * build_regexp_match_result - build output array for current match
 */
static ArrayType *
build_regexp_match_result(regexp_matches_ctx *matchctx)
{
	char	   *buf = matchctx->conv_buf;
	Datum	   *elems = matchctx->elems;
	bool	   *nulls = matchctx->nulls;
	int			dims[1];
	int			lbs[1];
	int			loc;
	int			i;

	/* Extract matching substrings from the original string */
	loc = matchctx->next_match * matchctx->npatterns * 2;
	for (i = 0; i < matchctx->npatterns; i++)
	{
		int			so = matchctx->match_locs[loc++];
		int			eo = matchctx->match_locs[loc++];

		if (so < 0 || eo < 0)
		{
			elems[i] = (Datum) 0;
			nulls[i] = true;
		}
		else if (buf)
		{
			int			len = pg_wchar2mb_with_len(matchctx->wide_str + so,
												   buf,
												   eo - so);

			Assert(len < matchctx->conv_bufsiz);
			elems[i] = PointerGetDatum(cstring_to_text_with_len(buf, len));
			nulls[i] = false;
		}
		else
		{
			elems[i] = DirectFunctionCall3(text_substr,
										   PointerGetDatum(matchctx->orig_str),
										   Int32GetDatum(so + 1),
										   Int32GetDatum(eo - so));
			nulls[i] = false;
		}
	}

	/* And form an array */
	dims[0] = matchctx->npatterns;
	lbs[0] = 1;
	/* XXX: this hardcodes assumptions about the text type */
	return construct_md_array(elems, nulls, 1, dims, lbs,
							  TEXTOID, -1, false, TYPALIGN_INT);
}

/*
 * regexp_split_to_table()
 *		Split the string at matches of the pattern, returning the
 *		split-out substrings as a table.
 */
Datum
regexp_split_to_table(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	regexp_matches_ctx *splitctx;

	if (SRF_IS_FIRSTCALL())
	{
		text	   *pattern = PG_GETARG_TEXT_PP(1);
		text	   *flags = PG_GETARG_TEXT_PP_IF_EXISTS(2);
		pg_re_flags re_flags;
		MemoryContext oldcontext;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* Determine options */
		parse_re_flags(&re_flags, flags);
		/* User mustn't specify 'g' */
		if (re_flags.glob)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			/* translator: %s is a SQL function name */
					 errmsg("%s does not support the \"global\" option",
							"regexp_split_to_table()")));
		/* But we find all the matches anyway */
		re_flags.glob = true;

		/* be sure to copy the input string into the multi-call ctx */
		splitctx = setup_regexp_matches(PG_GETARG_TEXT_P_COPY(0), pattern,
										&re_flags, 0,
										PG_GET_COLLATION(),
										false, true, true);

		MemoryContextSwitchTo(oldcontext);
		funcctx->user_fctx = (void *) splitctx;
	}

	funcctx = SRF_PERCALL_SETUP();
	splitctx = (regexp_matches_ctx *) funcctx->user_fctx;

	if (splitctx->next_match <= splitctx->nmatches)
	{
		Datum		result = build_regexp_split_result(splitctx);

		splitctx->next_match++;
		SRF_RETURN_NEXT(funcctx, result);
	}

	SRF_RETURN_DONE(funcctx);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_split_to_table_no_flags(PG_FUNCTION_ARGS)
{
	return regexp_split_to_table(fcinfo);
}

/*
 * regexp_split_to_array()
 *		Split the string at matches of the pattern, returning the
 *		split-out substrings as an array.
 */
Datum
regexp_split_to_array(PG_FUNCTION_ARGS)
{
	ArrayBuildState *astate = NULL;
	pg_re_flags re_flags;
	regexp_matches_ctx *splitctx;

	/* Determine options */
	parse_re_flags(&re_flags, PG_GETARG_TEXT_PP_IF_EXISTS(2));
	/* User mustn't specify 'g' */
	if (re_flags.glob)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		/* translator: %s is a SQL function name */
				 errmsg("%s does not support the \"global\" option",
						"regexp_split_to_array()")));
	/* But we find all the matches anyway */
	re_flags.glob = true;

	splitctx = setup_regexp_matches(PG_GETARG_TEXT_PP(0),
									PG_GETARG_TEXT_PP(1),
									&re_flags, 0,
									PG_GET_COLLATION(),
									false, true, true);

	while (splitctx->next_match <= splitctx->nmatches)
	{
		astate = accumArrayResult(astate,
								  build_regexp_split_result(splitctx),
								  false,
								  TEXTOID,
								  GetCurrentMemoryContext());
		splitctx->next_match++;
	}

	PG_RETURN_ARRAYTYPE_P(makeArrayResult(astate, GetCurrentMemoryContext()));
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_split_to_array_no_flags(PG_FUNCTION_ARGS)
{
	return regexp_split_to_array(fcinfo);
}

/*
 * build_regexp_split_result - build output string for current match
 *
 * We return the string between the current match and the previous one,
 * or the string after the last match when next_match == nmatches.
 */
static Datum
build_regexp_split_result(regexp_matches_ctx *splitctx)
{
	char	   *buf = splitctx->conv_buf;
	int			startpos;
	int			endpos;

	if (splitctx->next_match > 0)
		startpos = splitctx->match_locs[splitctx->next_match * 2 - 1];
	else
		startpos = 0;
	if (startpos < 0)
		elog(ERROR, "invalid match ending position");

	endpos = splitctx->match_locs[splitctx->next_match * 2];
	if (endpos < startpos)
		elog(ERROR, "invalid match starting position");

	if (buf)
	{
		int			len;

		len = pg_wchar2mb_with_len(splitctx->wide_str + startpos,
								   buf,
								   endpos - startpos);
		Assert(len < splitctx->conv_bufsiz);
		return PointerGetDatum(cstring_to_text_with_len(buf, len));
	}
	else
	{
		return DirectFunctionCall3(text_substr,
								   PointerGetDatum(splitctx->orig_str),
								   Int32GetDatum(startpos + 1),
								   Int32GetDatum(endpos - startpos));
	}
}

/*
 * regexp_substr()
 *		Return the substring that matches a regular expression pattern
 */
Datum
regexp_substr(PG_FUNCTION_ARGS)
{
	text	   *str = PG_GETARG_TEXT_PP(0);
	text	   *pattern = PG_GETARG_TEXT_PP(1);
	int			start = 1;
	int			n = 1;
	text	   *flags = PG_GETARG_TEXT_PP_IF_EXISTS(4);
	int			subexpr = 0;
	int			so,
				eo,
				pos;
	pg_re_flags re_flags;
	regexp_matches_ctx *matchctx;

	/* Collect optional parameters */
	if (PG_NARGS() > 2)
	{
		start = PG_GETARG_INT32(2);
		if (start <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"start", start)));
	}
	if (PG_NARGS() > 3)
	{
		n = PG_GETARG_INT32(3);
		if (n <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"n", n)));
	}
	if (PG_NARGS() > 5)
	{
		subexpr = PG_GETARG_INT32(5);
		if (subexpr < 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid value for parameter \"%s\": %d",
							"subexpr", subexpr)));
	}

	/* Determine options */
	parse_re_flags(&re_flags, flags);
	/* User mustn't specify 'g' */
	if (re_flags.glob)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		/* translator: %s is a SQL function name */
				 errmsg("%s does not support the \"global\" option",
						"regexp_substr()")));
	/* But we find all the matches anyway */
	re_flags.glob = true;

	/* Do the matching */
	matchctx = setup_regexp_matches(str, pattern, &re_flags, start - 1,
									PG_GET_COLLATION(),
									(subexpr > 0),	/* need submatches? */
									false, false);

	/* When n exceeds matches return NULL (includes case of no matches) */
	if (n > matchctx->nmatches)
		PG_RETURN_NULL();

	/* When subexpr exceeds number of subexpressions return NULL */
	if (subexpr > matchctx->npatterns)
		PG_RETURN_NULL();

	/* Select the appropriate match position to return */
	pos = (n - 1) * matchctx->npatterns;
	if (subexpr > 0)
		pos += subexpr - 1;
	pos *= 2;
	so = matchctx->match_locs[pos];
	eo = matchctx->match_locs[pos + 1];

	if (so < 0 || eo < 0)
		PG_RETURN_NULL();		/* unidentifiable location */

	PG_RETURN_DATUM(DirectFunctionCall3(text_substr,
										PointerGetDatum(matchctx->orig_str),
										Int32GetDatum(so + 1),
										Int32GetDatum(eo - so)));
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_substr_no_start(PG_FUNCTION_ARGS)
{
	return regexp_substr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_substr_no_n(PG_FUNCTION_ARGS)
{
	return regexp_substr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_substr_no_flags(PG_FUNCTION_ARGS)
{
	return regexp_substr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
regexp_substr_no_subexpr(PG_FUNCTION_ARGS)
{
	return regexp_substr(fcinfo);
}

/*
 * regexp_fixed_prefix - extract fixed prefix, if any, for a regexp
 *
 * The result is NULL if there is no fixed prefix, else a palloc'd string.
 * If it is an exact match, not just a prefix, *exact is returned as true.
 */
char *
regexp_fixed_prefix(text *text_re, bool case_insensitive, Oid collation,
					bool *exact)
{
	char	   *result;
	regex_t    *re;
	int			cflags;
	int			re_result;
	pg_wchar   *str;
	size_t		slen;
	size_t		maxlen;
	char		errMsg[100];

	*exact = false;				/* default result */

	/* Compile RE */
	cflags = REG_ADVANCED;
	if (case_insensitive)
		cflags |= REG_ICASE;

	re = RE_compile_and_cache(text_re, cflags | REG_NOSUB, collation);

	/* Examine it to see if there's a fixed prefix */
	re_result = pg_regprefix(re, &str, &slen);

	switch (re_result)
	{
		case REG_NOMATCH:
			return NULL;

		case REG_PREFIX:
			/* continue with wchar conversion */
			break;

		case REG_EXACT:
			*exact = true;
			/* continue with wchar conversion */
			break;

		default:
			/* re failed??? */
			CHECK_FOR_INTERRUPTS();
			pg_regerror(re_result, re, errMsg, sizeof(errMsg));
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
					 errmsg("regular expression failed: %s", errMsg)));
			break;
	}

	/* Convert pg_wchar result back to database encoding */
	maxlen = pg_database_encoding_max_length() * slen + 1;
	result = (char *) palloc(maxlen);
	slen = pg_wchar2mb_with_len(str, result, slen);
	Assert(slen < maxlen);

	free(str);

	return result;
}

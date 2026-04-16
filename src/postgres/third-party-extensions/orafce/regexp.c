#include "postgres.h"

#include "catalog/pg_type.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "regex/regex.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#if PG_VERSION_NUM >= 150000

#include "utils/varlena.h"

#endif

#include "orafce.h"
#include "builtins.h"

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
 * Backport code from PostgreSQL 15
 */

PG_FUNCTION_INFO_V1(orafce_regexp_instr);
PG_FUNCTION_INFO_V1(orafce_regexp_instr_no_start);
PG_FUNCTION_INFO_V1(orafce_regexp_instr_no_n);
PG_FUNCTION_INFO_V1(orafce_regexp_instr_no_endoption);
PG_FUNCTION_INFO_V1(orafce_regexp_instr_no_flags);
PG_FUNCTION_INFO_V1(orafce_regexp_instr_no_subexpr);
PG_FUNCTION_INFO_V1(orafce_textregexreplace_noopt);
PG_FUNCTION_INFO_V1(orafce_textregexreplace);
PG_FUNCTION_INFO_V1(orafce_textregexreplace_extended);
PG_FUNCTION_INFO_V1(orafce_textregexreplace_extended_no_n);
PG_FUNCTION_INFO_V1(orafce_textregexreplace_extended_no_flags);

#if PG_VERSION_NUM <  120000


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

static int	num_res = 0;		/* # of cached re's */
static cached_re_str re_array[MAX_CACHED_RES];	/* cached re's */


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
static regex_t *
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

	/*
	 * Look for a match among previously compiled REs.  Since the data
	 * structure is self-organizing with most-used entries at the front, our
	 * search strategy can just be to scan from the front.
	 */
	for (i = 0; i < num_res; i++)
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
	if (num_res >= MAX_CACHED_RES)
	{
		--num_res;
		Assert(num_res < MAX_CACHED_RES);
		pg_regfree(&re_array[num_res].cre_re);
		free(re_array[num_res].cre_pat);
	}

	if (num_res > 0)
		memmove(&re_array[1], &re_array[0], num_res * sizeof(cached_re_str));

	re_array[0] = re_temp;
	num_res++;

	return &re_array[0].cre_re;
}

#endif

#if PG_VERSION_NUM <  150000

/*
 * check_replace_text_has_escape
 *
 * Returns 0 if text contains no backslashes that need processing.
 * Returns 1 if text contains backslashes, but not regexp submatch specifiers.
 * Returns 2 if text contains regexp submatch specifiers (\1 .. \9).
 */
static int
check_replace_text_has_escape(const text *replace_text)
{
	int			result = 0;
	const char *p = VARDATA_ANY(replace_text);
	const char *p_end = p + VARSIZE_ANY_EXHDR(replace_text);

	while (p < p_end)
	{
		/* Find next escape char, if any. */
		p = memchr(p, '\\', p_end - p);
		if (p == NULL)
			break;
		p++;
		/* Note: a backslash at the end doesn't require extra processing. */
		if (p < p_end)
		{
			if (*p >= '1' && *p <= '9')
				return 2;		/* Found a submatch specifier, so done */
			result = 1;			/* Found some other sequence, keep looking */
			p++;
		}
	}
	return result;
}

/*
 * charlen_to_bytelen()
 *	Compute the number of bytes occupied by n characters starting at *p
 *
 * It is caller's responsibility that there actually are n characters;
 * the string need not be null-terminated.
 */
static int
charlen_to_bytelen(const char *p, int n)
{
	if (pg_database_encoding_max_length() == 1)
	{
		/* Optimization for single-byte encodings */
		return n;
	}
	else
	{
		const char *s;

		for (s = p; n > 0; n--)
			s += pg_mblen(s);

		return s - p;
	}
}

/*
 * appendStringInfoText
 *
 * Append a text to str.
 * Like appendStringInfoString(str, text_to_cstring(t)) but faster.
 */
static void
appendStringInfoText(StringInfo str, const text *t)
{
	appendBinaryStringInfo(str, VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
}

/*
 * appendStringInfoRegexpSubstr
 *
 * Append replace_text to str, substituting regexp back references for
 * \n escapes.  start_ptr is the start of the match in the source string,
 * at logical character position data_pos.
 */
static void
appendStringInfoRegexpSubstr(StringInfo str, text *replace_text,
							 regmatch_t *pmatch,
							 char *start_ptr, int data_pos)
{
	const char *p = VARDATA_ANY(replace_text);
	const char *p_end = p + VARSIZE_ANY_EXHDR(replace_text);

	while (p < p_end)
	{
		const char *chunk_start = p;
		int			so;
		int			eo;

		/* Find next escape char, if any. */
		p = memchr(p, '\\', p_end - p);
		if (p == NULL)
			p = p_end;

		/* Copy the text we just scanned over, if any. */
		if (p > chunk_start)
			appendBinaryStringInfo(str, chunk_start, p - chunk_start);

		/* Done if at end of string, else advance over escape char. */
		if (p >= p_end)
			break;
		p++;

		if (p >= p_end)
		{
			/* Escape at very end of input.  Treat same as unexpected char */
			appendStringInfoChar(str, '\\');
			break;
		}

		if (*p >= '1' && *p <= '9')
		{
			/* Use the back reference of regexp. */
			int			idx = *p - '0';

			so = pmatch[idx].rm_so;
			eo = pmatch[idx].rm_eo;
			p++;
		}
		else if (*p == '&')
		{
			/* Use the entire matched string. */
			so = pmatch[0].rm_so;
			eo = pmatch[0].rm_eo;
			p++;
		}
		else if (*p == '\\')
		{
			/* \\ means transfer one \ to output. */
			appendStringInfoChar(str, '\\');
			p++;
			continue;
		}
		else
		{
			/*
			 * If escape char is not followed by any expected char, just treat
			 * it as ordinary data to copy.  (XXX would it be better to throw
			 * an error?)
			 */
			appendStringInfoChar(str, '\\');
			continue;
		}

		if (so >= 0 && eo >= 0)
		{
			/*
			 * Copy the text that is back reference of regexp.  Note so and eo
			 * are counted in characters not bytes.
			 */
			char	   *chunk_start;
			int			chunk_len;

			Assert(so >= data_pos);
			chunk_start = start_ptr;
			chunk_start += charlen_to_bytelen(chunk_start, so - data_pos);
			chunk_len = charlen_to_bytelen(chunk_start, eo - so);
			appendBinaryStringInfo(str, chunk_start, chunk_len);
		}
	}
}

/*
 * replace_text_regexp
 *
 * replace substring(s) in src_text that match pattern with replace_text.
 * The replace_text can contain backslash markers to substitute
 * (parts of) the matched text.
 *
 * cflags: regexp compile flags.
 * collation: collation to use.
 * search_start: the character (not byte) offset in src_text at which to
 * begin searching.
 * n: if 0, replace all matches; if > 0, replace only the N'th match.
 */
static text *
orafce_replace_text_regexp(text *src_text, text *pattern_text,
					text *replace_text,
					int cflags, Oid collation,
					int search_start, int n)
{
	text	   *ret_text;
	regex_t    *re;
	int			src_text_len = VARSIZE_ANY_EXHDR(src_text);
	int			nmatches = 0;
	StringInfoData buf;
	regmatch_t	pmatch[10];		/* main match, plus \1 to \9 */
	int			nmatch = lengthof(pmatch);
	pg_wchar   *data;
	size_t		data_len;
	size_t		data_pos;
	char	   *start_ptr;
	int			escape_status;

	initStringInfo(&buf);

	/* Convert data string to wide characters. */
	data = (pg_wchar *) palloc((src_text_len + 1) * sizeof(pg_wchar));
	data_len = pg_mb2wchar_with_len(VARDATA_ANY(src_text), data, src_text_len);

	/* Check whether replace_text has escapes, especially regexp submatches. */
	escape_status = check_replace_text_has_escape(replace_text);

#if PG_VERSION_NUM >=  150000

	/* REG_NOSUB doesn't work well in pre PostgreSQL 15 */

	/* If no regexp submatches, we can use REG_NOSUB. */
	if (escape_status < 2)
	{
		cflags |= REG_NOSUB;
		/* Also tell pg_regexec we only want the whole-match location. */
		nmatch = 1;
	}

#endif

	/* Prepare the regexp. */
	re = RE_compile_and_cache(pattern_text, cflags, collation);

	/* start_ptr points to the data_pos'th character of src_text */
	start_ptr = (char *) VARDATA_ANY(src_text);
	data_pos = 0;

	while (search_start <= (int) data_len)
	{
		int			regexec_result;

		CHECK_FOR_INTERRUPTS();

		regexec_result = pg_regexec(re,
									data,
									data_len,
									search_start,
									NULL,	/* no details */
									nmatch,
									pmatch,
									0);

		if (regexec_result == REG_NOMATCH)
			break;

		if (regexec_result != REG_OKAY)
		{
			char		errMsg[100];

			CHECK_FOR_INTERRUPTS();
			pg_regerror(regexec_result, re, errMsg, sizeof(errMsg));
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
					 errmsg("regular expression failed: %s", errMsg)));
		}

		/*
		 * Count matches, and decide whether to replace this match.
		 */
		nmatches++;
		if (n > 0 && nmatches != n)
		{
			/*
			 * No, so advance search_start, but not start_ptr/data_pos. (Thus,
			 * we treat the matched text as if it weren't matched, and copy it
			 * to the output later.)
			 */
			search_start = pmatch[0].rm_eo;
			if (pmatch[0].rm_so == pmatch[0].rm_eo)
				search_start++;
			continue;
		}

		/*
		 * Copy the text to the left of the match position.  Note we are given
		 * character not byte indexes.
		 */
		if (pmatch[0].rm_so - data_pos > 0)
		{
			int			chunk_len;

			chunk_len = charlen_to_bytelen(start_ptr,
										   pmatch[0].rm_so - data_pos);
			appendBinaryStringInfo(&buf, start_ptr, chunk_len);

			/*
			 * Advance start_ptr over that text, to avoid multiple rescans of
			 * it if the replace_text contains multiple back-references.
			 */
			start_ptr += chunk_len;
			data_pos = pmatch[0].rm_so;
		}

		/*
		 * Copy the replace_text, processing escapes if any are present.
		 */
		if (escape_status > 0)
			appendStringInfoRegexpSubstr(&buf, replace_text, pmatch,
										 start_ptr, data_pos);
		else
			appendStringInfoText(&buf, replace_text);

		/* Advance start_ptr and data_pos over the matched text. */
		start_ptr += charlen_to_bytelen(start_ptr,
										pmatch[0].rm_eo - data_pos);
		data_pos = pmatch[0].rm_eo;

		/*
		 * If we only want to replace one occurrence, we're done.
		 */
		if (n > 0)
			break;

		/*
		 * Advance search position.  Normally we start the next search at the
		 * end of the previous match; but if the match was of zero length, we
		 * have to advance by one character, or we'd just find the same match
		 * again.
		 */
		search_start = data_pos;
		if (pmatch[0].rm_so == pmatch[0].rm_eo)
			search_start++;
	}

	/*
	 * Copy the text to the right of the last match.
	 */
	if (data_pos < data_len)
	{
		int			chunk_len;

		chunk_len = ((char *) src_text + VARSIZE_ANY(src_text)) - start_ptr;
		appendBinaryStringInfo(&buf, start_ptr, chunk_len);
	}

	ret_text = cstring_to_text_with_len(buf.data, buf.len);
	pfree(buf.data);
	pfree(data);

	return ret_text;
}

#else

#define orafce_replace_text_regexp replace_text_regexp

#endif

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
		char		errMsg[100];

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
	regex_t    *cpattern;
	regmatch_t *pmatch;
	int			pmatch_len;
	int			array_len;
	int			array_idx;
	int			prev_match_end;
	int			prev_valid_match_end;
	int			maxlen = 0;		/* largest fetch length in characters */
	int			cflags;

	/* save original string --- we'll extract result substrings from it */
	matchctx->orig_str = orig_str;

	/* convert string to pg_wchar form for matching */
	orig_len = VARSIZE_ANY_EXHDR(orig_str);
	wide_str = (pg_wchar *) palloc(sizeof(pg_wchar) * (orig_len + 1));
	wide_len = pg_mb2wchar_with_len(VARDATA_ANY(orig_str), wide_str, orig_len);

	/* set up the compiled pattern */
	cflags = re_flags->cflags;

#if PG_VERSION_NUM >=  150000

	/* REG_NOSUB doesn't work well in pre PostgreSQL 15 */

	if (!use_subpatterns)
		cflags |= REG_NOSUB;

#endif

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
				if (array_len > (int) (MaxAllocSize / sizeof(int)))
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
 * regexp_instr()
 *		Return the match's position within the string
 */
Datum
orafce_regexp_instr(PG_FUNCTION_ARGS)
{
	text	   *str = NULL;
	text	   *pattern = NULL;
	int			start = 1;
	int			n = 1;
	int			endoption = 0;
	text	   *flags = NULL;
	int			subexpr = 0;
	int			pos;
	pg_re_flags re_flags;
	regexp_matches_ctx *matchctx;

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		PG_RETURN_NULL();

	str = PG_GETARG_TEXT_PP(0);
	pattern = PG_GETARG_TEXT_PP(1);

	/* Collect optional parameters */
	if (PG_NARGS() > 2)
	{
		if (PG_ARGISNULL(2))
			PG_RETURN_NULL();

		start = PG_GETARG_INT32(2);
		if (start <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("argument 'position' must be a number greater than 0")));
	}
	if (PG_NARGS() > 3)
	{
		if (PG_ARGISNULL(3))
			PG_RETURN_NULL();

		n = PG_GETARG_INT32(3);
		if (n <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("argument 'occurence' must be a number greater than 0")));
	}
	if (PG_NARGS() > 4)
	{
		if (PG_ARGISNULL(4))
			PG_RETURN_NULL();

		endoption = PG_GETARG_INT32(4);
		if (endoption != 0 && endoption != 1)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("argument 'return_opt' must be 0 or 1")));
	}
	if (PG_NARGS() > 5)
	{
		if (!PG_ARGISNULL(5))
			flags = PG_GETARG_TEXT_PP(5);
	}
	if (PG_NARGS() > 6)
	{
		if (PG_ARGISNULL(6))
			PG_RETURN_NULL();

		subexpr = PG_GETARG_INT32(6);
		if (subexpr < 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("argument 'group' must be a positive number")));
	}

	/* Determine options */
	parse_re_flags(&re_flags, flags);

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
orafce_regexp_instr_no_start(PG_FUNCTION_ARGS)
{
	return orafce_regexp_instr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
orafce_regexp_instr_no_n(PG_FUNCTION_ARGS)
{
	return orafce_regexp_instr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
orafce_regexp_instr_no_endoption(PG_FUNCTION_ARGS)
{
	return orafce_regexp_instr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
orafce_regexp_instr_no_flags(PG_FUNCTION_ARGS)
{
	return orafce_regexp_instr(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
orafce_regexp_instr_no_subexpr(PG_FUNCTION_ARGS)
{
	return orafce_regexp_instr(fcinfo);
}

/*
 * textregexreplace_noopt()
 *		Return a string matched by a regular expression, with replacement.
 *
 * This version doesn't have an option argument: we default to case
 * sensitive match, replace the first instance only.
 */
Datum
orafce_textregexreplace_noopt(PG_FUNCTION_ARGS)
{
	text	   *s;
	text	   *p;
	text	   *r;

	if (PG_ARGISNULL(1) && !PG_ARGISNULL(0))
		PG_RETURN_TEXT_P(PG_GETARG_TEXT_PP(0));

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
		PG_RETURN_NULL();

	s = PG_GETARG_TEXT_PP(0);
	p = PG_GETARG_TEXT_PP(1);
	r = PG_GETARG_TEXT_PP(2);

	PG_RETURN_TEXT_P(orafce_replace_text_regexp(s, p, r,
										 REG_ADVANCED, PG_GET_COLLATION(),
										 0, 0));
}

/*
 * textregexreplace()
 *		Return a string matched by a regular expression, with replacement.
 */
Datum
orafce_textregexreplace(PG_FUNCTION_ARGS)
{
	text	   *s;
	text	   *p;
	text	   *r;
	text	   *opt = NULL;
	pg_re_flags flags;

	/* Always return NULL when start position or occurrence are NULL */
	if (PG_NARGS() > 3 && PG_ARGISNULL(3))
		PG_RETURN_NULL();
	if (PG_NARGS() > 4 && PG_ARGISNULL(4))
		PG_RETURN_NULL();

	/*
	 * Special case for second parameter in REGEXP_REPLACE, when NULL
	 * returns the original value unless the start position or occurrences
	 * are NULL too. In this case, it returns NULL (see instruction above).
	 */
	if (PG_ARGISNULL(1) && !PG_ARGISNULL(0))
		PG_RETURN_TEXT_P(PG_GETARG_TEXT_PP(0));

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
		PG_RETURN_NULL();

	s = PG_GETARG_TEXT_PP(0);
	p = PG_GETARG_TEXT_PP(1);
	r = PG_GETARG_TEXT_PP(2);

	if (!PG_ARGISNULL(3))
		opt = PG_GETARG_TEXT_PP(3);

	/*
	 * regexp_replace() with four arguments will be preferentially resolved as
	 * this form when the fourth argument is of type UNKNOWN.  However, the
	 * user might have intended to call textregexreplace_extended_no_n.  If we
	 * see flags that look like an integer, emit the same error that
	 * parse_re_flags would, but add a HINT about how to fix it.
	 */
	if (opt && VARSIZE_ANY_EXHDR(opt) > 0)
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

	PG_RETURN_TEXT_P(orafce_replace_text_regexp(s, p, r,
										 flags.cflags, PG_GET_COLLATION(),
										 0, 0));
}

/*
 * textregexreplace_extended()
 *		Return a string matched by a regular expression, with replacement.
 *		Extends textregexreplace by allowing a start position and the
 *		choice of the occurrence to replace (0 means all occurrences).
 */
Datum
orafce_textregexreplace_extended(PG_FUNCTION_ARGS)
{
	text	   *s;
	text	   *p;
	text	   *r;
	int			start = 1;
	int			n = 1;
	text	   *flags = NULL;
	pg_re_flags re_flags;

	/* Always return NULL when start position or occurrence are NULL */
	if (PG_NARGS() > 3 && PG_ARGISNULL(3))
		PG_RETURN_NULL();
	if (PG_NARGS() > 4 && PG_ARGISNULL(4))
		PG_RETURN_NULL();

	/*
	 * Special case for second parameter in REGEXP_REPLACE, when NULL
	 * returns the original value unless the start position or occurrences
	 * are NULL too. In this case, it returns NULL (see instruction above).
	 */
	if (PG_ARGISNULL(1) && !PG_ARGISNULL(0))
		PG_RETURN_TEXT_P(PG_GETARG_TEXT_PP(0));

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
		PG_RETURN_NULL();

	s = PG_GETARG_TEXT_PP(0);
	p = PG_GETARG_TEXT_PP(1);
	r = PG_GETARG_TEXT_PP(2);

	/* Collect optional parameters */
	if (PG_NARGS() > 3)
	{
		start = PG_GETARG_INT32(3);
		if (start <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("argument 'position' must be a number greater than 0")));
	}
	if (PG_NARGS() > 4)
	{
		n = PG_GETARG_INT32(4);
		if (n < 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("argument 'occurrence' must be a positive number")));
	}
	if (PG_NARGS() > 5)
	{
		if (!PG_ARGISNULL(5))
			flags = PG_GETARG_TEXT_PP(5);
	}

	/* Determine options */
	parse_re_flags(&re_flags, flags);

	/* The global modifier is not allowed with Oracle */
	if (re_flags.glob)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("modifier 'g' is not supported by this function")));

	/*
	 * If N was not specified, force the 'g' modifier. This is the
	 * default in Oracle when no occurence is specified.
	 */
	if (PG_NARGS() <= 4)
		n = 0;

	/* Do the replacement(s) */
	PG_RETURN_TEXT_P(orafce_replace_text_regexp(s, p, r,
										 re_flags.cflags, PG_GET_COLLATION(),
										 start - 1, n));
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
orafce_textregexreplace_extended_no_n(PG_FUNCTION_ARGS)
{
	return orafce_textregexreplace_extended(fcinfo);
}

/* This is separate to keep the opr_sanity regression test from complaining */
Datum
orafce_textregexreplace_extended_no_flags(PG_FUNCTION_ARGS)
{
	return orafce_textregexreplace_extended(fcinfo);
}

#include "postgres.h"

#include "parse_keyword.h"

#include "parser/scanner.h"

#if PG_VERSION_NUM >= 90600

#include "common/keywords.h"

#else

#include "parser/keywords.h"

#endif

#if PG_VERSION_NUM >= 120000

const char *
orafce_scan_keyword(const char *text, int *keycode)
{
	int		kwnum;

	kwnum = ScanKeywordLookup(text, &ScanKeywords);
	if (kwnum >= 0)
	{
		*keycode = ScanKeywordTokens[kwnum];
		return GetScanKeyword(kwnum, &ScanKeywords);
	}

	return NULL;
}

#else

const char *
orafce_scan_keyword(const char *text, int *keycode)
{
	const ScanKeyword *keyword;

	keyword = ScanKeywordLookup(text, ScanKeywords, NumScanKeywords);
	if (keyword)
	{
		*keycode = keyword->value;
		return keyword->name;
	}

	return NULL;
}

#endif

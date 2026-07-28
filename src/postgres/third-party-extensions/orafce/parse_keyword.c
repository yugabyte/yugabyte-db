#include "postgres.h"
#include "parse_keyword.h"
#include "parser/scanner.h"
#include "common/keywords.h"

const char *
orafce_scan_keyword(const char *text, int *keycode)
{
	int			kwnum;

	kwnum = ScanKeywordLookup(text, &ScanKeywords);
	if (kwnum >= 0)
	{
		*keycode = ScanKeywordTokens[kwnum];
		return GetScanKeyword(kwnum, &ScanKeywords);
	}

	return NULL;
}

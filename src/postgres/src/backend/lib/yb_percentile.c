#include "postgres.h"

#include <math.h>
#include <regex.h>

#include "utils/jsonb.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#define MAX_INTERVAL_LEN 100

typedef enum HistParseState
{
	NONE,
	ARRAY_BEGIN,
	OBJECT_BEGIN,
	KEY,
	VALUE,
	OBJECT_END,
	ARRAY_END
} HistParseState;

typedef struct HistPair
{
	double end;
	int64_t total;
} HistPair;

typedef struct HistInterval
{
	double start;
	double end;
} HistInterval;

static double extract_from_match(const char *str, regmatch_t *match, int match_num)
{
	int start = match[match_num].rm_so;
	int end = match[match_num].rm_eo;
	if (start == -1 || end == -1 )
		return INFINITY;
	char num_str[MAX_INTERVAL_LEN];
	strncpy(num_str, str + start, end - start);
	num_str[end - start] = '\0';
	return atof(num_str);
}

static HistInterval extract_interval(regex_t *regex, const char *str, int len)
{
	regmatch_t match[7];
	double start;
	double end;

	char str_cpy[MAX_INTERVAL_LEN];
	strncpy(str_cpy, str, len);
	str_cpy[len] = '\0';
	if (0 == regexec(regex, str_cpy, 7, match, 0))
	{
		start = extract_from_match(str_cpy, match, 1);
		end = extract_from_match(str_cpy, match, 4);
	}
	else
		elog(ERROR, "Input %s did not match either [num1,num2) or [num1,) regex", str);

	if (start == INFINITY)
		elog(ERROR, "Interval start is mismatched or missing");
	if (end <= start)
		elog(ERROR, "Unexpected histogram interval where where start >= end");

	return (struct HistInterval){start, end};
}

Datum
yb_get_percentile(PG_FUNCTION_ARGS)
{
	Jsonb *jsonb = PG_GETARG_JSONB_P(0);
	double percentile = PG_GETARG_FLOAT8(1);
	JsonbIterator *it = JsonbIteratorInit(&jsonb->root);
	JsonbValue val;
	JsonbIteratorToken token;

	HistParseState h_state = NONE;
	int64_t total_count = 0;
	int total_entries = 0;
	int allocated_entries = 100;
	HistPair *entries = palloc(allocated_entries * sizeof(HistPair));
	HistInterval interval;
	double last_interval_end = -INFINITY;
	double ret = 0;

	regex_t regex;
	const char *pattern = "^\\[([-+]?[0-9]+(\\.[0-9]+)?(e[-+]?[0-9]+)?),([-+]?[0-9]+(\\.[0-9]+)?(e[-+]?[0-9]+)?)?\\)$";
	if (regcomp(&regex, pattern, REG_EXTENDED))
		Assert(false);

	percentile = percentile > 0.0 ? percentile : 0.0;
	percentile = percentile < 100.0 ? percentile : 100.0;

	MemoryContext tmpContext = AllocSetContextCreate(GetCurrentMemoryContext(),
		"JSONB processing temporary context", ALLOCSET_DEFAULT_SIZES);
	MemoryContext oldContext = MemoryContextSwitchTo(tmpContext);

	while ((token = JsonbIteratorNext(&it, &val, false)) != WJB_DONE)
	{
		switch (token)
		{
			case WJB_BEGIN_ARRAY:
				if (h_state != NONE)
					elog(ERROR, "Invalid histogram: Unexpected array beginning, should only be the first json element");
				h_state = ARRAY_BEGIN;
				break;
			case WJB_BEGIN_OBJECT:
				if (h_state != OBJECT_END && h_state != ARRAY_BEGIN)
					elog(ERROR, "Invalid histogram: Unexpected object beginning, should follow prior object or array beginning");
				h_state = OBJECT_BEGIN;
				break;
			case WJB_KEY:
				if (h_state != OBJECT_BEGIN)
					elog(ERROR, "Invalid histogram: Unexpected key, should follow object beginning");
				if (val.type != jbvString)
					elog(ERROR, "Invalid histogram: Unexpected key that is not of string type");
				h_state = KEY;
				interval = extract_interval(&regex, val.val.string.val,
					val.val.string.len);
				if (interval.start < last_interval_end)
					elog(ERROR, "Invalid histogram: Unexpected interval intersection between keys");
				last_interval_end = interval.end;
				break;
			case WJB_VALUE:
				if (h_state != KEY)
					elog(ERROR, "Invalid histogram: Unexpected value, should follow key within object");
				if (val.type != jbvNumeric)
					elog(ERROR, "Invalid histogram: Unexpected value that is not of numeric type");
				int64_t count = DatumGetInt64(DirectFunctionCall1(numeric_int8,
					NumericGetDatum(val.val.numeric)));
				if (count < 0)
					elog(ERROR, "Invalid histogram: Unexpected negative count value");
				if (count > 0)
				{
					total_count += count;
					h_state = VALUE;
					if (total_entries >= allocated_entries)
					{
						allocated_entries *= 2;
						entries = repalloc(entries,
							allocated_entries * sizeof(HistPair));
					}
					entries[total_entries] =
						(struct HistPair){last_interval_end, total_count};
					total_entries++;
				}
				break;
			case WJB_END_OBJECT:
				if (h_state != VALUE)
					elog(ERROR, "Invalid histogram: Unexpected object end, should follow k/v pair within object");
				h_state = OBJECT_END;
				break;
			case WJB_END_ARRAY:
				if (h_state != OBJECT_END && h_state != ARRAY_BEGIN)
					elog(ERROR, "Invalid histogram: Unexpected array end, should follow valid objects or array beginning");
				h_state = ARRAY_END;
				break;
			default:
				elog(ERROR, "Invalid histogram: Unexpected node found that is not an array, object, or k/v pair");
				break;
		}
	}

	/* Covers all 0 bucket counts case, as well as empty array */
	if (total_count == 0)
	{
		MemoryContextSwitchTo(oldContext);
		MemoryContextDelete(tmpContext);
		PG_RETURN_FLOAT8(-INFINITY);
	}

	int64_t expected_min_count = ((percentile / 100) * total_count) + 0.5;
	/* Always want a minimum count of at least 1 */
	expected_min_count = 0 < expected_min_count ? expected_min_count : 1;

	for (int i = total_entries - 1;
		i >= 0 && entries[i].total >= expected_min_count; i--)
		ret = entries[i].end;

	MemoryContextSwitchTo(oldContext);
	MemoryContextDelete(tmpContext);
	PG_RETURN_FLOAT8(ret);
}

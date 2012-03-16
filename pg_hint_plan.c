/*-------------------------------------------------------------------------
 *
 * pg_hint_plan.c
 *		  do instructions or hints to the planner using C-style block comments
 * 		  of the SQL.
 *
 * Copyright (c) 2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "miscadmin.h"
#include "nodes/print.h"
#include "optimizer/cost.h"
#include "optimizer/joininfo.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/plancat.h"
#include "optimizer/planner.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/guc.h"
#include "utils/memutils.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#if PG_VERSION_NUM != 90200
#error unsupported PostgreSQL version
#endif

#define HINT_START	"/*"
#define HINT_END	"*/"

/* hint keywords */
#define HINT_SEQSCAN		"SeqScan"
#define HINT_INDEXSCAN		"IndexScan"
#define HINT_BITMAPSCAN		"BitmapScan"
#define HINT_TIDSCAN		"TidScan"
#define HINT_NOSEQSCAN		"NoSeqScan"
#define HINT_NOINDEXSCAN	"NoIndexScan"
#define HINT_NOBITMAPSCAN	"NoBitmapScan"
#define HINT_NOTIDSCAN		"NoTidScan"
#define HINT_NESTLOOP		"NestLoop"
#define HINT_MERGEJOIN		"MergeJoin"
#define HINT_HASHJOIN		"HashJoin"
#define HINT_NONESTLOOP		"NoNestLoop"
#define HINT_NOMERGEJOIN	"NoMergeJoin"
#define HINT_NOHASHJOIN		"NoHashJoin"
#define HINT_LEADING		"Leading"
#define HINT_SET			"Set"

#if PG_VERSION_NUM >= 90200
#define	HINT_INDEXONLYSCAN	"IndexonlyScan";
#endif

#define HINT_ARRAY_DEFAULT_INITSIZE 8

#define parse_ereport(str, detail) \
	ereport(pg_hint_plan_parse_message, \
			(errmsg("hint syntax error at or near \"%s\"", (str)), \
			 errdetail detail))

#define skip_space(str) \
	while (isspace(*str)) \
		str++;

enum
{
	ENABLE_SEQSCAN		= 0x01,
	ENABLE_INDEXSCAN	= 0x02,
	ENABLE_BITMAPSCAN	= 0x04,
	ENABLE_TIDSCAN		= 0x08,
	ENABLE_NESTLOOP		= 0x10,
	ENABLE_MERGEJOIN	= 0x20,
	ENABLE_HASHJOIN		= 0x40
} TYPE_BITS;

#define ENABLE_ALL_SCAN (ENABLE_SEQSCAN | ENABLE_INDEXSCAN | ENABLE_BITMAPSCAN \
						| ENABLE_TIDSCAN)
#define ENABLE_ALL_JOIN (ENABLE_NESTLOOP | ENABLE_MERGEJOIN | ENABLE_HASHJOIN)

/* scan method hints */
typedef struct ScanHint
{
	const char	   *opt_str;		/* must not do pfree */
	char		   *relname;
	List		   *indexnames;
	unsigned char	enforce_mask;
} ScanHint;

/* join method hints */
typedef struct JoinHint
{
	const char	   *opt_str;		/* must not do pfree */
	int				nrels;
	char		  **relnames;
	unsigned char	enforce_mask;
	Relids			joinrelids;
} JoinHint;

/* change a run-time parameter hints */
typedef struct SetHint
{
	char   *name;				/* name of variable */
	char   *value;
} SetHint;

typedef struct PlanHint
{
	char	   *hint_str;

	int			nscan_hints;
	int			max_scan_hints;
	ScanHint  **scan_hints;

	int			njoin_hints;
	int			max_join_hints;
	JoinHint  **join_hints;

	int			nlevel;
	List	  **join_hint_level;

	List	   *leading;

	GucContext	context;
	List	   *set_hints;
} PlanHint;

typedef const char *(*HintParserFunction) (PlanHint *plan, Query *parse, char *keyword, const char *str);

typedef struct HintParser
{
	char   *keyword;
	bool	have_paren;
	HintParserFunction	hint_parser;
} HintParser;

/* Module callbacks */
void		_PG_init(void);
void		_PG_fini(void);


#define HASH_ENTRIES 201
typedef struct tidlist
{
	int nrels;
	Oid *oids;
} TidList;
typedef struct hash_entry
{
	TidList tidlist;
	unsigned char enforce_mask;
	struct hash_entry *next;
} HashEntry;
static HashEntry *hashent[HASH_ENTRIES];
static bool print_log = false;
/* Join Method Hints */
typedef struct RelIdInfo
{
	Index		relid;
	Oid			oid;
	Alias	   *eref;
} RelIdInfo;

typedef struct GucVariables
{
	bool	enable_seqscan;
	bool	enable_indexscan;
	bool	enable_bitmapscan;
	bool	enable_tidscan;
	bool	enable_sort;
	bool	enable_hashagg;
	bool	enable_nestloop;
	bool	enable_material;
	bool	enable_mergejoin;
	bool	enable_hashjoin;
} GucVariables;


static PlannedStmt *my_planner(Query *parse, int cursorOptions,
							   ParamListInfo boundParams);
static void my_get_relation_info(PlannerInfo *root, Oid relationObjectId,
								 bool inhparent, RelOptInfo *rel);
static RelOptInfo *my_join_search(PlannerInfo *root, int levels_needed,
								  List *initial_rels);


static void backup_guc(GucVariables *backup);
static void restore_guc(GucVariables *backup);
static void set_guc(unsigned char enforce_mask);
static void build_join_hints(PlannerInfo *root, int level, List *initial_rels);
static RelOptInfo *my_make_join_rel(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2);
static void my_join_search_one_level(PlannerInfo *root, int level);
static void make_rels_by_clause_joins(PlannerInfo *root, RelOptInfo *old_rel, ListCell *other_rels);
static void make_rels_by_clauseless_joins(PlannerInfo *root, RelOptInfo *old_rel, ListCell *other_rels);
static bool has_join_restriction(PlannerInfo *root, RelOptInfo *rel);


/* GUC variables */
static bool pg_hint_plan_enable = true;
static bool pg_hint_plan_debug_print = false;
static int pg_hint_plan_parse_message = INFO;

static const struct config_enum_entry parse_message_level_options[] = {
	{"debug", DEBUG2, true},
	{"debug5", DEBUG5, false},
	{"debug4", DEBUG4, false},
	{"debug3", DEBUG3, false},
	{"debug2", DEBUG2, false},
	{"debug1", DEBUG1, false},
	{"log", LOG, false},
	{"info", INFO, false},
	{"notice", NOTICE, false},
	{"warning", WARNING, false},
	{"error", ERROR, false},
	/*
	{"fatal", FATAL, true},
	{"panic", PANIC, true},
	 */
	{NULL, 0, false}
};

/* Saved hook values in case of unload */
static planner_hook_type prev_planner_hook = NULL;
static get_relation_info_hook_type prev_get_relation_info = NULL;
static join_search_hook_type prev_join_search = NULL;

/* フック関数をまたがって使用する情報を管理する */
static PlanHint *global = NULL;

static const HintParser parsers[] = {
/*
	{HINT_SEQSCAN, true, ParseScanMethod},
	{HINT_INDEXSCAN, true, ParseIndexScanMethod},
	{HINT_BITMAPSCAN, true, ParseIndexScanMethod},
	{HINT_TIDSCAN, true, ParseScanMethod},
	{HINT_NOSEQSCAN, true, ParseScanMethod},
	{HINT_NOINDEXSCAN, true, ParseScanMethod},
	{HINT_NOBITMAPSCAN, true, ParseScanMethod},
	{HINT_NOTIDSCAN, true, ParseScanMethod},
	{HINT_NESTLOOP, true, ParseJoinMethod},
	{HINT_MERGEJOIN, true, ParseJoinMethod},
	{HINT_HASHJOIN, true, ParseJoinMethod},
	{HINT_NONESTLOOP, true, ParseJoinMethod},
	{HINT_NOMERGEJOIN, true, ParseJoinMethod},
	{HINT_NOHASHJOIN, true, ParseJoinMethod},
	{HINT_LEADING, true, ParseLeading},
	{HINT_SET, true, ParseSet},
*/
	{NULL, false, NULL},
};

/*
 * Module load callbacks
 */
void
_PG_init(void)
{
	/* Define custom GUC variables. */
	DefineCustomBoolVariable("pg_hint_plan.enable",
			 "Instructions or hints to the planner using block comments.",
							 NULL,
							 &pg_hint_plan_enable,
							 true,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("pg_hint_plan.debug_print",
							 "Logs each query's parse results of the hint.",
							 NULL,
							 &pg_hint_plan_debug_print,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomEnumVariable("pg_hint_plan.parse_message",
							 "Messege level of the parse error.",
							 NULL,
							 &pg_hint_plan_parse_message,
							 INFO,
							 parse_message_level_options,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	/* Install hooks. */
	prev_planner_hook = planner_hook;
	planner_hook = my_planner;
	prev_get_relation_info = get_relation_info_hook;
	get_relation_info_hook = my_get_relation_info;
	prev_join_search = join_search_hook;
	join_search_hook = my_join_search;
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	/* Uninstall hooks. */
	planner_hook = prev_planner_hook;
	get_relation_info_hook = prev_get_relation_info;
	join_search_hook = prev_join_search;
}

static ScanHint *
ScanHintCreate(void)
{
	ScanHint	   *hint;

	hint = palloc(sizeof(ScanHint));
	hint->opt_str = NULL;
	hint->relname = NULL;
	hint->indexnames = NIL;
	hint->enforce_mask = 0;

	return hint;
}

static void
ScanHintDelete(ScanHint *hint)
{
	if (!hint)
		return;

	if (hint->relname)
		pfree(hint->relname);
	list_free_deep(hint->indexnames);
	pfree(hint);
}

static JoinHint *
JoinHintCreate(void)
{
	JoinHint   *hint;

	hint = palloc(sizeof(JoinHint));
	hint->opt_str = NULL;
	hint->nrels = 0;
	hint->relnames = NULL;
	hint->enforce_mask = 0;
	hint->joinrelids = NULL;

	return hint;
}

static void
JoinHintDelete(JoinHint *hint)
{
	if (!hint)
		return;

	if (hint->relnames)
	{
		int	i;

		for (i = 0; i < hint->nrels; i++)
			pfree(hint->relnames[i]);
		pfree(hint->relnames);
	}
	bms_free(hint->joinrelids);
	pfree(hint);
}

static SetHint *
SetHintCreate(void)
{
	SetHint	   *hint;

	hint = palloc(sizeof(SetHint));
	hint->name = NULL;
	hint->value = NULL;

	return hint;
}

static void
SetHintDelete(SetHint *hint)
{
	if (!hint)
		return;

	if (hint->name)
		pfree(hint->name);
	if (hint->value)
		pfree(hint->value);
	pfree(hint);
}

static PlanHint *
PlanHintCreate(void)
{
	PlanHint   *hint;

	hint = palloc(sizeof(PlanHint));
	hint->hint_str = NULL;
	hint->nscan_hints = 0;
	hint->max_scan_hints = 0;
	hint->scan_hints = NULL;
	hint->njoin_hints = 0;
	hint->max_join_hints = 0;
	hint->join_hints = NULL;
	hint->nlevel = 0;
	hint->join_hint_level = NULL;
	hint->context = superuser() ? PGC_SUSET : PGC_USERSET;
	hint->set_hints = NIL;
	hint->leading = NIL;

	return hint;
}

static void
PlanHintDelete(PlanHint *hint)
{
	ListCell   *l;
	int			i;

	if (!hint)
		return;

	if (hint->hint_str)
		pfree(hint->hint_str);

	for (i = 0; i < hint->nscan_hints; i++)
		ScanHintDelete(hint->scan_hints[i]);
	if (hint->scan_hints)
		pfree(hint->scan_hints);

	for (i = 0; i < hint->njoin_hints; i++)
		JoinHintDelete(hint->join_hints[i]);
	if (hint->join_hints)
		pfree(hint->join_hints);

	for (i = 2; i <= hint->nlevel; i++)
		list_free(hint->join_hint_level[i]);
	if (hint->join_hint_level)
		pfree(hint->join_hint_level);

	foreach(l, hint->set_hints)
		SetHintDelete((SetHint *) lfirst(l));
	list_free(hint->set_hints);

	list_free_deep(hint->leading);

	pfree(hint);
}

static bool
PlanHintIsempty(PlanHint *hint)
{
	if (hint->nscan_hints == 0 &&
		hint->njoin_hints == 0 &&
		hint->set_hints == NIL &&
		hint->leading == NIL)
		return true;

	return false;
}

// TODO オブジェクト名のクォート処理を追加
static void
PlanHintDump(PlanHint *hint)
{
	StringInfoData	buf;
	ListCell	   *l;
	int				i;
	bool			is_first = true;

	if (!hint)
	{
		elog(INFO, "no hint");
		return;
	}

	initStringInfo(&buf);
	appendStringInfo(&buf, "/*\n");
	for (i = 0; i < hint->nscan_hints; i++)
	{
		ScanHint   *h = hint->scan_hints[i];
		ListCell   *n;
		switch(h->enforce_mask)
		{
			case(ENABLE_SEQSCAN):
				appendStringInfo(&buf, "%s(", HINT_SEQSCAN);
				break;
			case(ENABLE_INDEXSCAN):
				appendStringInfo(&buf, "%s(", HINT_INDEXSCAN);
				break;
			case(ENABLE_BITMAPSCAN):
				appendStringInfo(&buf, "%s(", HINT_BITMAPSCAN);
				break;
			case(ENABLE_TIDSCAN):
				appendStringInfo(&buf, "%s(", HINT_TIDSCAN);
				break;
			case(ENABLE_INDEXSCAN | ENABLE_BITMAPSCAN | ENABLE_TIDSCAN):
				appendStringInfo(&buf, "%s(", HINT_NOSEQSCAN);
				break;
			case(ENABLE_SEQSCAN | ENABLE_BITMAPSCAN | ENABLE_TIDSCAN):
				appendStringInfo(&buf, "%s(", HINT_NOINDEXSCAN);
				break;
			case(ENABLE_SEQSCAN | ENABLE_INDEXSCAN | ENABLE_TIDSCAN):
				appendStringInfo(&buf, "%s(", HINT_NOBITMAPSCAN);
				break;
			case(ENABLE_SEQSCAN | ENABLE_INDEXSCAN | ENABLE_BITMAPSCAN):
				appendStringInfo(&buf, "%s(", HINT_NOTIDSCAN);
				break;
			default:
				appendStringInfoString(&buf, "\?\?\?(");
				break;
		}
		appendStringInfo(&buf, "%s", h->relname);
		foreach(n, h->indexnames)
			appendStringInfo(&buf, " %s", (char *) lfirst(n));
		appendStringInfoString(&buf, ")\n");
	}

	for (i = 0; i < hint->njoin_hints; i++)
	{
		JoinHint   *h = hint->join_hints[i];
		int			i;
		switch(h->enforce_mask)
		{
			case(ENABLE_NESTLOOP):
				appendStringInfo(&buf, "%s(", HINT_NESTLOOP);
				break;
			case(ENABLE_MERGEJOIN):
				appendStringInfo(&buf, "%s(", HINT_MERGEJOIN);
				break;
			case(ENABLE_HASHJOIN):
				appendStringInfo(&buf, "%s(", HINT_HASHJOIN);
				break;
			case(ENABLE_ALL_JOIN ^ ENABLE_NESTLOOP):
				appendStringInfo(&buf, "%s(", HINT_NONESTLOOP);
				break;
			case(ENABLE_ALL_JOIN ^ ENABLE_MERGEJOIN):
				appendStringInfo(&buf, "%s(", HINT_NOMERGEJOIN);
				break;
			case(ENABLE_ALL_JOIN ^ ENABLE_HASHJOIN):
				appendStringInfo(&buf, "%s(", HINT_NOHASHJOIN);
				break;
			case(ENABLE_ALL_JOIN):
				continue;
			default:
				appendStringInfoString(&buf, "\?\?\?(");
				break;
		}
		appendStringInfo(&buf, "%s", h->relnames[0]);
		for (i = 1; i < h->nrels; i++)
			appendStringInfo(&buf, " %s", h->relnames[i]);
		appendStringInfoString(&buf, ")\n");
	}

	foreach(l, hint->set_hints)
	{
		SetHint	   *h = (SetHint *) lfirst(l);
		appendStringInfo(&buf, "%s(%s %s)\n", HINT_SET, h->name, h->value);
	}

	foreach(l, hint->leading)
	{
		if (is_first)
		{
			appendStringInfo(&buf, "%s(%s", HINT_LEADING, (char *)lfirst(l));
			is_first = false;
		}
		else
			appendStringInfo(&buf, " %s", (char *)lfirst(l));
	}
	if (!is_first)
		appendStringInfoString(&buf, ")\n");

	appendStringInfoString(&buf, "*/");

	elog(INFO, "%s", buf.data);

	pfree(buf.data);
}

static int
RelnameCmp(const void *a, const void *b)
{
	const char *relnamea = *((const char **) a);
	const char *relnameb = *((const char **) b);

	return strcmp(relnamea, relnameb);
}

static int
ScanHintCmp(const void *a, const void *b, bool order)
{
	const ScanHint	   *hinta = *((const ScanHint **) a);
	const ScanHint	   *hintb = *((const ScanHint **) b);
	int					result;

	if ((result = strcmp(hinta->relname, hintb->relname)) != 0)
		return result;

	/* ヒント句で指定した順を返す */
	if (order)
		return hinta->opt_str - hintb->opt_str;
	else
		return 0;
}

static int
ScanHintCmpIsOrder(const void *a, const void *b)
{
	return ScanHintCmp(a, b, true);
}

static int
JoinHintCmp(const void *a, const void *b, bool order)
{
	const JoinHint	   *hinta = *((const JoinHint **) a);
	const JoinHint	   *hintb = *((const JoinHint **) b);

	if (hinta->nrels == hintb->nrels)
	{
		int	i;
		for (i = 0; i < hinta->nrels; i++)
		{
			int	result;
			if ((result = strcmp(hinta->relnames[i], hintb->relnames[i])) != 0)
				return result;
		}

		/* ヒント句で指定した順を返す */
		if (order)
			return hinta->opt_str - hintb->opt_str;
		else
			return 0;
	}

	return hinta->nrels - hintb->nrels;
}

static int
JoinHintCmpIsOrder(const void *a, const void *b)
{
	return JoinHintCmp(a, b, true);
}

static int
set_config_options(List *options, GucContext context)
{
	ListCell   *l;
	int			save_nestlevel;
	int			result = 1;

	save_nestlevel = NewGUCNestLevel();

	foreach(l, options)
	{
		SetHint	   *hint = (SetHint *) lfirst(l);

		if (result > 0)
			result = set_config_option(hint->name, hint->value, context,
						PGC_S_SESSION, GUC_ACTION_SAVE, true,
						pg_hint_plan_parse_message);
	}

	return save_nestlevel;
}

#define SET_CONFIG_OPTION(name, enforce_mask, type_bits) \
	set_config_option((name), \
		((enforce_mask) & (type_bits)) ? "true" : "false", \
		context, PGC_S_SESSION, GUC_ACTION_SAVE, true, ERROR)

static void
set_join_config_options(unsigned char enforce_mask, GucContext context)
{
	SET_CONFIG_OPTION("enable_nestloop", enforce_mask, ENABLE_NESTLOOP);
	SET_CONFIG_OPTION("enable_mergejoin", enforce_mask, ENABLE_MERGEJOIN);
	SET_CONFIG_OPTION("enable_hashjoin", enforce_mask, ENABLE_HASHJOIN);
}

static void
set_scan_config_options(unsigned char enforce_mask, GucContext context)
{
	SET_CONFIG_OPTION("enable_seqscan", enforce_mask, ENABLE_SEQSCAN);
	SET_CONFIG_OPTION("enable_indexscan", enforce_mask, ENABLE_INDEXSCAN);
	SET_CONFIG_OPTION("enable_bitmapscan", enforce_mask, ENABLE_BITMAPSCAN);
	SET_CONFIG_OPTION("enable_tidscan", enforce_mask, ENABLE_TIDSCAN);
#if PG_VERSION_NUM >= 90200
	SET_CONFIG_OPTION("enable_indexonlyscan", enforce_mask, ENABLE_INDEXSCAN);
#endif
}

/*
 * parse functions
 */

static const char *
parse_keyword(const char *str, StringInfo buf)
{
	skip_space(str);

	while (!isspace(*str) && *str != '(' && *str != '\0')
		appendStringInfoChar(buf, *str++);

	return str;
}

static const char *
skip_opened_parenthesis(const char *str)
{
	skip_space(str);

	if (*str != '(')
	{
		parse_ereport(str, ("Opened parenthesis is necessary."));
		return NULL;
	}

	str++;

	return str;
}

static const char *
skip_closed_parenthesis(const char *str)
{
	skip_space(str);

	if (*str != ')')
	{
		parse_ereport(str, ("Closed parenthesis is necessary."));
		return NULL;
	}

	str++;

	return str;
}

static const char *
parse_quote_value(const char *str, char **word, char *value_type)
{
	StringInfoData	buf;
	bool			in_quote;

	skip_space(str);

	initStringInfo(&buf);
	if (*str == '"')
	{
		str++;
		in_quote = true;
	}
	else
	{
		/*
		 * 1文字目以降の制限の適用
		 */
		if (!isalpha(*str) && *str != '_')
		{
			pfree(buf.data);
			parse_ereport(str, ("Need for %s to be quoted.", value_type));
			return NULL;
		}

		in_quote = false;
		appendStringInfoCharMacro(&buf, *str++);
	}

	while (true)
	{
		if (in_quote)
		{
			if (*str == '\0')
			{
				pfree(buf.data);
				parse_ereport(str, ("Unterminated quoted %s.", value_type));
				return NULL;
			}

			/*
			 * エスケープ対象をスキップする。
			 * TODO エスケープ対象の仕様にあわせた処理を行う。
			 */
			if(*str == '"')
			{
				str++;
				if (*str != '"')
					break;
			}
		}
		else
		{
			/*
			 * 2文字目以降の制限の適用
			 */
			if (!isalnum(*str) && *str != '_' && *str != '$')
				break;
		}

		appendStringInfoCharMacro(&buf, *str++);
	}

	if (buf.len == 0)
	{
		pfree(buf.data);
		parse_ereport(str, ("%s is necessary.", value_type));
		return NULL;
	}

	*word = buf.data;

	return str;
}

static const char *
skip_option_delimiter(const char *str)
{
	const char *p = str;

	skip_space(str);

	if (str == p)
	{
		parse_ereport(str, ("Must be specified space."));
		return NULL;
	}

	return str;
}

static bool
parse_hints(PlanHint *plan, Query *parse, const char *str)
{
	StringInfoData	buf;
	char		   *head;

	initStringInfo(&buf);
	while (*str != '\0')
	{
		const HintParser *parser;

		/* in error message, we output the comment including the keyword. */
		head = (char *) str;

		/* parse only the keyword of the hint. */
		resetStringInfo(&buf);
		str = parse_keyword(str, &buf);

		for (parser = parsers; parser->keyword != NULL; parser++)
		{
			char   *keyword = parser->keyword;

			if (strcasecmp(buf.data, keyword) != 0)
				continue;

			if (parser->have_paren)
			{
				/* parser of each hint does parse in a parenthesis. */
				if ((str = skip_opened_parenthesis(str)) == NULL ||
					(str = parser->hint_parser(plan, parse, keyword, str)) == NULL ||
					(str = skip_closed_parenthesis(str)) == NULL)
				{
					pfree(buf.data);
					return false;
				}
			}
			else
			{
				if ((str = parser->hint_parser(plan, parse, keyword, str)) == NULL)
				{
					pfree(buf.data);
					return false;
				}

				/*
				 * 直前のヒントに括弧の指定がなければ次のヒントの間に空白が必要
				 */
				if (!isspace(*str) && *str == '\0')
					parse_ereport(str, ("Delimiter of the hint is necessary."));
			}

			skip_space(str);

			break;
		}

		if (parser->keyword == NULL)
		{
			parse_ereport(head, ("Keyword \"%s\" does not exist.", buf.data));
			pfree(buf.data);
			return false;
		}
	}

	pfree(buf.data);

	return true;
}

/*
 * Do basic parsing of the query head comment.
 */
static PlanHint *
parse_head_comment(Query *parse)
{
	const char	   *p;
	char		   *head;
	char		   *tail;
	int				len;
	int				i;
	PlanHint	   *plan;

	/* get client-supplied query string. */
	p = debug_query_string;
	if (p == NULL)
		return NULL;


	/* extract query head comment. */
	len = strlen(HINT_START);
	skip_space(p);
	if (strncmp(p, HINT_START, len))
		return NULL;

	p += len;
	skip_space(p);

	if ((tail = strstr(p, HINT_END)) == NULL)
		elog(ERROR, "unterminated /* comment at or near \"%s\"",
			 debug_query_string);

	/* 入れ子にしたブロックコメントはサポートしない */
	if ((head = strstr(p, HINT_START)) != NULL && head < tail)
		parse_ereport(head, ("block comments nest doesn't supported"));

	/* ヒント句部分を切り出す */
	len = tail - p;
	head = palloc(len + 1);
	memcpy(head, p, len);
	head[len] = '\0';
	p = head;

	plan = PlanHintCreate();
	plan->hint_str = head;

	/* parse each hint. */
	if (!parse_hints(plan, parse, p))
		return plan;

	/* 重複したScan条件をを除外する */
	qsort(plan->scan_hints, plan->nscan_hints, sizeof(ScanHint *), ScanHintCmpIsOrder);
	for (i = 0; i < plan->nscan_hints - 1;)
	{
		int	result = ScanHintCmp(plan->scan_hints + i,
						plan->scan_hints + i + 1, false);
		if (result != 0)
			i++;
		else
		{
			/* 後で指定したヒントを有効にする */
			plan->nscan_hints--;
			memmove(plan->scan_hints + i, plan->scan_hints + i + 1,
					sizeof(ScanHint *) * (plan->nscan_hints - i));
		}
	}

	/* 重複したJoin条件をを除外する */
	qsort(plan->join_hints, plan->njoin_hints, sizeof(JoinHint *), JoinHintCmpIsOrder);
	for (i = 0; i < plan->njoin_hints - 1;)
	{
		int	result = JoinHintCmp(plan->join_hints + i,
						plan->join_hints + i + 1, false);
		if (result != 0)
			i++;
		else
		{
			/* 後で指定したヒントを有効にする */
			plan->njoin_hints--;
			memmove(plan->join_hints + i, plan->join_hints + i + 1,
					sizeof(JoinHint *) * (plan->njoin_hints - i));
		}
	}

	return plan;
}

static PlannedStmt *
my_planner(Query *parse, int cursorOptions, ParamListInfo boundParams)
{
	int				save_nestlevel;
	PlannedStmt	   *result;

	/*
	 * hintが指定されない、または空のhintを指定された場合は通常のparser処理をお
	 * こなう。
	 * 他のフック関数で実行されるhint処理をスキップするために、global 変数をNULL
	 * に設定しておく。
	 */
	if (!pg_hint_plan_enable ||
		(global = parse_head_comment(parse)) == NULL ||
		PlanHintIsempty(global))
	{
		PlanHintDelete(global);
		global = NULL;

		if (prev_planner_hook)
			return (*prev_planner_hook) (parse, cursorOptions, boundParams);
		else
			return standard_planner(parse, cursorOptions, boundParams);
	}

	/* Set hint で指定されたGUCパラメータを設定する */
	save_nestlevel = set_config_options(global->set_hints, global->context);

	if (global->leading != NIL)
		set_join_config_options(0, global->context);

	/*
	 * TODO ビュー定義で指定したテーブル数が1つの場合にもこのタイミングでGUCを変更する必
	 * 要がある。
	 */
	if (list_length(parse->rtable) == 1 &&
		((RangeTblEntry *) linitial(parse->rtable))->rtekind == RTE_RELATION)
	{
		int	i;
		RangeTblEntry  *rte = linitial(parse->rtable);
		
		for (i = 0; i < global->nscan_hints; i++)
		{
			ScanHint	   *hint = global->scan_hints[i];

			if (strcmp(hint->relname, rte->eref->aliasname) != 0)
				parse_ereport(hint->opt_str, ("Relation \"%s\" does not exist.", hint->relname));

			set_scan_config_options(hint->enforce_mask, global->context);
		}
	}

	if (prev_planner_hook)
		result = (*prev_planner_hook) (parse, cursorOptions, boundParams);
	else
		result = standard_planner(parse, cursorOptions, boundParams);

	/*
	 * Restore the GUC variables we set above.
	 */
	AtEOXact_GUC(true, save_nestlevel);

	if (pg_hint_plan_debug_print)
	{
		PlanHintDump(global);
		//elog_node_display(INFO, "rtable", parse->rtable, true);
	}

	PlanHintDelete(global);
	global = NULL;

	return result;
}

static void my_get_relation_info(PlannerInfo *root, Oid relationObjectId,
								 bool inhparent, RelOptInfo *rel)
{
	if (prev_get_relation_info)
		(*prev_get_relation_info) (root, relationObjectId, inhparent, rel);
}

/*
 * pg_add_hint()で登録した個別のヒントを、使用しやすい構造に変換する。
 */
static JoinHint *
set_relids(HashEntry *ent, RelIdInfo **relids, int nrels)
{
	int			i;
	int			j;
	JoinHint   *hint;

	hint = palloc(sizeof(JoinHint));
	hint->joinrelids = NULL;

	for (i = 0; i < ent->tidlist.nrels; i++)
	{
		for (j = 0; j < nrels; j++)
		{
			if (ent->tidlist.oids[i] == relids[j]->oid)
			{
				hint->joinrelids =
					bms_add_member(hint->joinrelids, relids[j]->relid);
				break;
			}
		}

		if (j == nrels)
		{
			pfree(hint);
			return NULL;
		}
	}

	hint->nrels = ent->tidlist.nrels;
	hint->enforce_mask = ent->enforce_mask;

	return hint;
}

/*
 * pg_add_hint()で登録したヒントから、今回のクエリで使用するもののみ抽出し、
 * 使用しやすい構造に変換する。
 */
static void
build_join_hints(PlannerInfo *root, int level, List *initial_rels)
{
	int			i;
	int			nrels;
	RelIdInfo **relids;
	JoinHint   *hint;

	if (1)
		return;

	relids = palloc(sizeof(RelIdInfo *) * root->simple_rel_array_size);

	if (print_log)
	{
		ListCell   *l;
		foreach(l, initial_rels)
		{
			RelOptInfo *rel = (RelOptInfo *) lfirst(l);
			elog_node_display(INFO, "initial_rels", rel, true);
		}
		elog_node_display(INFO, "root", root, true);
		elog(INFO, "%s(simple_rel_array_size:%d, level:%d, query_level:%d, parent_root:%p)",
			__func__, root->simple_rel_array_size, level, root->query_level, root->parent_root);
	}

	for (i = 0, nrels = 0; i < root->simple_rel_array_size; i++)
	{
		if (root->simple_rel_array[i] == NULL)
			continue;

		relids[nrels] = palloc(sizeof(RelIdInfo));

		Assert(i == root->simple_rel_array[i]->relid);

		relids[nrels]->relid = i;
		relids[nrels]->oid = root->simple_rte_array[i]->relid;
		relids[nrels]->eref = root->simple_rte_array[i]->eref;
		//elog(INFO, "%d:%d:%d:%s", i, relids[nrels]->relid, relids[nrels]->oid, relids[nrels]->eref->aliasname);

		nrels++;
	}

	global->join_hint_level = palloc0(sizeof(List *) * (root->simple_rel_array_size));

	for (i = 0; i < HASH_ENTRIES; i++)
	{
		HashEntry *next;

		for (next = hashent[i]; next; next = next->next)
		{
			int	lv;
			if (!(next->enforce_mask & ENABLE_HASHJOIN) &&
				!(next->enforce_mask & ENABLE_NESTLOOP) &&
				!(next->enforce_mask & ENABLE_MERGEJOIN))
				continue;

			if ((hint = set_relids(next, relids, nrels)) == NULL)
				continue;

			lv = bms_num_members(hint->joinrelids);
			global->join_hint_level[lv] = lappend(global->join_hint_level[lv], hint);
		}
	}
}

static void
backup_guc(GucVariables *backup)
{
	backup->enable_seqscan = enable_seqscan;
	backup->enable_indexscan = enable_indexscan;
	backup->enable_bitmapscan = enable_bitmapscan;
	backup->enable_tidscan = enable_tidscan;
	backup->enable_sort = enable_sort;
	backup->enable_hashagg = enable_hashagg;
	backup->enable_nestloop = enable_nestloop;
	backup->enable_material = enable_material;
	backup->enable_mergejoin = enable_mergejoin;
	backup->enable_hashjoin = enable_hashjoin;
}

static void
restore_guc(GucVariables *backup)
{
	enable_seqscan = backup->enable_seqscan;
	enable_indexscan = backup->enable_indexscan;
	enable_bitmapscan = backup->enable_bitmapscan;
	enable_tidscan = backup->enable_tidscan;
	enable_sort = backup->enable_sort;
	enable_hashagg = backup->enable_hashagg;
	enable_nestloop = backup->enable_nestloop;
	enable_material = backup->enable_material;
	enable_mergejoin = backup->enable_mergejoin;
	enable_hashjoin = backup->enable_hashjoin;
}

static void
set_guc(unsigned char enforce_mask)
{
	enable_mergejoin = enforce_mask & ENABLE_MERGEJOIN ? true : false;
	enable_hashjoin = enforce_mask & ENABLE_HASHJOIN ? true : false;
	enable_nestloop = enforce_mask & ENABLE_NESTLOOP ? true : false;
}

/*
 * relidビットマスクと一致するヒントを探す
 */
static JoinHint *
find_join_hint(Relids joinrelids)
{
	List	   *join_hint;
	ListCell   *l;

	join_hint = global->join_hint_level[bms_num_members(joinrelids)];
	foreach(l, join_hint)
	{
		JoinHint   *hint = (JoinHint *) lfirst(l);
		if (bms_equal(joinrelids, hint->joinrelids))
			return hint;
	}

	return NULL;
}


/*
 * src/backend/optimizer/path/joinrels.c
 * export make_join_rel() をラップする関数
 * 
 * ヒントにしたがって、enabele_* パラメータを変更した上で、make_join_rel()を
 * 呼び出す。
 */
static RelOptInfo *
my_make_join_rel(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2)
{
	GucVariables	guc;
	Relids			joinrelids;
	JoinHint	   *hint;
	RelOptInfo	   *rel;

	if (true)
		return make_join_rel(root, rel1, rel2);

	joinrelids = bms_union(rel1->relids, rel2->relids);
	hint = find_join_hint(joinrelids);
	bms_free(joinrelids);

	if (hint)
	{
		backup_guc(&guc);
		set_guc(hint->enforce_mask);
	}

	rel = make_join_rel(root, rel1, rel2);

	if (hint)
		restore_guc(&guc);

	return rel;
}

/*
 * PostgreSQL 本体から流用した関数
 */

/*
 * src/backend/optimizer/path/allpaths.c
 * export standard_join_search() を流用
 * 
 * 変更箇所
 *  build_join_hints() の呼び出しを追加
 */
/*
 * standard_join_search
 *	  Find possible joinpaths for a query by successively finding ways
 *	  to join component relations into join relations.
 *
 * 'levels_needed' is the number of iterations needed, ie, the number of
 *		independent jointree items in the query.  This is > 1.
 *
 * 'initial_rels' is a list of RelOptInfo nodes for each independent
 *		jointree item.	These are the components to be joined together.
 *		Note that levels_needed == list_length(initial_rels).
 *
 * Returns the final level of join relations, i.e., the relation that is
 * the result of joining all the original relations together.
 * At least one implementation path must be provided for this relation and
 * all required sub-relations.
 *
 * To support loadable plugins that modify planner behavior by changing the
 * join searching algorithm, we provide a hook variable that lets a plugin
 * replace or supplement this function.  Any such hook must return the same
 * final join relation as the standard code would, but it might have a
 * different set of implementation paths attached, and only the sub-joinrels
 * needed for these paths need have been instantiated.
 *
 * Note to plugin authors: the functions invoked during standard_join_search()
 * modify root->join_rel_list and root->join_rel_hash.	If you want to do more
 * than one join-order search, you'll probably need to save and restore the
 * original states of those data structures.  See geqo_eval() for an example.
 */
static RelOptInfo *
my_join_search(PlannerInfo *root, int levels_needed, List *initial_rels)
{
	int			lev;
	RelOptInfo *rel;

	/*
	 * This function cannot be invoked recursively within any one planning
	 * problem, so join_rel_level[] can't be in use already.
	 */
	Assert(root->join_rel_level == NULL);

	/*
	 * We employ a simple "dynamic programming" algorithm: we first find all
	 * ways to build joins of two jointree items, then all ways to build joins
	 * of three items (from two-item joins and single items), then four-item
	 * joins, and so on until we have considered all ways to join all the
	 * items into one rel.
	 *
	 * root->join_rel_level[j] is a list of all the j-item rels.  Initially we
	 * set root->join_rel_level[1] to represent all the single-jointree-item
	 * relations.
	 */
	root->join_rel_level = (List **) palloc0((levels_needed + 1) * sizeof(List *));

	root->join_rel_level[1] = initial_rels;

	build_join_hints(root, levels_needed, initial_rels);

	for (lev = 2; lev <= levels_needed; lev++)
	{
		ListCell   *lc;

		/*
		 * Determine all possible pairs of relations to be joined at this
		 * level, and build paths for making each one from every available
		 * pair of lower-level relations.
		 */
		my_join_search_one_level(root, lev);

		/*
		 * Do cleanup work on each just-processed rel.
		 */
		foreach(lc, root->join_rel_level[lev])
		{
			rel = (RelOptInfo *) lfirst(lc);

			/* Find and save the cheapest paths for this rel */
			set_cheapest(rel);

#ifdef OPTIMIZER_DEBUG
			debug_print_rel(root, rel);
#endif
		}
	}

	/*
	 * We should have a single rel at the final level.
	 */
	if (root->join_rel_level[levels_needed] == NIL)
		elog(ERROR, "failed to build any %d-way joins", levels_needed);
	Assert(list_length(root->join_rel_level[levels_needed]) == 1);

	rel = (RelOptInfo *) linitial(root->join_rel_level[levels_needed]);

	root->join_rel_level = NULL;

	return rel;
}

/*
 * src/backend/optimizer/path/joinrels.c
 * static join_search_one_level() を流用
 * 
 * 変更箇所
 *  make_join_rel() の呼び出しをラップする、my_make_join_rel()の呼び出しに変更
 */
/*
 * join_search_one_level
 *	  Consider ways to produce join relations containing exactly 'level'
 *	  jointree items.  (This is one step of the dynamic-programming method
 *	  embodied in standard_join_search.)  Join rel nodes for each feasible
 *	  combination of lower-level rels are created and returned in a list.
 *	  Implementation paths are created for each such joinrel, too.
 *
 * level: level of rels we want to make this time
 * root->join_rel_level[j], 1 <= j < level, is a list of rels containing j items
 *
 * The result is returned in root->join_rel_level[level].
 */
static void
my_join_search_one_level(PlannerInfo *root, int level)
{
	List	  **joinrels = root->join_rel_level;
	ListCell   *r;
	int			k;

	Assert(joinrels[level] == NIL);

	/* Set join_cur_level so that new joinrels are added to proper list */
	root->join_cur_level = level;

	/*
	 * First, consider left-sided and right-sided plans, in which rels of
	 * exactly level-1 member relations are joined against initial relations.
	 * We prefer to join using join clauses, but if we find a rel of level-1
	 * members that has no join clauses, we will generate Cartesian-product
	 * joins against all initial rels not already contained in it.
	 *
	 * In the first pass (level == 2), we try to join each initial rel to each
	 * initial rel that appears later in joinrels[1].  (The mirror-image joins
	 * are handled automatically by make_join_rel.)  In later passes, we try
	 * to join rels of size level-1 from joinrels[level-1] to each initial rel
	 * in joinrels[1].
	 */
	foreach(r, joinrels[level - 1])
	{
		RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
		ListCell   *other_rels;

		if (level == 2)
			other_rels = lnext(r);		/* only consider remaining initial
										 * rels */
		else
			other_rels = list_head(joinrels[1]);		/* consider all initial
														 * rels */

		if (old_rel->joininfo != NIL || old_rel->has_eclass_joins ||
			has_join_restriction(root, old_rel))
		{
			/*
			 * Note that if all available join clauses for this rel require
			 * more than one other rel, we will fail to make any joins against
			 * it here.  In most cases that's OK; it'll be considered by
			 * "bushy plan" join code in a higher-level pass where we have
			 * those other rels collected into a join rel.
			 *
			 * See also the last-ditch case below.
			 */
			make_rels_by_clause_joins(root,
									  old_rel,
									  other_rels);
		}
		else
		{
			/*
			 * Oops, we have a relation that is not joined to any other
			 * relation, either directly or by join-order restrictions.
			 * Cartesian product time.
			 */
			make_rels_by_clauseless_joins(root,
										  old_rel,
										  other_rels);
		}
	}

	/*
	 * Now, consider "bushy plans" in which relations of k initial rels are
	 * joined to relations of level-k initial rels, for 2 <= k <= level-2.
	 *
	 * We only consider bushy-plan joins for pairs of rels where there is a
	 * suitable join clause (or join order restriction), in order to avoid
	 * unreasonable growth of planning time.
	 */
	for (k = 2;; k++)
	{
		int			other_level = level - k;

		/*
		 * Since make_join_rel(x, y) handles both x,y and y,x cases, we only
		 * need to go as far as the halfway point.
		 */
		if (k > other_level)
			break;

		foreach(r, joinrels[k])
		{
			RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
			ListCell   *other_rels;
			ListCell   *r2;

			/*
			 * We can ignore clauseless joins here, *except* when they
			 * participate in join-order restrictions --- then we might have
			 * to force a bushy join plan.
			 */
			if (old_rel->joininfo == NIL && !old_rel->has_eclass_joins &&
				!has_join_restriction(root, old_rel))
				continue;

			if (k == other_level)
				other_rels = lnext(r);	/* only consider remaining rels */
			else
				other_rels = list_head(joinrels[other_level]);

			for_each_cell(r2, other_rels)
			{
				RelOptInfo *new_rel = (RelOptInfo *) lfirst(r2);

				if (!bms_overlap(old_rel->relids, new_rel->relids))
				{
					/*
					 * OK, we can build a rel of the right level from this
					 * pair of rels.  Do so if there is at least one usable
					 * join clause or a relevant join restriction.
					 */
					if (have_relevant_joinclause(root, old_rel, new_rel) ||
						have_join_order_restriction(root, old_rel, new_rel))
					{
						(void) my_make_join_rel(root, old_rel, new_rel);
					}
				}
			}
		}
	}

	/*
	 * Last-ditch effort: if we failed to find any usable joins so far, force
	 * a set of cartesian-product joins to be generated.  This handles the
	 * special case where all the available rels have join clauses but we
	 * cannot use any of those clauses yet.  An example is
	 *
	 * SELECT * FROM a,b,c WHERE (a.f1 + b.f2 + c.f3) = 0;
	 *
	 * The join clause will be usable at level 3, but at level 2 we have no
	 * choice but to make cartesian joins.	We consider only left-sided and
	 * right-sided cartesian joins in this case (no bushy).
	 */
	if (joinrels[level] == NIL)
	{
		/*
		 * This loop is just like the first one, except we always call
		 * make_rels_by_clauseless_joins().
		 */
		foreach(r, joinrels[level - 1])
		{
			RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
			ListCell   *other_rels;

			if (level == 2)
				other_rels = lnext(r);	/* only consider remaining initial
										 * rels */
			else
				other_rels = list_head(joinrels[1]);	/* consider all initial
														 * rels */

			make_rels_by_clauseless_joins(root,
										  old_rel,
										  other_rels);
		}

		/*----------
		 * When special joins are involved, there may be no legal way
		 * to make an N-way join for some values of N.	For example consider
		 *
		 * SELECT ... FROM t1 WHERE
		 *	 x IN (SELECT ... FROM t2,t3 WHERE ...) AND
		 *	 y IN (SELECT ... FROM t4,t5 WHERE ...)
		 *
		 * We will flatten this query to a 5-way join problem, but there are
		 * no 4-way joins that join_is_legal() will consider legal.  We have
		 * to accept failure at level 4 and go on to discover a workable
		 * bushy plan at level 5.
		 *
		 * However, if there are no special joins then join_is_legal() should
		 * never fail, and so the following sanity check is useful.
		 *----------
		 */
		if (joinrels[level] == NIL && root->join_info_list == NIL)
			elog(ERROR, "failed to build any %d-way joins", level);
	}
}

/*
 * src/backend/optimizer/path/joinrels.c
 * static make_rels_by_clause_joins() を流用
 * 
 * 変更箇所
 *  make_join_rel() の呼び出しをラップする、my_make_join_rel()の呼び出しに変更
 */
/*
 * make_rels_by_clause_joins
 *	  Build joins between the given relation 'old_rel' and other relations
 *	  that participate in join clauses that 'old_rel' also participates in
 *	  (or participate in join-order restrictions with it).
 *	  The join rels are returned in root->join_rel_level[join_cur_level].
 *
 * Note: at levels above 2 we will generate the same joined relation in
 * multiple ways --- for example (a join b) join c is the same RelOptInfo as
 * (b join c) join a, though the second case will add a different set of Paths
 * to it.  This is the reason for using the join_rel_level mechanism, which
 * automatically ensures that each new joinrel is only added to the list once.
 *
 * 'old_rel' is the relation entry for the relation to be joined
 * 'other_rels': the first cell in a linked list containing the other
 * rels to be considered for joining
 *
 * Currently, this is only used with initial rels in other_rels, but it
 * will work for joining to joinrels too.
 */
static void
make_rels_by_clause_joins(PlannerInfo *root,
						  RelOptInfo *old_rel,
						  ListCell *other_rels)
{
	ListCell   *l;

	for_each_cell(l, other_rels)
	{
		RelOptInfo *other_rel = (RelOptInfo *) lfirst(l);

		if (!bms_overlap(old_rel->relids, other_rel->relids) &&
			(have_relevant_joinclause(root, old_rel, other_rel) ||
			 have_join_order_restriction(root, old_rel, other_rel)))
		{
			(void) my_make_join_rel(root, old_rel, other_rel);
		}
	}
}

/*
 * src/backend/optimizer/path/joinrels.c
 * static make_rels_by_clauseless_joins() を流用
 * 
 * 変更箇所
 *  make_join_rel() の呼び出しをラップする、my_make_join_rel()の呼び出しに変更
 */
/*
 * make_rels_by_clauseless_joins
 *	  Given a relation 'old_rel' and a list of other relations
 *	  'other_rels', create a join relation between 'old_rel' and each
 *	  member of 'other_rels' that isn't already included in 'old_rel'.
 *	  The join rels are returned in root->join_rel_level[join_cur_level].
 *
 * 'old_rel' is the relation entry for the relation to be joined
 * 'other_rels': the first cell of a linked list containing the
 * other rels to be considered for joining
 *
 * Currently, this is only used with initial rels in other_rels, but it would
 * work for joining to joinrels too.
 */
static void
make_rels_by_clauseless_joins(PlannerInfo *root,
							  RelOptInfo *old_rel,
							  ListCell *other_rels)
{
	ListCell   *l;

	for_each_cell(l, other_rels)
	{
		RelOptInfo *other_rel = (RelOptInfo *) lfirst(l);

		if (!bms_overlap(other_rel->relids, old_rel->relids))
		{
			(void) my_make_join_rel(root, old_rel, other_rel);
		}
	}
}

/*
 * src/backend/optimizer/path/joinrels.c
 * static has_join_restriction() を流用
 * 
 * 変更箇所
 *  なし
 */
/*
 * has_join_restriction
 *		Detect whether the specified relation has join-order restrictions
 *		due to being inside an outer join or an IN (sub-SELECT).
 *
 * Essentially, this tests whether have_join_order_restriction() could
 * succeed with this rel and some other one.  It's OK if we sometimes
 * say "true" incorrectly.	(Therefore, we don't bother with the relatively
 * expensive has_legal_joinclause test.)
 */
static bool
has_join_restriction(PlannerInfo *root, RelOptInfo *rel)
{
	ListCell   *l;

	foreach(l, root->join_info_list)
	{
		SpecialJoinInfo *sjinfo = (SpecialJoinInfo *) lfirst(l);

		/* ignore full joins --- other mechanisms preserve their ordering */
		if (sjinfo->jointype == JOIN_FULL)
			continue;

		/* ignore if SJ is already contained in rel */
		if (bms_is_subset(sjinfo->min_lefthand, rel->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel->relids))
			continue;

		/* restricted if it overlaps LHS or RHS, but doesn't contain SJ */
		if (bms_overlap(sjinfo->min_lefthand, rel->relids) ||
			bms_overlap(sjinfo->min_righthand, rel->relids))
			return true;
	}

	return false;
}

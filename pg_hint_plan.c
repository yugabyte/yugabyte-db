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
#include "commands/prepare.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/geqo.h"
#include "optimizer/joininfo.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/plancat.h"
#include "optimizer/planner.h"
#include "optimizer/prep.h"
#include "optimizer/restrictinfo.h"
#include "tcop/tcopprot.h"
#include "tcop/utility.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#if PG_VERSION_NUM < 90100
#error unsupported PostgreSQL version
#endif

#define BLOCK_COMMENT_START		"/*"
#define BLOCK_COMMENT_END		"*/"
#define HINT_COMMENT_KEYWORD	"+"
#define HINT_START		BLOCK_COMMENT_START HINT_COMMENT_KEYWORD
#define HINT_END		BLOCK_COMMENT_END

/* hint keywords */
#define HINT_SEQSCAN			"SeqScan"
#define HINT_INDEXSCAN			"IndexScan"
#define HINT_BITMAPSCAN			"BitmapScan"
#define HINT_TIDSCAN			"TidScan"
#define HINT_NOSEQSCAN			"NoSeqScan"
#define HINT_NOINDEXSCAN		"NoIndexScan"
#define HINT_NOBITMAPSCAN		"NoBitmapScan"
#define HINT_NOTIDSCAN			"NoTidScan"
#if PG_VERSION_NUM >= 90200
#define HINT_INDEXONLYSCAN		"IndexonlyScan"
#define HINT_NOINDEXONLYSCAN	"NoIndexonlyScan"
#endif
#define HINT_NESTLOOP			"NestLoop"
#define HINT_MERGEJOIN			"MergeJoin"
#define HINT_HASHJOIN			"HashJoin"
#define HINT_NONESTLOOP			"NoNestLoop"
#define HINT_NOMERGEJOIN		"NoMergeJoin"
#define HINT_NOHASHJOIN			"NoHashJoin"
#define HINT_LEADING			"Leading"
#define HINT_SET				"Set"


#define HINT_ARRAY_DEFAULT_INITSIZE 8

#define parse_ereport(str, detail) \
	ereport(pg_hint_plan_parse_messages, \
			(errmsg("hint syntax error at or near \"%s\"", (str)), \
			 errdetail detail))

#define skip_space(str) \
	while (isspace(*str)) \
		str++;

enum
{
	ENABLE_SEQSCAN			= 0x01,
	ENABLE_INDEXSCAN		= 0x02,
	ENABLE_BITMAPSCAN		= 0x04,
	ENABLE_TIDSCAN			= 0x08,
#if PG_VERSION_NUM >= 90200
	ENABLE_INDEXONLYSCAN	= 0x10,
#endif
	ENABLE_NESTLOOP			= 0x20,
	ENABLE_MERGEJOIN		= 0x40,
	ENABLE_HASHJOIN			= 0x80
} TYPE_BITS;

#define ENABLE_ALL_SCAN (ENABLE_SEQSCAN | ENABLE_INDEXSCAN | ENABLE_BITMAPSCAN \
						| ENABLE_TIDSCAN)
#if PG_VERSION_NUM >= 90200
#define ENABLE_ALL_SCAN (ENABLE_SEQSCAN | ENABLE_INDEXSCAN | ENABLE_BITMAPSCAN \
						| ENABLE_TIDSCAN | ENABLE_INDEXONLYSCAN)
#endif
#define ENABLE_ALL_JOIN (ENABLE_NESTLOOP | ENABLE_MERGEJOIN | ENABLE_HASHJOIN)
#define DISABLE_ALL_SCAN 0
#define DISABLE_ALL_JOIN 0

typedef struct Hint Hint;
typedef struct PlanHint PlanHint;

typedef Hint *(*HintCreateFunction) (const char *hint_str, const char *keyword);
typedef void (*HintDeleteFunction) (Hint *hint);
typedef const char *(*HintParseFunction) (Hint *hint, PlanHint *plan, Query *parse, const char *str);

/* hint status */
typedef enum HintStatus
{
	HINT_STATE_NOTUSED = 0,	/* specified relation not used in query */
	HINT_STATE_USED,		/* hint is used */
	HINT_STATE_DUPLICATION,	/* specified hint duplication */
	/* execute error (parse error does not include it) */
	HINT_STATE_ERROR
} HintStatus;

#define hint_state_enabled(hint) ((hint)->base.state == HINT_STATE_NOTUSED || \
								  (hint)->base.state == HINT_STATE_USED)

/* common data for all hints. */
struct Hint
{
	const char		   *hint_str;		/* must not do pfree */
	const char		   *keyword;		/* must not do pfree */
	HintStatus			state;
	HintDeleteFunction	delete_func;
	HintParseFunction	parser_func;
};

/* scan method hints */
typedef struct ScanMethodHint
{
	Hint			base;
	char		   *relname;
	List		   *indexnames;
	unsigned char	enforce_mask;
} ScanMethodHint;

/* join method hints */
typedef struct JoinMethodHint
{
	Hint			base;
	int				nrels;
	char		  **relnames;
	unsigned char	enforce_mask;
	Relids			joinrelids;
} JoinMethodHint;

/* join order hints */
typedef struct LeadingHint
{
	Hint	base;
	List   *relations;		/* relation names specified in Leading hint */
} LeadingHint;

/* change a run-time parameter hints */
typedef struct SetHint
{
	Hint	base;
	char   *name;				/* name of variable */
	char   *value;
} SetHint;

/*
 * Describes a context of hint processing.
 */
struct PlanHint
{
	char		   *hint_str;			/* original hint string */

	/* for scan method hints */
	int				nscan_hints;		/* # of valid scan hints */
	int				max_scan_hints;		/* # of slots for scan hints */
	ScanMethodHint **scan_hints;		/* parsed scan hints */
	int				init_scan_mask;		/* initial value scan parameter */
	Index			parent_relid;		/* inherit parent table relid */

	/* for join method hints */
	int				njoin_hints;		/* # of valid join hints */
	int				max_join_hints;		/* # of slots for join hints */
	JoinMethodHint **join_hints;		/* parsed join hints */
	int				init_join_mask;		/* initial value join parameter */

	List		  **join_hint_level;

	/* for Leading hints */
	int				nleading_hints;		/* # of valid leading hints */
	int				max_leading_hints;	/* # of slots for leading hints */
	LeadingHint	  **leading_hints;		/* parsed Leading hints */

	/* for Set hints */
	GucContext		context;			/* which GUC parameters can we set? */
	int				nset_hints;			/* # of valid set hints */
	int				max_set_hints;		/* # of slots for set hints */
	SetHint		  **set_hints;			/* parsed Set hints */
};

/*
 * Describes a hint parser module which is bound with particular hint keyword.
 */
typedef struct HintParser
{
	char   *keyword;
	bool	have_paren;
	HintCreateFunction	create_func;
} HintParser;

/* Module callbacks */
void		_PG_init(void);
void		_PG_fini(void);

void pg_hint_plan_ProcessUtility(Node *parsetree, const char *queryString,
								 ParamListInfo params, bool isTopLevel,
								 DestReceiver *dest, char *completionTag);
static PlannedStmt *pg_hint_plan_planner(Query *parse, int cursorOptions,
							   ParamListInfo boundParams);
static void pg_hint_plan_get_relation_info(PlannerInfo *root, Oid relationObjectId,
								 bool inhparent, RelOptInfo *rel);
static RelOptInfo *pg_hint_plan_join_search(PlannerInfo *root, int levels_needed,
								  List *initial_rels);

static Hint *ScanMethodHintCreate(const char *hint_str, const char *keyword);
static void ScanMethodHintDelete(ScanMethodHint *hint);
static const char *ScanMethodHintParse(ScanMethodHint *hint, PlanHint *plan, Query *parse, const char *str);
static Hint *JoinMethodHintCreate(const char *hint_str, const char *keyword);
static void JoinMethodHintDelete(JoinMethodHint *hint);
static const char *JoinMethodHintParse(JoinMethodHint *hint, PlanHint *plan, Query *parse, const char *str);
static Hint *LeadingHintCreate(const char *hint_str, const char *keyword);
static void LeadingHintDelete(LeadingHint *hint);
static const char *LeadingHintParse(LeadingHint *hint, PlanHint *plan, Query *parse, const char *str);
static Hint *SetHintCreate(const char *hint_str, const char *keyword);
static void SetHintDelete(SetHint *hint);
static const char *SetHintParse(SetHint *hint, PlanHint *plan, Query *parse, const char *str);

RelOptInfo *pg_hint_plan_standard_join_search(PlannerInfo *root, int levels_needed, List *initial_rels);
void pg_hint_plan_join_search_one_level(PlannerInfo *root, int level);
static void make_rels_by_clause_joins(PlannerInfo *root, RelOptInfo *old_rel, ListCell *other_rels);
static void make_rels_by_clauseless_joins(PlannerInfo *root, RelOptInfo *old_rel, ListCell *other_rels);
static bool has_join_restriction(PlannerInfo *root, RelOptInfo *rel);
static void set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
				 Index rti, RangeTblEntry *rte);
static void set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
					   RangeTblEntry *rte);
static void set_append_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
						Index rti, RangeTblEntry *rte);
static List *accumulate_append_subpath(List *subpaths, Path *path);
static void set_dummy_rel_pathlist(RelOptInfo *rel);
RelOptInfo *pg_hint_plan_make_join_rel(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2);


/* GUC variables */
static bool pg_hint_plan_enable = true;
static bool pg_hint_plan_debug_print = false;
static int pg_hint_plan_parse_messages = INFO;

static const struct config_enum_entry parse_messages_level_options[] = {
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
static ProcessUtility_hook_type prev_ProcessUtility = NULL;
static planner_hook_type prev_planner = NULL;
static get_relation_info_hook_type prev_get_relation_info = NULL;
static join_search_hook_type prev_join_search = NULL;

/* フック関数をまたがって使用する情報を管理する */
static PlanHint *global = NULL;

/*
 * EXECUTEコマンド実行時に、ステートメント名を格納する。
 * その他のコマンドの場合は、NULLに設定る。
 */
static char	   *stmt_name = NULL;

static const HintParser parsers[] = {
	{HINT_SEQSCAN, true, ScanMethodHintCreate},
	{HINT_INDEXSCAN, true, ScanMethodHintCreate},
	{HINT_BITMAPSCAN, true, ScanMethodHintCreate},
	{HINT_TIDSCAN, true, ScanMethodHintCreate},
	{HINT_NOSEQSCAN, true, ScanMethodHintCreate},
	{HINT_NOINDEXSCAN, true, ScanMethodHintCreate},
	{HINT_NOBITMAPSCAN, true, ScanMethodHintCreate},
	{HINT_NOTIDSCAN, true, ScanMethodHintCreate},
#if PG_VERSION_NUM >= 90200
	{HINT_INDEXONLYSCAN, true, ScanMethodHintCreate},
	{HINT_NOINDEXONLYSCAN, true, ScanMethodHintCreate},
#endif
	{HINT_NESTLOOP, true, JoinMethodHintCreate},
	{HINT_MERGEJOIN, true, JoinMethodHintCreate},
	{HINT_HASHJOIN, true, JoinMethodHintCreate},
	{HINT_NONESTLOOP, true, JoinMethodHintCreate},
	{HINT_NOMERGEJOIN, true, JoinMethodHintCreate},
	{HINT_NOHASHJOIN, true, JoinMethodHintCreate},
	{HINT_LEADING, true, LeadingHintCreate},
	{HINT_SET, true, SetHintCreate},
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

	DefineCustomEnumVariable("pg_hint_plan.parse_messages",
							 "Messege level of the parse error.",
							 NULL,
							 &pg_hint_plan_parse_messages,
							 INFO,
							 parse_messages_level_options,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	/* Install hooks. */
	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = pg_hint_plan_ProcessUtility;
	prev_planner = planner_hook;
	planner_hook = pg_hint_plan_planner;
	prev_get_relation_info = get_relation_info_hook;
	get_relation_info_hook = pg_hint_plan_get_relation_info;
	prev_join_search = join_search_hook;
	join_search_hook = pg_hint_plan_join_search;
}

/*
 * Module unload callback
 * XXX never called
 */
void
_PG_fini(void)
{
	/* Uninstall hooks. */
	ProcessUtility_hook = prev_ProcessUtility;
	planner_hook = prev_planner;
	get_relation_info_hook = prev_get_relation_info;
	join_search_hook = prev_join_search;
}

void
pg_hint_plan_ProcessUtility(Node *parsetree, const char *queryString,
							ParamListInfo params, bool isTopLevel,
							DestReceiver *dest, char *completionTag)
{
	Node   *node;

	if (!pg_hint_plan_enable)
	{
		if (prev_ProcessUtility)
			(*prev_ProcessUtility) (parsetree, queryString, params,
									isTopLevel, dest, completionTag);
		else
			standard_ProcessUtility(parsetree, queryString, params,
									isTopLevel, dest, completionTag);

		return;
	}

	node = parsetree;
	if (IsA(node , ExplainStmt))
	{
		/*
		 * EXPLAIN対象のクエリのパースツリーを取得する
		 */
		ExplainStmt	   *stmt;
		Query		   *query;

		stmt = (ExplainStmt *) node;

		Assert(IsA(stmt->query, Query));
		query = (Query *) stmt->query;

		if (query->commandType == CMD_UTILITY && query->utilityStmt != NULL)
			node = query->utilityStmt;
	}

	/*
	 * EXECUTEコマンドならば、PREPARE時に指定されたクエリ文字列を取得し、ヒント
	 * 句の候補として設定する
	 */
	if (IsA(node , ExecuteStmt))
	{
		ExecuteStmt		   *stmt;

		stmt = (ExecuteStmt *) node;
		stmt_name = stmt->name;
	}
	else
		stmt_name = NULL;

	if (prev_ProcessUtility)
		(*prev_ProcessUtility) (parsetree, queryString, params,
								isTopLevel, dest, completionTag);
	else
		standard_ProcessUtility(parsetree, queryString, params,
								isTopLevel, dest, completionTag);

	stmt_name = NULL;
}

static Hint *
ScanMethodHintCreate(const char *hint_str, const char *keyword)
{
	ScanMethodHint *hint;

	hint = palloc(sizeof(ScanMethodHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) ScanMethodHintDelete;
	hint->base.parser_func = (HintParseFunction) ScanMethodHintParse;
	hint->relname = NULL;
	hint->indexnames = NIL;
	hint->enforce_mask = 0;

	return (Hint *) hint;
}

static void
ScanMethodHintDelete(ScanMethodHint *hint)
{
	if (!hint)
		return;

	if (hint->relname)
		pfree(hint->relname);
	list_free_deep(hint->indexnames);
	pfree(hint);
}

static Hint *
JoinMethodHintCreate(const char *hint_str, const char *keyword)
{
	JoinMethodHint *hint;

	hint = palloc(sizeof(JoinMethodHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) JoinMethodHintDelete;
	hint->base.parser_func = (HintParseFunction) JoinMethodHintParse;
	hint->nrels = 0;
	hint->relnames = NULL;
	hint->enforce_mask = 0;
	hint->joinrelids = NULL;

	return (Hint *) hint;
}

static void
JoinMethodHintDelete(JoinMethodHint *hint)
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

static Hint *
LeadingHintCreate(const char *hint_str, const char *keyword)
{
	LeadingHint	   *hint;

	hint = palloc(sizeof(LeadingHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction)LeadingHintDelete;
	hint->base.parser_func = (HintParseFunction) LeadingHintParse;
	hint->relations = NIL;

	return (Hint *) hint;
}

static void
LeadingHintDelete(LeadingHint *hint)
{
	if (!hint)
		return;

	list_free_deep(hint->relations);
	pfree(hint);
}

static Hint *
SetHintCreate(const char *hint_str, const char *keyword)
{
	SetHint	   *hint;

	hint = palloc(sizeof(SetHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) SetHintDelete;
	hint->base.parser_func = (HintParseFunction) SetHintParse;
	hint->name = NULL;
	hint->value = NULL;

	return (Hint *) hint;
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
	hint->init_scan_mask = 0;
	hint->parent_relid = 0;
	hint->njoin_hints = 0;
	hint->max_join_hints = 0;
	hint->join_hints = NULL;
	hint->init_join_mask = 0;
	hint->join_hint_level = NULL;
	hint->nleading_hints = 0;
	hint->max_leading_hints = 0;
	hint->leading_hints = NULL;
	hint->context = superuser() ? PGC_SUSET : PGC_USERSET;
	hint->nset_hints = 0;
	hint->max_set_hints = 0;
	hint->set_hints = NULL;

	return hint;
}

static void
PlanHintDelete(PlanHint *hint)
{
	int			i;

	if (!hint)
		return;

	if (hint->hint_str)
		pfree(hint->hint_str);

	for (i = 0; i < hint->nscan_hints; i++)
		ScanMethodHintDelete(hint->scan_hints[i]);
	if (hint->scan_hints)
		pfree(hint->scan_hints);

	for (i = 0; i < hint->njoin_hints; i++)
		JoinMethodHintDelete(hint->join_hints[i]);
	if (hint->join_hints)
		pfree(hint->join_hints);

	for (i = 0; i < hint->nleading_hints; i++)
		LeadingHintDelete(hint->leading_hints[i]);
	if (hint->leading_hints)
		pfree(hint->leading_hints);

	for (i = 0; i < hint->nset_hints; i++)
		SetHintDelete(hint->set_hints[i]);
	if (hint->set_hints)
		pfree(hint->set_hints);

	pfree(hint);
}

static bool
PlanHintIsEmpty(PlanHint *hint)
{
	if (hint->nscan_hints == 0 &&
		hint->njoin_hints == 0 &&
		hint->nleading_hints == 0 &&
		hint->nset_hints == 0)
		return true;

	return false;
}

static void
dump_quote_value(StringInfo buf, const char *value)
{
	bool		need_quote = false;
	const char *str;

	for (str = value; *str != '\0'; str++)
	{
		if (isspace(*str) || *str == ')' || *str == '"' || *str == '\0')
		{
			need_quote = true;
			appendStringInfoCharMacro(buf, '"');
			break;
		}
	}

	for (str = value; *str != '\0'; str++)
	{
		if (*str == '"')
			appendStringInfoCharMacro(buf, '"');

		appendStringInfoCharMacro(buf, *str);
	}

	if (need_quote)
		appendStringInfoCharMacro(buf, '"');
}

static void
all_hint_dump(PlanHint *hint, StringInfo buf, const char *title, HintStatus state)
{
	int				i;
	ListCell	   *l;

	appendStringInfo(buf, "%s:\n", title);
	for (i = 0; i < hint->nscan_hints; i++)
	{
		ScanMethodHint *h = hint->scan_hints[i];

		if (h->base.state != state)
			continue;

		appendStringInfo(buf, "%s(", h->base.keyword);
		dump_quote_value(buf, h->relname);
		foreach(l, h->indexnames)
		{
			appendStringInfoCharMacro(buf, ' ');
			dump_quote_value(buf, (char *) lfirst(l));
		}
		appendStringInfoString(buf, ")\n");
	}

	for (i = 0; i < hint->njoin_hints; i++)
	{
		JoinMethodHint *h = hint->join_hints[i];
		int				j;

		if (h->base.state != state)
			continue;

		appendStringInfo(buf, "%s(", h->base.keyword);
		dump_quote_value(buf, h->relnames[0]);
		for (j = 1; j < h->nrels; j++)
		{
			appendStringInfoCharMacro(buf, ' ');
			dump_quote_value(buf, h->relnames[j]);
		}
		appendStringInfoString(buf, ")\n");
	}

	for (i = 0; i < hint->nset_hints; i++)
	{
		SetHint	   *h = hint->set_hints[i];

		if (h->base.state != state)
			continue;

		appendStringInfo(buf, "%s(", HINT_SET);
		dump_quote_value(buf, h->name);
		appendStringInfoCharMacro(buf, ' ');
		dump_quote_value(buf, h->value);
		appendStringInfo(buf, ")\n");
	}

	for (i = 0; i < hint->nleading_hints; i++)
	{
		LeadingHint	   *h = hint->leading_hints[i];
		bool			is_first;

		if (h->base.state != state)
			continue;

		appendStringInfo(buf, "%s(", HINT_LEADING);
		is_first = true;
		foreach(l, h->relations)
		{
			if (is_first)
				is_first = false;
			else
				appendStringInfoCharMacro(buf, ' ');

			dump_quote_value(buf, (char *)lfirst(l));
		}

		appendStringInfoString(buf, ")\n");
	}
}

static void
PlanHintDump(PlanHint *hint)
{
	StringInfoData	buf;

	if (!hint)
	{
		elog(LOG, "pg_hint_plan:\nno hint");
		return;
	}

	initStringInfo(&buf);

	appendStringInfoString(&buf, "pg_hint_plan:\n");
	all_hint_dump(hint, &buf, "used hint", HINT_STATE_USED);
	all_hint_dump(hint, &buf, "not used hint", HINT_STATE_NOTUSED);
	all_hint_dump(hint, &buf, "duplication hint", HINT_STATE_DUPLICATION);
	all_hint_dump(hint, &buf, "error hint", HINT_STATE_ERROR);

	elog(LOG, "%s", buf.data);

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
ScanMethodHintCmp(const void *a, const void *b, bool order)
{
	const ScanMethodHint   *hinta = *((const ScanMethodHint **) a);
	const ScanMethodHint   *hintb = *((const ScanMethodHint **) b);
	int						result;

	if ((result = RelnameCmp(&hinta->relname, &hintb->relname)) != 0)
		return result;

	/* ヒント句で指定した順を返す */
	if (order)
		return hinta->base.hint_str - hintb->base.hint_str;
	else
		return 0;
}

static int
ScanMethodHintCmpIsOrder(const void *a, const void *b)
{
	return ScanMethodHintCmp(a, b, true);
}

static int
JoinMethodHintCmp(const void *a, const void *b, bool order)
{
	const JoinMethodHint   *hinta = *((const JoinMethodHint **) a);
	const JoinMethodHint   *hintb = *((const JoinMethodHint **) b);
	int						i;

	if (hinta->nrels != hintb->nrels)
		return hinta->nrels - hintb->nrels;

	for (i = 0; i < hinta->nrels; i++)
	{
		int	result;
		if ((result = RelnameCmp(&hinta->relnames[i], &hintb->relnames[i])) != 0)
			return result;
	}

	/* ヒント句で指定した順を返す */
	if (order)
		return hinta->base.hint_str - hintb->base.hint_str;
	else
		return 0;
}

static int
JoinMethodHintCmpIsOrder(const void *a, const void *b)
{
	return JoinMethodHintCmp(a, b, true);
}

static int
LeadingHintCmp(const void *a, const void *b, bool order)
{
	const LeadingHint  *hinta = *((const LeadingHint **) a);
	const LeadingHint  *hintb = *((const LeadingHint **) b);

	/* ヒント句で指定した順を返す */
	if (order)
		return hinta->base.hint_str - hintb->base.hint_str;
	else
		return 0;
}

#ifdef NOT_USED
static int
LeadingHintCmpIsOrder(const void *a, const void *b)
{
	return LeadingHintCmp(a, b, true);
}
#endif

static int
SetHintCmp(const void *a, const void *b, bool order)
{
	const SetHint  *hinta = *((const SetHint **) a);
	const SetHint  *hintb = *((const SetHint **) b);
	int				result;

	if ((result = strcmp(hinta->name, hintb->name)) != 0)
		return result;

	/* ヒント句で指定した順を返す */
	if (order)
		return hinta->base.hint_str - hintb->base.hint_str;
	else
		return 0;
}

static int
SetHintCmpIsOrder(const void *a, const void *b)
{
	return SetHintCmp(a, b, true);
}

#if PG_VERSION_NUM < 90200
static int
set_config_option_wrapper(const char *name, const char *value,
				 GucContext context, GucSource source,
				 GucAction action, bool changeVal, int elevel)
{
	int				result = 0;
	MemoryContext	ccxt = CurrentMemoryContext;

	PG_TRY();
	{
		result = set_config_option(name, value, context, source,
								   action, changeVal);
	}
	PG_CATCH();
	{
		ErrorData	   *errdata;
		MemoryContext	ecxt;

		if (elevel >= ERROR)
			PG_RE_THROW();

		ecxt = MemoryContextSwitchTo(ccxt);
		errdata = CopyErrorData();
		ereport(elevel, (errcode(errdata->sqlerrcode),
				errmsg("%s", errdata->message),
				errdata->detail ? errdetail("%s", errdata->detail) : 0,
				errdata->hint ? errhint("%s", errdata->hint) : 0));
		FreeErrorData(errdata);

		MemoryContextSwitchTo(ecxt);
	}
	PG_END_TRY();

	return result;
}

#define set_config_option(name, value, context, source, \
						  action, changeVal, elevel) \
	set_config_option_wrapper(name, value, context, source, \
							  action, changeVal, elevel)
#endif

static int
set_config_options(SetHint **options, int noptions, GucContext context)
{
	int	i;
	int	save_nestlevel;

	save_nestlevel = NewGUCNestLevel();

	for (i = 0; i < noptions; i++)
	{
		SetHint	   *hint = options[i];
		int			result;

		if (!hint_state_enabled(hint))
			continue;

		result = set_config_option(hint->name, hint->value, context,
						PGC_S_SESSION, GUC_ACTION_SAVE, true,
						pg_hint_plan_parse_messages);
		if (result != 0)
			hint->base.state = HINT_STATE_USED;
		else
			hint->base.state = HINT_STATE_ERROR;
	}

	return save_nestlevel;
}

#define SET_CONFIG_OPTION(name, type_bits) \
	set_config_option((name), \
		(enforce_mask & (type_bits)) ? "true" : "false", \
		context, PGC_S_SESSION, GUC_ACTION_SAVE, true, ERROR)

static void
set_scan_config_options(unsigned char enforce_mask, GucContext context)
{
	SET_CONFIG_OPTION("enable_seqscan", ENABLE_SEQSCAN);
	SET_CONFIG_OPTION("enable_indexscan", ENABLE_INDEXSCAN);
	SET_CONFIG_OPTION("enable_bitmapscan", ENABLE_BITMAPSCAN);
	SET_CONFIG_OPTION("enable_tidscan", ENABLE_TIDSCAN);
#if PG_VERSION_NUM >= 90200
	SET_CONFIG_OPTION("enable_indexonlyscan", ENABLE_INDEXSCAN);
#endif
}

static void
set_join_config_options(unsigned char enforce_mask, GucContext context)
{
	SET_CONFIG_OPTION("enable_nestloop", ENABLE_NESTLOOP);
	SET_CONFIG_OPTION("enable_mergejoin", ENABLE_MERGEJOIN);
	SET_CONFIG_OPTION("enable_hashjoin", ENABLE_HASHJOIN);
}

/*
 * parse functions
 */

static const char *
parse_keyword(const char *str, StringInfo buf)
{
	skip_space(str);

	while (!isspace(*str) && *str != '(' && *str != '\0')
		appendStringInfoCharMacro(buf, *str++);

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

/*
 * 二重引用符で囲まれているかもしれないトークンを読み取り word 引数に palloc
 * で確保したバッファに格納してそのポインタを返す。
 *
 * 正常にパースできた場合は残りの文字列の先頭位置を、異常があった場合は NULL を
 * 返す。
 */
static const char *
parse_quote_value(const char *str, char **word, char *value_type)
{
	StringInfoData	buf;
	bool			in_quote;

	/* 先頭のスペースは読み飛ばす。 */
	skip_space(str);

	initStringInfo(&buf);
	if (*str == '"')
	{
		str++;
		in_quote = true;
	}
	else
		in_quote = false;

	while (true)
	{
		if (in_quote)
		{
			/* 二重引用符が閉じられていない場合はパース中断 */
			if (*str == '\0')
			{
				pfree(buf.data);
				parse_ereport(str, ("Unterminated quoted %s.", value_type));
				return NULL;
			}

			/*
			 * エスケープ対象のダブルクウォートをスキップする。
			 * もしブロックコメントの開始文字列や終了文字列もオブジェクト名とし
			 * て使用したい場合は、/ と * もエスケープ対象とすることで使用できる
			 * が、処理対象としていない。もしテーブル名にこれらの文字が含まれる
			 * 場合は、エイリアスを指定する必要がある。
			 */
			if(*str == '"')
			{
				str++;
				if (*str != '"')
					break;
			}
		}
		else
			if (isspace(*str) || *str == ')' || *str == '"' || *str == '\0')
				break;

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

static void
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
			Hint   *hint;

			if (strcasecmp(buf.data, keyword) != 0)
				continue;

			hint = parser->create_func(head, keyword);

			if (parser->have_paren)
			{
				/* parser of each hint does parse in a parenthesis. */
				if ((str = skip_opened_parenthesis(str)) == NULL ||
					(str = hint->parser_func(hint, plan, parse, str)) == NULL ||
					(str = skip_closed_parenthesis(str)) == NULL)
				{
					hint->delete_func(hint);
					pfree(buf.data);
					return;
				}
			}
			else
			{
				if ((str = hint->parser_func(hint, plan, parse, str)) == NULL)
				{
					hint->delete_func(hint);
					pfree(buf.data);
					return;
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
			return;
		}
	}

	pfree(buf.data);
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
	if (stmt_name)
	{
		PreparedStatement  *entry;

		entry = FetchPreparedStatement(stmt_name, true);
		p = entry->plansource->query_string;
	}
	else
		p = debug_query_string;

	if (p == NULL)
		return NULL;

	/* extract query head comment. */
	len = strlen(HINT_START);
	skip_space(p);
	if (strncmp(p, HINT_START, len))
		return NULL;

	head = (char *) p;
	p += len;
	skip_space(p);

	/* find hint end keyword. */
	if ((tail = strstr(p, HINT_END)) == NULL)
	{
		parse_ereport(head, ("Unterminated block comment."));
		return NULL;
	}

	/* 入れ子にしたブロックコメントはサポートしない */
	if ((head = strstr(p, BLOCK_COMMENT_START)) != NULL && head < tail)
		parse_ereport(head, ("Block comments nest doesn't supported."));

	/* ヒント句部分を切り出す */
	len = tail - p;
	head = palloc(len + 1);
	memcpy(head, p, len);
	head[len] = '\0';
	p = head;

	plan = PlanHintCreate();
	plan->hint_str = head;

	/* parse each hint. */
	parse_hints(plan, parse, p);

	/* When nothing specified a hint, we free PlanHint and returns NULL. */
	if (PlanHintIsEmpty(plan))
	{
		PlanHintDelete(plan);
		return NULL;
	}

	/* 重複したscan method hintを検索する */
	qsort(plan->scan_hints, plan->nscan_hints, sizeof(ScanMethodHint *), ScanMethodHintCmpIsOrder);
	for (i = 0; i < plan->nscan_hints - 1; i++)
	{
		if (ScanMethodHintCmp(plan->scan_hints + i,
						plan->scan_hints + i + 1, false) == 0)
		{
			parse_ereport(plan->scan_hints[i]->base.hint_str,
				("Conflict scan method hint."));
			plan->scan_hints[i]->base.state = HINT_STATE_DUPLICATION;
		}
	}

	/* 重複したjoin method hintを検索する */
	qsort(plan->join_hints, plan->njoin_hints, sizeof(JoinMethodHint *), JoinMethodHintCmpIsOrder);
	for (i = 0; i < plan->njoin_hints - 1; i++)
	{
		if (JoinMethodHintCmp(plan->join_hints + i,
						plan->join_hints + i + 1, false) == 0)
		{
			parse_ereport(plan->join_hints[i]->base.hint_str,
				("Duplicate join method hint."));
			plan->join_hints[i]->base.state = HINT_STATE_DUPLICATION;
		}
	}

	/* 重複したSet hintを検索する */
	qsort(plan->set_hints, plan->nset_hints, sizeof(SetHint *), SetHintCmpIsOrder);
	for (i = 0; i < plan->nset_hints - 1; i++)
	{
		if (SetHintCmp(plan->set_hints + i,
						plan->set_hints + i + 1, false) == 0)
		{
			parse_ereport(plan->set_hints[i]->base.hint_str,
				("Duplicate set hint."));
			plan->set_hints[i]->base.state = HINT_STATE_DUPLICATION;
		}
	}

	/* 重複したLeading hintを検索する */
	for (i = 0; i < plan->nleading_hints - 1; i++)
	{
		if (LeadingHintCmp(plan->leading_hints + i,
						plan->leading_hints + i + 1, false) == 0)
		{
			parse_ereport(plan->leading_hints[i]->base.hint_str,
				("Duplicate leading hint."));
			plan->leading_hints[i]->base.state = HINT_STATE_DUPLICATION;
		}
	}

	return plan;
}

/*
 * スキャン方式ヒントのカッコ内をパースする
 */
static const char *
ScanMethodHintParse(ScanMethodHint *hint, PlanHint *plan, Query *parse, const char *str)
{
	const char	   *keyword = hint->base.keyword;

	/*
	 * スキャン方式のヒントでリレーション名が読み取れない場合はヒント無効
	 */
	if ((str = parse_quote_value(str, &hint->relname, "ralation name")) == NULL)
		return NULL;

	skip_space(str);

	/*
	 * インデックスリストを受け付けるヒントであれば、インデックス参照をパース
	 * する。
	 */
	if (strcmp(keyword, HINT_INDEXSCAN) == 0 ||
#if PG_VERSION_NUM >= 90200
		strcmp(keyword, HINT_INDEXONLYSCAN) == 0 ||
#endif
		strcmp(keyword, HINT_BITMAPSCAN) == 0)
	{
		while (*str != ')' && *str != '\0')
		{
			char	   *indexname;

			str = parse_quote_value(str, &indexname, "index name");
			if (str == NULL)
				return NULL;

			hint->indexnames = lappend(hint->indexnames, indexname);
			skip_space(str);
		}
	}

	/* カッコが閉じていなければヒント無効。 */
	skip_space(str);		/* just in case */
	if (*str != ')')
	{
		parse_ereport(str, ("Closed parenthesis is necessary."));
		return NULL;
	}

	/*
	 * ヒントごとに決まっている許容スキャン方式をビットマスクとして設定
	 */
	if (strcasecmp(keyword, HINT_SEQSCAN) == 0)
		hint->enforce_mask = ENABLE_SEQSCAN;
	else if (strcasecmp(keyword, HINT_INDEXSCAN) == 0)
		hint->enforce_mask = ENABLE_INDEXSCAN;
	else if (strcasecmp(keyword, HINT_BITMAPSCAN) == 0)
		hint->enforce_mask = ENABLE_BITMAPSCAN;
	else if (strcasecmp(keyword, HINT_TIDSCAN) == 0)
		hint->enforce_mask = ENABLE_TIDSCAN;
	else if (strcasecmp(keyword, HINT_NOSEQSCAN) == 0)
		hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_SEQSCAN;
	else if (strcasecmp(keyword, HINT_NOINDEXSCAN) == 0)
		hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_INDEXSCAN;
	else if (strcasecmp(keyword, HINT_NOBITMAPSCAN) == 0)
		hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_BITMAPSCAN;
	else if (strcasecmp(keyword, HINT_NOTIDSCAN) == 0)
		hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_TIDSCAN;
	else
	{
		parse_ereport(str, ("Unrecognized hint keyword \"%s\".", keyword));
		return NULL;
	}

	/*
	 * 出来上がったヒント情報を追加。スロットが足りない場合は二倍に拡張する。
	 */
	if (plan->nscan_hints == 0)
	{
		plan->max_scan_hints = HINT_ARRAY_DEFAULT_INITSIZE;
		plan->scan_hints = palloc(sizeof(ScanMethodHint *) * plan->max_scan_hints);
	}
	else if (plan->nscan_hints == plan->max_scan_hints)
	{
		plan->max_scan_hints *= 2;
		plan->scan_hints = repalloc(plan->scan_hints,
								sizeof(ScanMethodHint *) * plan->max_scan_hints);
	}

	plan->scan_hints[plan->nscan_hints] = hint;
	plan->nscan_hints++;

	return str;
}

static const char *
JoinMethodHintParse(JoinMethodHint *hint, PlanHint *plan, Query *parse, const char *str)
{
	char		   *relname;
	const char	   *keyword = hint->base.keyword;

	skip_space(str);

	hint->relnames = palloc(sizeof(char *));

	while ((str = parse_quote_value(str, &relname, "table name")) != NULL)
	{
		hint->nrels++;
		hint->relnames = repalloc(hint->relnames, sizeof(char *) * hint->nrels);
		hint->relnames[hint->nrels - 1] = relname;

		skip_space(str);
		if (*str == ')')
			break;
	}

	if (str == NULL)
		return NULL;

	/* Join 対象のテーブルは最低でも2つ指定する必要がある */
	if (hint->nrels < 2)
	{
		parse_ereport(str, ("Specified relation more than two."));
		return NULL;
	}

	/* テーブル名順にソートする */
	qsort(hint->relnames, hint->nrels, sizeof(char *), RelnameCmp);

	if (strcasecmp(keyword, HINT_NESTLOOP) == 0)
		hint->enforce_mask = ENABLE_NESTLOOP;
	else if (strcasecmp(keyword, HINT_MERGEJOIN) == 0)
		hint->enforce_mask = ENABLE_MERGEJOIN;
	else if (strcasecmp(keyword, HINT_HASHJOIN) == 0)
		hint->enforce_mask = ENABLE_HASHJOIN;
	else if (strcasecmp(keyword, HINT_NONESTLOOP) == 0)
		hint->enforce_mask = ENABLE_ALL_JOIN ^ ENABLE_NESTLOOP;
	else if (strcasecmp(keyword, HINT_NOMERGEJOIN) == 0)
		hint->enforce_mask = ENABLE_ALL_JOIN ^ ENABLE_MERGEJOIN;
	else if (strcasecmp(keyword, HINT_NOHASHJOIN) == 0)
		hint->enforce_mask = ENABLE_ALL_JOIN ^ ENABLE_HASHJOIN;
	else
	{
		parse_ereport(str, ("Unrecognized hint keyword \"%s\".", keyword));
		return NULL;
	}

	if (plan->njoin_hints == 0)
	{
		plan->max_join_hints = HINT_ARRAY_DEFAULT_INITSIZE;
		plan->join_hints = palloc(sizeof(JoinMethodHint *) * plan->max_join_hints);
	}
	else if (plan->njoin_hints == plan->max_join_hints)
	{
		plan->max_join_hints *= 2;
		plan->join_hints = repalloc(plan->join_hints,
								sizeof(JoinMethodHint *) * plan->max_join_hints);
	}

	plan->join_hints[plan->njoin_hints] = hint;
	plan->njoin_hints++;

	return str;
}

static const char *
LeadingHintParse(LeadingHint *hint, PlanHint *plan, Query *parse, const char *str)
{
	skip_space(str);

	while (*str != ')')
	{
		char   *relname;

		if ((str = parse_quote_value(str, &relname, "relation name")) == NULL)
			return NULL;

		hint->relations = lappend(hint->relations, relname);

		skip_space(str);
	}

	/* テーブル指定が2つ未満の場合は、Leading ヒントはエラーとする */
	if (list_length(hint->relations) < 2)
	{
		parse_ereport(hint->base.hint_str,
			("In %s hint, specified relation name 2 or more.", HINT_LEADING));
		hint->base.state = HINT_STATE_ERROR;
	}

	if (plan->nleading_hints == 0)
	{
		plan->max_leading_hints = HINT_ARRAY_DEFAULT_INITSIZE;
		plan->leading_hints = palloc(sizeof(LeadingHint *) * plan->max_leading_hints);
	}
	else if (plan->nleading_hints == plan->max_leading_hints)
	{
		plan->max_leading_hints *= 2;
		plan->leading_hints = repalloc(plan->leading_hints,
								sizeof(LeadingHint *) * plan->max_leading_hints);
	}

	plan->leading_hints[plan->nleading_hints] = hint;
	plan->nleading_hints++;

	return str;
}

static const char *
SetHintParse(SetHint *hint, PlanHint *plan, Query *parse, const char *str)
{
	if ((str = parse_quote_value(str, &hint->name, "parameter name")) == NULL ||
		(str = parse_quote_value(str, &hint->value, "parameter value")) == NULL)
		return NULL;

	skip_space(str);
	if (*str != ')')
	{
		parse_ereport(str, ("Closed parenthesis is necessary."));
		return NULL;
	}

	if (plan->nset_hints == 0)
	{
		plan->max_set_hints = HINT_ARRAY_DEFAULT_INITSIZE;
		plan->set_hints = palloc(sizeof(SetHint *) * plan->max_set_hints);
	}
	else if (plan->nset_hints == plan->max_set_hints)
	{
		plan->max_set_hints *= 2;
		plan->set_hints = repalloc(plan->set_hints,
								sizeof(SetHint *) * plan->max_set_hints);
	}

	plan->set_hints[plan->nset_hints] = hint;
	plan->nset_hints++;

	return str;
}

static PlannedStmt *
pg_hint_plan_planner(Query *parse, int cursorOptions, ParamListInfo boundParams)
{
	int				save_nestlevel;
	PlannedStmt	   *result;

	/*
	 * hintが指定されない、または空のhintを指定された場合は通常のparser処理をお
	 * こなう。
	 * 他のフック関数で実行されるhint処理をスキップするために、global 変数をNULL
	 * に設定しておく。
	 */
	if (!pg_hint_plan_enable || (global = parse_head_comment(parse)) == NULL)
	{
		global = NULL;

		if (prev_planner)
			return (*prev_planner) (parse, cursorOptions, boundParams);
		else
			return standard_planner(parse, cursorOptions, boundParams);
	}

	/* Set hint で指定されたGUCパラメータを設定する */
	save_nestlevel = set_config_options(global->set_hints, global->nset_hints,
						global->context);

	if (enable_seqscan)
		global->init_scan_mask |= ENABLE_SEQSCAN;
	if (enable_indexscan)
		global->init_scan_mask |= ENABLE_INDEXSCAN;
	if (enable_bitmapscan)
		global->init_scan_mask |= ENABLE_BITMAPSCAN;
	if (enable_tidscan)
		global->init_scan_mask |= ENABLE_TIDSCAN;
#if PG_VERSION_NUM >= 90200
	if (enable_indexonlyscan)
		global->init_scan_mask |= ENABLE_INDEXONLYSCAN;
#endif
	if (enable_nestloop)
		global->init_join_mask |= ENABLE_NESTLOOP;
	if (enable_mergejoin)
		global->init_join_mask |= ENABLE_MERGEJOIN;
	if (enable_hashjoin)
		global->init_join_mask |= ENABLE_HASHJOIN;

	if (prev_planner)
		result = (*prev_planner) (parse, cursorOptions, boundParams);
	else
		result = standard_planner(parse, cursorOptions, boundParams);

	/*
	 * Restore the GUC variables we set above.
	 */
	AtEOXact_GUC(true, save_nestlevel);

	/*
	 * Print hint if debugging.
	 */
	if (pg_hint_plan_debug_print)
	{
		PlanHintDump(global);
#ifdef NOT_USED
		elog_node_display(INFO, "rtable", parse->rtable, true);
#endif
	}

	PlanHintDelete(global);
	global = NULL;

	return result;
}

/*
 * aliasnameと一致するSCANヒントを探す
 */
static ScanMethodHint *
find_scan_hint(PlannerInfo *root, RelOptInfo *rel)
{
	RangeTblEntry  *rte;
	int	i;

	/*
	 * RELOPT_BASEREL でなければ、scan method ヒントが適用しない。
	 * 子テーブルの場合はRELOPT_OTHER_MEMBER_RELとなるが、サポート対象外とする。
	 * また、通常のリレーション以外は、スキャン方式を選択できない。
	 */
	if (rel->reloptkind != RELOPT_BASEREL || rel->rtekind != RTE_RELATION)
		return NULL;

	rte = root->simple_rte_array[rel->relid];

	/* 外部表はスキャン方式が選択できない。 */
	if (rte->relkind == RELKIND_FOREIGN_TABLE)
		return NULL;

	for (i = 0; i < global->nscan_hints; i++)
	{
		ScanMethodHint *hint = global->scan_hints[i];

		if (!hint_state_enabled(hint))
			continue;

		if (RelnameCmp(&rte->eref->aliasname, &hint->relname) == 0)
			return hint;
	}

	return NULL;
}

static void
pg_hint_plan_get_relation_info(PlannerInfo *root, Oid relationObjectId,
								 bool inhparent, RelOptInfo *rel)
{
	ScanMethodHint *hint;
	ListCell	   *cell;
	ListCell	   *prev;
	ListCell	   *next;

	if (prev_get_relation_info)
		(*prev_get_relation_info) (root, relationObjectId, inhparent, rel);

	/* 有効なヒントが指定されなかった場合は処理をスキップする。 */
	if (!global)
		return;

	if (inhparent)
	{
		/* cache does relids of parent table */
		global->parent_relid = rel->relid;
	}
	else if (global->parent_relid != 0)
	{
		/* If this rel is an appendrel child, */
		ListCell   *l;

		/* append_rel_list contains all append rels; ignore others */
		foreach(l, root->append_rel_list)
		{
			AppendRelInfo *appinfo = (AppendRelInfo *) lfirst(l);

			/* This rel is child table. */
			if (appinfo->parent_relid == global->parent_relid)
				return;
		}

		/* This rel is not inherit table. */
		global->parent_relid = 0;
	}

	/* scan hint が指定されない場合は、GUCパラメータをリセットする。 */
	if ((hint = find_scan_hint(root, rel)) == NULL)
	{
		set_scan_config_options(global->init_scan_mask, global->context);
		return;
	}
	set_scan_config_options(hint->enforce_mask, global->context);
	hint->base.state = HINT_STATE_USED;

	/* インデックスを全て削除し、スキャンに使えなくする */
	if (hint->enforce_mask == ENABLE_SEQSCAN ||
		hint->enforce_mask == ENABLE_TIDSCAN)
	{
		list_free_deep(rel->indexlist);
		rel->indexlist = NIL;
		hint->base.state = HINT_STATE_USED;

		return;
	}

	/* 後でパスを作り直すため、ここではなにもしない */
	if (hint->indexnames == NULL)
		return;

	/* 指定されたインデックスのみをのこす */
	prev = NULL;
	for (cell = list_head(rel->indexlist); cell; cell = next)
	{
		IndexOptInfo   *info = (IndexOptInfo *) lfirst(cell);
		char		   *indexname = get_rel_name(info->indexoid);
		ListCell	   *l;
		bool			use_index = false;

		next = lnext(cell);

		foreach(l, hint->indexnames)
		{
			if (RelnameCmp(&indexname, &lfirst(l)) == 0)
			{
				use_index = true;
				break;
			}
		}

		if (!use_index)
			rel->indexlist = list_delete_cell(rel->indexlist, cell, prev);
		else
			prev = cell;

		pfree(indexname);
	}
}

/*
 * aliasnameがクエリ中に指定した別名と一致する場合は、そのインデックスを返し、一致
 * する別名がなければ0を返す。
 * aliasnameがクエリ中に複数回指定された場合は、-1を返す。
 */
static Index
find_relid_aliasname(PlannerInfo *root, char *aliasname, List *initial_rels,
		const char *str)
{
	int		i;
	Index	found = 0;

	for (i = 1; i < root->simple_rel_array_size; i++)
	{
		ListCell   *l;

		if (root->simple_rel_array[i] == NULL)
			continue;

		Assert(i == root->simple_rel_array[i]->relid);

		if (RelnameCmp(&aliasname, &root->simple_rte_array[i]->eref->aliasname)
				!= 0)
			continue;

		foreach(l, initial_rels)
		{
			RelOptInfo *rel = (RelOptInfo *) lfirst(l);

			if (rel->reloptkind == RELOPT_BASEREL)
			{
				if (rel->relid != i)
					continue;
			}
			else
			{
				Assert(rel->reloptkind == RELOPT_JOINREL);

				if (!bms_is_member(i, rel->relids))
					continue;
			}

			if (found != 0)
			{
				parse_ereport(str, ("Relation name \"%s\" is ambiguous.", aliasname));
				return -1;
			}

			found = i;
			break;
		}

	}

	return found;
}

/*
 * relidビットマスクと一致するヒントを探す
 */
static JoinMethodHint *
find_join_hint(Relids joinrelids)
{
	List	   *join_hint;
	ListCell   *l;

	join_hint = global->join_hint_level[bms_num_members(joinrelids)];
	foreach(l, join_hint)
	{
		JoinMethodHint *hint = (JoinMethodHint *) lfirst(l);
		if (bms_equal(joinrelids, hint->joinrelids))
			return hint;
	}

	return NULL;
}

/*
 * 結合方式のヒントを使用しやすい構造に変換する。
 */
static void
transform_join_hints(PlanHint *plan, PlannerInfo *root, int nbaserel,
		List *initial_rels, JoinMethodHint **join_method_hints)
{
	int				i;
	Index			relid;
	LeadingHint	   *lhint;
	Relids			joinrelids;
	int				njoinrels;
	ListCell	   *l;

	for (i = 0; i < plan->njoin_hints; i++)
	{
		JoinMethodHint *hint = plan->join_hints[i];
		int				j;

		if (!hint_state_enabled(hint) || hint->nrels > nbaserel)
			continue;

		bms_free(hint->joinrelids);
		hint->joinrelids = NULL;
		relid = 0;
		for (j = 0; j < hint->nrels; j++)
		{
			char   *relname = hint->relnames[j];

			relid = find_relid_aliasname(root, relname, initial_rels,
						hint->base.hint_str);

			if (relid == -1)
				hint->base.state = HINT_STATE_ERROR;

			if (relid <= 0)
				break;

			if (bms_is_member(relid, hint->joinrelids))
			{
				parse_ereport(hint->base.hint_str,
					("Relation name \"%s\" is duplicate.", relname));
				hint->base.state = HINT_STATE_ERROR;
				break;
			}

			hint->joinrelids = bms_add_member(hint->joinrelids, relid);
		}

		if (relid <= 0 || hint->base.state == HINT_STATE_ERROR)
			continue;

		plan->join_hint_level[hint->nrels] =
			lappend(plan->join_hint_level[hint->nrels], hint);
	}

	/*
	 * 有効なLeading ヒントが指定されている場合は、結合順にあわせてjoin method hint
	 * のフォーマットに変換する。
	 */
	if (plan->nleading_hints == 0)
		return;

	lhint = plan->leading_hints[plan->nleading_hints - 1];
	if (!hint_state_enabled(lhint))
		return;

	/* Leading hint は、全ての join 方式が有効な hint として登録する */
	joinrelids = NULL;
	njoinrels = 0;
	foreach(l, lhint->relations)
	{
		char		   *relname = (char *)lfirst(l);
		JoinMethodHint *hint;

		i = find_relid_aliasname(root, relname, initial_rels, plan->hint_str);

		if (i == -1)
		{
			bms_free(joinrelids);
			return;
		}

		if (i == 0)
			continue;

		if (bms_is_member(i, joinrelids))
		{
			parse_ereport(lhint->base.hint_str,
				("Relation name \"%s\" is duplicate.", relname));
			lhint->base.state = HINT_STATE_ERROR;
			bms_free(joinrelids);
			return;
		}

		joinrelids = bms_add_member(joinrelids, i);
		njoinrels++;

		if (njoinrels < 2)
			continue;

		hint = find_join_hint(joinrelids);
		if (hint == NULL)
		{
			/*
			 * Here relnames is not set, since Relids bitmap is sufficient to
			 * control paths of this query afterwards.
			 */
			hint = (JoinMethodHint *) JoinMethodHintCreate(lhint->base.hint_str,
						HINT_LEADING);
			hint->base.state = HINT_STATE_USED;
			hint->nrels = njoinrels;
			hint->enforce_mask = ENABLE_ALL_JOIN;
			hint->joinrelids = bms_copy(joinrelids);
		}

		join_method_hints[njoinrels] = hint;

		if (njoinrels >= nbaserel)
			break;
	}

	bms_free(joinrelids);

	if (njoinrels < 2)
		return;

	for (i = 2; i <= njoinrels; i++)
	{
		/* Leading で指定した組み合わせ以外の join hint を削除する */
		list_free(plan->join_hint_level[i]);

		plan->join_hint_level[i] =
			lappend(NIL, join_method_hints[i]);
	}

	if (hint_state_enabled(lhint))
		set_join_config_options(DISABLE_ALL_JOIN, global->context);

	lhint->base.state = HINT_STATE_USED;

}

static void
rebuild_scan_path(PlanHint *plan, PlannerInfo *root, int level, List *initial_rels)
{
	ListCell   *l;

	foreach(l, initial_rels)
	{
		RelOptInfo	   *rel = (RelOptInfo *) lfirst(l);
		RangeTblEntry  *rte;
		ScanMethodHint *hint;

		/*
		 * スキャン方式が選択できるリレーションのみ、スキャンパスを再生成
		 * する。
		 */
		if (rel->reloptkind != RELOPT_BASEREL || rel->rtekind != RTE_RELATION)
			continue;

		rte = root->simple_rte_array[rel->relid];

		/* 外部表はスキャン方式が選択できない。 */
		if (rte->relkind == RELKIND_FOREIGN_TABLE)
			continue;

		/*
		 * scan method hint が指定されていなければ、初期値のGUCパラメータでscan
		 * path を再生成する。
		 */
		if ((hint = find_scan_hint(root, rel)) == NULL)
			set_scan_config_options(plan->init_scan_mask, plan->context);
		else
		{
			set_scan_config_options(hint->enforce_mask, plan->context);
			hint->base.state = HINT_STATE_USED;
		}

		list_free_deep(rel->pathlist);
		rel->pathlist = NIL;
		if (rte->inh)
		{
			/* It's an "append relation", process accordingly */
			set_append_rel_pathlist(root, rel, rel->relid, rte);
		}
		else
		{
			set_plain_rel_pathlist(root, rel, rte);
		}
	}

	/*
	 * Restore the GUC variables we set above.
	 */
	set_scan_config_options(plan->init_scan_mask, plan->context);
}

/*
 * make_join_rel() をラップする関数
 * 
 * ヒントにしたがって、enabele_* パラメータを変更した上で、make_join_rel()を
 * 呼び出す。
 */
static RelOptInfo *
make_join_rel_wrapper(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2)
{
	Relids			joinrelids;
	JoinMethodHint *hint;
	RelOptInfo	   *rel;
	int				save_nestlevel;

	joinrelids = bms_union(rel1->relids, rel2->relids);
	hint = find_join_hint(joinrelids);
	bms_free(joinrelids);

	if (!hint)
		return pg_hint_plan_make_join_rel(root, rel1, rel2);

	save_nestlevel = NewGUCNestLevel();

	set_join_config_options(hint->enforce_mask, global->context);

	rel = pg_hint_plan_make_join_rel(root, rel1, rel2);
	hint->base.state = HINT_STATE_USED;

	/*
	 * Restore the GUC variables we set above.
	 */
	AtEOXact_GUC(true, save_nestlevel);

	return rel;
}

static int
get_num_baserels(List *initial_rels)
{
	int			nbaserel = 0;
	ListCell   *l;

	foreach(l, initial_rels)
	{
		RelOptInfo *rel = (RelOptInfo *) lfirst(l);

		if (rel->reloptkind == RELOPT_BASEREL)
			nbaserel++;
		else if (rel->reloptkind ==RELOPT_JOINREL)
			nbaserel+= bms_num_members(rel->relids);
		else
		{
			/* other values not expected here */
			elog(ERROR, "unrecognized reloptkind type: %d", rel->reloptkind);
		}
	}

	return nbaserel;
}

static RelOptInfo *
pg_hint_plan_join_search(PlannerInfo *root, int levels_needed, List *initial_rels)
{
	JoinMethodHint **join_method_hints;
	int				nbaserel;
	RelOptInfo	   *rel;
	int				i;

	/*
	 * pg_hint_planが無効、または有効なヒントが1つも指定されなかった場合は、標準
	 * の処理を行う。
	 */
	if (!global)
	{
		if (prev_join_search)
			return (*prev_join_search) (root, levels_needed, initial_rels);
		else if (enable_geqo && levels_needed >= geqo_threshold)
			return geqo(root, levels_needed, initial_rels);
		else
			return standard_join_search(root, levels_needed, initial_rels);
	}

	/* We apply scan method hint rebuild scan path. */
	rebuild_scan_path(global, root, levels_needed, initial_rels);

	/*
	 * GEQOを使用する条件を満たした場合は、GEQOを用いた結合方式の検索を行う。
	 * このとき、スキャン方式のヒントとSetヒントのみが有効になり、結合方式や結合
	 * 順序はヒント句は無効になりGEQOのアルゴリズムで決定される。
	 */
	if (enable_geqo && levels_needed >= geqo_threshold)
		return geqo(root, levels_needed, initial_rels);

	nbaserel = get_num_baserels(initial_rels);
	global->join_hint_level = palloc0(sizeof(List *) * (nbaserel + 1));
	join_method_hints = palloc0(sizeof(JoinMethodHint *) * (nbaserel + 1));

	transform_join_hints(global, root, nbaserel, initial_rels, join_method_hints);

	rel = pg_hint_plan_standard_join_search(root, levels_needed, initial_rels);

	for (i = 2; i <= nbaserel; i++)
	{
		list_free(global->join_hint_level[i]);

		/* free Leading hint only */
		if (join_method_hints[i] != NULL &&
			join_method_hints[i]->enforce_mask == ENABLE_ALL_JOIN)
			JoinMethodHintDelete(join_method_hints[i]);
	}
	pfree(global->join_hint_level);
	pfree(join_method_hints);

	if (global->nleading_hints > 0 &&
		hint_state_enabled(global->leading_hints[global->nleading_hints - 1]))
		set_join_config_options(global->init_join_mask, global->context);

	return rel;
}

/*
 * set_rel_pathlist
 *	  Build access paths for a base relation
 */
static void
set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
				 Index rti, RangeTblEntry *rte)
{
	if (rte->inh)
	{
		/* It's an "append relation", process accordingly */
		set_append_rel_pathlist(root, rel, rti, rte);
	}
	else
	{
		if (rel->rtekind == RTE_RELATION)
		{
			if (rte->relkind == RELKIND_RELATION)
			{
				/* Plain relation */
				set_plain_rel_pathlist(root, rel, rte);
			}
			else
				elog(ERROR, "unexpected relkind: %c", rte->relkind);
		}
		else
			elog(ERROR, "unexpected rtekind: %d", (int) rel->rtekind);
	}
}

#define standard_join_search pg_hint_plan_standard_join_search
#define join_search_one_level pg_hint_plan_join_search_one_level
#define make_join_rel make_join_rel_wrapper
#include "core.c"

#undef make_join_rel
#define make_join_rel pg_hint_plan_make_join_rel
#define add_paths_to_joinrel(root, joinrel, outerrel, innerrel, jointype, sjinfo, restrictlist) \
do { \
	ScanMethodHint *hint = NULL; \
	if ((hint = find_scan_hint((root), (innerrel))) != NULL) \
	{ \
		set_scan_config_options(hint->enforce_mask, global->context); \
		hint->base.state = HINT_STATE_USED; \
	} \
	add_paths_to_joinrel((root), (joinrel), (outerrel), (innerrel), (jointype), (sjinfo), (restrictlist)); \
	if (hint != NULL) \
		set_scan_config_options(global->init_scan_mask, global->context); \
} while(0)
#include "make_join_rel.c"

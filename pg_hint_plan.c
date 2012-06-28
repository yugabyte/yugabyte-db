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
#include "miscadmin.h"
#include "optimizer/geqo.h"
#include "optimizer/joininfo.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/plancat.h"
#include "optimizer/planner.h"
#include "tcop/tcopprot.h"
#include "utils/lsyscache.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#if PG_VERSION_NUM < 90100
#error unsupported PostgreSQL version
#endif

#define HINT_START	"/*"
#define HINT_END	"*/"

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

typedef struct Hint Hint;
typedef struct PlanHint PlanHint;

typedef Hint *(*HintCreateFunction) (char *hint_str, char *keyword);
typedef void (*HintDeleteFunction) (Hint *hint);
typedef const char *(*HintParseFunction) (Hint *hint, PlanHint *plan, Query *parse, const char *str);

/* hint status */
typedef enum HintStatus
{
	HINT_STATE_NOTUSED = 0,	/* specified relation not used in query */
	HINT_STATE_DUPLICATION,	/* specified hint duplication */
	HINT_STATE_USED,			/* hint is used */
	/* execute error (parse error does not include it) */
	HINT_STATE_ERROR
} HintStatus;

/* common data for all hints. */
struct Hint
{
	const char		   *hint_str;		/* must not do pfree */
	const char		   *keyword;		/* must not do pfree */
	bool				used;
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
	char		   *hint_str;		/* original hint string */

	/* for scan method hints */
	int				nscan_hints;	/* # of valid scan hints */
	int				max_scan_hints;	/* # of slots for scan hints */
	ScanMethodHint **scan_hints;	/* parsed scan hints */

	/* for join method hints */
	int				njoin_hints;	/* # of valid join hints */
	int				max_join_hints;	/* # of slots for join hints */
	JoinMethodHint **join_hints;	/* parsed join hints */

	int				nlevel;			/* # of relations to be joined */
	List		  **join_hint_level;

	/* for Leading hints */
	List		   *leading;		/* relation names specified in Leading hint */

	/* for Set hints */
	GucContext		context;		/* which GUC parameters can we set? */
	List		   *set_hints;		/* parsed Set hints */
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

static PlannedStmt *pg_hint_plan_planner(Query *parse, int cursorOptions,
							   ParamListInfo boundParams);
static void pg_hint_plan_get_relation_info(PlannerInfo *root, Oid relationObjectId,
								 bool inhparent, RelOptInfo *rel);
static RelOptInfo *pg_hint_plan_join_search(PlannerInfo *root, int levels_needed,
								  List *initial_rels);

static Hint *ScanMethodHintCreate(char *hint_str, char *keyword);
static void ScanMethodHintDelete(ScanMethodHint *hint);
static const char *ScanMethodHintParse(Hint *hint, PlanHint *plan, Query *parse, const char *str);
static Hint *JoinMethodHintCreate(char *hint_str, char *keyword);
static void JoinMethodHintDelete(JoinMethodHint *hint);
static const char *JoinMethodHintParse(Hint *hint, PlanHint *plan, Query *parse, const char *str);
static Hint *LeadingHintCreate(char *hint_str, char *keyword);
static void LeadingHintDelete(LeadingHint *hint);
static const char *LeadingHintParse(Hint *hint, PlanHint *plan, Query *parse, const char *str);
static Hint *SetHintCreate(char *hint_str, char *keyword);
static void SetHintDelete(SetHint *hint);
static const char *SetHintParse(Hint *hint, PlanHint *plan, Query *parse, const char *str);
#ifdef NOT_USED
static const char *OrderedHintParse(Hint *hint, PlanHint *plan, Query *parse, const char *str);
#endif

RelOptInfo *standard_join_search_org(PlannerInfo *root, int levels_needed, List *initial_rels);
void pg_hint_plan_join_search_one_level(PlannerInfo *root, int level);
static void make_rels_by_clause_joins(PlannerInfo *root, RelOptInfo *old_rel, ListCell *other_rels);
static void make_rels_by_clauseless_joins(PlannerInfo *root, RelOptInfo *old_rel, ListCell *other_rels);
static bool has_join_restriction(PlannerInfo *root, RelOptInfo *rel);
static void set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte);


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
static planner_hook_type prev_planner_hook = NULL;
static get_relation_info_hook_type prev_get_relation_info = NULL;
static join_search_hook_type prev_join_search = NULL;

/* フック関数をまたがって使用する情報を管理する */
static PlanHint *global = NULL;

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
	prev_planner_hook = planner_hook;
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
	planner_hook = prev_planner_hook;
	get_relation_info_hook = prev_get_relation_info;
	join_search_hook = prev_join_search;
}

static Hint *
ScanMethodHintCreate(char *hint_str, char *keyword)
{
	ScanMethodHint *hint;

	hint = palloc(sizeof(ScanMethodHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.used = false;
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
JoinMethodHintCreate(char *hint_str, char *keyword)
{
	JoinMethodHint *hint;

	hint = palloc(sizeof(JoinMethodHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.used = false;
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
LeadingHintCreate(char *hint_str, char *keyword)
{
	LeadingHint	   *hint;

	hint = palloc(sizeof(LeadingHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.used = false;
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
SetHintCreate(char *hint_str, char *keyword)
{
	SetHint	   *hint;

	hint = palloc(sizeof(SetHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.used = false;
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
	hint->njoin_hints = 0;
	hint->max_join_hints = 0;
	hint->join_hints = NULL;
	hint->nlevel = 0;
	hint->join_hint_level = NULL;
	hint->leading = NIL;
	hint->context = superuser() ? PGC_SUSET : PGC_USERSET;
	hint->set_hints = NIL;

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
		ScanMethodHintDelete(hint->scan_hints[i]);
	if (hint->scan_hints)
		pfree(hint->scan_hints);

	for (i = 0; i < hint->njoin_hints; i++)
		JoinMethodHintDelete(hint->join_hints[i]);
	if (hint->join_hints)
		pfree(hint->join_hints);

	for (i = 2; i <= hint->nlevel; i++)
		list_free(hint->join_hint_level[i]);
	if (hint->join_hint_level)
		pfree(hint->join_hint_level);

	list_free_deep(hint->leading);

	foreach(l, hint->set_hints)
		SetHintDelete((SetHint *) lfirst(l));
	list_free(hint->set_hints);

	pfree(hint);
}

static bool
PlanHintIsempty(PlanHint *hint)
{
	if (hint->nscan_hints == 0 &&
		hint->njoin_hints == 0 &&
		hint->leading == NIL &&
		hint->set_hints == NIL)
		return true;

	return false;
}

/* TODO オブジェクト名のクォート処理を追加 */
static void
PlanHintDump(PlanHint *hint)
{
	StringInfoData	buf;
	ListCell	   *l;
	int				i;
	bool			is_first = true;

	if (!hint)
	{
		elog(LOG, "no hint");
		return;
	}

	initStringInfo(&buf);
	appendStringInfo(&buf, "/*\n");
	for (i = 0; i < hint->nscan_hints; i++)
	{
		ScanMethodHint *h = hint->scan_hints[i];
		ListCell	   *n;
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
		JoinMethodHint *h = hint->join_hints[i];
		int				i;
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

	if (hinta->nrels == hintb->nrels)
	{
		int	i;
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

	return hinta->nrels - hintb->nrels;
}

static int
JoinMethodHintCmpIsOrder(const void *a, const void *b)
{
	return JoinMethodHintCmp(a, b, true);
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
						pg_hint_plan_parse_messages);
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
			if (isspace(*str) || *str == ')' || *str == '\0')
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
					return false;
				}
			}
			else
			{
				if ((str = hint->parser_func(hint, plan, parse, str)) == NULL)
				{
					hint->delete_func(hint);
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
	{
		parse_ereport(debug_query_string, ("unterminated /* comment"));
		return NULL;
	}

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
	qsort(plan->scan_hints, plan->nscan_hints, sizeof(ScanMethodHint *), ScanMethodHintCmpIsOrder);
	for (i = 0; i < plan->nscan_hints - 1;)
	{
		int	result = ScanMethodHintCmp(plan->scan_hints + i,
						plan->scan_hints + i + 1, false);
		if (result != 0)
			i++;
		else
		{
			/* 後で指定したヒントを有効にする */
			plan->nscan_hints--;
			memmove(plan->scan_hints + i, plan->scan_hints + i + 1,
					sizeof(ScanMethodHint *) * (plan->nscan_hints - i));
		}
	}

	/* 重複したJoin条件をを除外する */
	qsort(plan->join_hints, plan->njoin_hints, sizeof(JoinMethodHint *), JoinMethodHintCmpIsOrder);
	for (i = 0; i < plan->njoin_hints - 1;)
	{
		int	result = JoinMethodHintCmp(plan->join_hints + i,
						plan->join_hints + i + 1, false);
		if (result != 0)
			i++;
		else
		{
			/* 後で指定したヒントを有効にする */
			plan->njoin_hints--;
			memmove(plan->join_hints + i, plan->join_hints + i + 1,
					sizeof(JoinMethodHint *) * (plan->njoin_hints - i));
		}
	}

	return plan;
}

/*
 * スキャン方式ヒントのカッコ内をパースする
 */
static const char *
ScanMethodHintParse(Hint *base_hint, PlanHint *plan, Query *parse, const char *str)
{
	ScanMethodHint *hint = (ScanMethodHint *) base_hint;
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
		parse_ereport(str, ("unrecognized hint keyword \"%s\"", keyword));
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
JoinMethodHintParse(Hint *base_hint, PlanHint *plan, Query *parse, const char *str)
{
	char		   *relname;
	JoinMethodHint *hint = (JoinMethodHint *) base_hint;
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
		parse_ereport(str, ("unrecognized hint keyword \"%s\"", keyword));
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
LeadingHintParse(Hint *hint, PlanHint *plan, Query *parse, const char *str)
{
	char   *relname;

	/*
	 * すでに指定済みの場合は、後で指定したヒントが有効にするため、登録済みの
	 * 情報を削除する
	 */
	list_free_deep(plan->leading);
	plan->leading = NIL;

	while ((str = parse_quote_value(str, &relname, "relation name")) != NULL)
	{
		const char *p;

		plan->leading = lappend(plan->leading, relname);

		p = str;
		skip_space(str);
		if (*str == ')')
			break;

		if (p == str)
		{
			parse_ereport(str, ("Must be specified space."));
			break;
		}
	}

	/* テーブル指定が1つのみの場合は、ヒントを無効にし、パースを続ける */
	if (list_length(plan->leading) == 1)
	{
		parse_ereport(str, ("In %s hint, specified relation name 2 or more.", HINT_LEADING));
		list_free_deep(plan->leading);
		plan->leading = NIL;
	}

	return str;
}

static const char *
SetHintParse(Hint *base_hint, PlanHint *plan, Query *parse, const char *str)
{
	SetHint	   *hint = (SetHint *) base_hint;

	if ((str = parse_quote_value(str, &hint->name, "parameter name")) == NULL ||
		(str = skip_option_delimiter(str)) == NULL ||
		(str = parse_quote_value(str, &hint->value, "parameter value")) == NULL)
		return NULL;

	skip_space(str);
	if (*str != ')')
	{
		parse_ereport(str, ("Closed parenthesis is necessary."));
		return NULL;
	}
	plan->set_hints = lappend(plan->set_hints, hint);

	return str;
}

#ifdef NOT_USED
/*
 * Oracle の ORDERD ヒントの実装
 */
static const char *
OrderedHintParse(Hint *base_hint, PlanHint *plan, Query *parse, const char *str);
{
	SetHint	   *hint = (SetHint *) base_hint;

	hint->name = pstrdup("join_collapse_limit");
	hint->value = pstrdup("1");
	plan->set_hints = lappend(plan->set_hints, hint);

	return str;
}
#endif

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
			ScanMethodHint *hint = global->scan_hints[i];

			if (RelnameCmp(&rte->eref->aliasname, &hint->relname) != 0)
				parse_ereport(hint->base.hint_str, ("Relation \"%s\" does not exist.", hint->relname));

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
find_scan_hint(RangeTblEntry *rte)
{
	int	i;

	for (i = 0; i < global->nscan_hints; i++)
	{
		ScanMethodHint *hint = global->scan_hints[i];

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

	if (rel->reloptkind != RELOPT_BASEREL)
		return;

	if ((hint = find_scan_hint(root->simple_rte_array[rel->relid])) == NULL)
		return;

	/* インデックスを全て削除し、スキャンに使えなくする */
	if (hint->enforce_mask == ENABLE_SEQSCAN)
	{
		list_free_deep(rel->indexlist);
		rel->indexlist = NIL;

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

static Index
scan_relid_aliasname(PlannerInfo *root, char *aliasname, bool check_ambiguous, const char *str)
{
	/* TODO refnameRangeTblEntry を参考 */
	int		i;
	Index	find = 0;

	for (i = 1; i < root->simple_rel_array_size; i++)
	{
		if (root->simple_rel_array[i] == NULL)
			continue;

		Assert(i == root->simple_rel_array[i]->relid);

		if (RelnameCmp(&aliasname, &root->simple_rte_array[i]->eref->aliasname)
				!= 0)
			continue;

		if (!check_ambiguous)
			return i;

		if (find)
			parse_ereport(str, ("relation name \"%s\" is ambiguous", aliasname));

		find = i;
	}

	return find;
}

/*
 * relidビットマスクと一致するヒントを探す
 */
static JoinMethodHint *
scan_join_hint(Relids joinrelids)
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
transform_join_hints(PlanHint *plan, PlannerInfo *root, int level, List *initial_rels)
{
	int			i;
	ListCell   *l;
	Relids		joinrelids;
	int			njoinrels;

	plan->nlevel = root->simple_rel_array_size - 1;
	plan->join_hint_level = palloc0(sizeof(List *) * (root->simple_rel_array_size));
	for (i = 0; i < plan->njoin_hints; i++)
	{
		JoinMethodHint *hint = plan->join_hints[i];
		int				j;
		Index			relid = 0;

		for (j = 0; j < hint->nrels; j++)
		{
			char   *relname = hint->relnames[j];

			relid = scan_relid_aliasname(root, relname, true, hint->base.hint_str);
			if (relid == 0)
			{
				parse_ereport(hint->base.hint_str, ("Relation \"%s\" does not exist.", relname));
				break;
			}

			hint->joinrelids = bms_add_member(hint->joinrelids, relid);
		}

		if (relid == 0)
			continue;

		plan->join_hint_level[hint->nrels] =
			lappend(plan->join_hint_level[hint->nrels], hint);
	}

	/* Leading hint は、全ての join 方式が有効な hint として登録する */
	joinrelids = NULL;
	njoinrels = 0;
	foreach(l, plan->leading)
	{
		char		   *relname = (char *)lfirst(l);
		JoinMethodHint *hint;

		i = scan_relid_aliasname(root, relname, true, plan->hint_str);
		if (i == 0)
		{
			parse_ereport(plan->hint_str, ("Relation \"%s\" does not exist.", relname));
			list_free_deep(plan->leading);
			plan->leading = NIL;
			break;
		}

		joinrelids = bms_add_member(joinrelids, i);
		njoinrels++;

		if (njoinrels < 2)
			continue;

		if (njoinrels > plan->nlevel)
		{
			parse_ereport(plan->hint_str, ("In %s hint, specified relation name %d or less.", HINT_LEADING, plan->nlevel));
			break;
		}

		/* Leading で指定した組み合わせ以外の join hint を削除する */
		hint = scan_join_hint(joinrelids);
		list_free(plan->join_hint_level[njoinrels]);
		if (hint)
			plan->join_hint_level[njoinrels] = lappend(NIL, hint);
		else
		{
			/*
			 * Here relnames is not set, since Relids bitmap is sufficient to
			 * control paths of this query afterwards.
			 */
			// TODO plan->hint_strをLeadingHint構造に変更後修正
			hint = (JoinMethodHint *) JoinMethodHintCreate(plan->hint_str, HINT_LEADING);
			hint->nrels = njoinrels;
			hint->enforce_mask = ENABLE_ALL_JOIN;
			hint->joinrelids = bms_copy(joinrelids);
			plan->join_hint_level[njoinrels] = lappend(NIL, hint);

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
		}
	}

	bms_free(joinrelids);
}

static void
rebuild_scan_path(PlanHint *plan, PlannerInfo *root, int level, List *initial_rels)
{
	int	i;
	int	save_nestlevel = 0;

	for (i = 0; i < plan->nscan_hints; i++)
	{
		ScanMethodHint *hint = plan->scan_hints[i];
		ListCell	   *l;

		if (hint->enforce_mask == ENABLE_SEQSCAN)
			continue;

		foreach(l, initial_rels)
		{
			RelOptInfo	   *rel = (RelOptInfo *) lfirst(l);
			RangeTblEntry  *rte = root->simple_rte_array[rel->relid];

			/*
			 * スキャン方式が選択できるリレーションのみ、スキャンパスを再生成
			 * する。
			 */
			if (rel->reloptkind != RELOPT_BASEREL ||
				rte->rtekind == RTE_VALUES ||
				RelnameCmp(&hint->relname, &rte->eref->aliasname) != 0)
				continue;

			/*
			 * 複数のスキャンヒントが指定されていた場合でも、1つのネストレベルで
			 * スキャン関連のGUCパラメータを変更する。
			 */
			if (save_nestlevel == 0)
				save_nestlevel = NewGUCNestLevel();

			/*
			 * TODO ヒントで指定されたScan方式が最安価でない場合のみ、Pathを生成
			 * しなおす
			 */
			set_scan_config_options(hint->enforce_mask, plan->context);

			rel->pathlist = NIL;	/* TODO 解放 */
			set_plain_rel_pathlist(root, rel, rte);

			break;
		}
	}

	/*
	 * Restore the GUC variables we set above.
	 */
	if (save_nestlevel != 0)
		AtEOXact_GUC(true, save_nestlevel);
}

/*
 * src/backend/optimizer/path/joinrels.c
 * export make_join_rel() をラップする関数
 * 
 * ヒントにしたがって、enabele_* パラメータを変更した上で、make_join_rel()を
 * 呼び出す。
 */
static RelOptInfo *
pg_hint_plan_make_join_rel(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2)
{
	Relids			joinrelids;
	JoinMethodHint *hint;
	RelOptInfo	   *rel;
	int				save_nestlevel;

	joinrelids = bms_union(rel1->relids, rel2->relids);
	hint = scan_join_hint(joinrelids);
	bms_free(joinrelids);

	if (!hint)
		return make_join_rel(root, rel1, rel2);

	save_nestlevel = NewGUCNestLevel();

	set_join_config_options(hint->enforce_mask, global->context);

	rel = make_join_rel(root, rel1, rel2);

	/*
	 * Restore the GUC variables we set above.
	 */
	AtEOXact_GUC(true, save_nestlevel);

	return rel;
}

static RelOptInfo *
pg_hint_plan_join_search(PlannerInfo *root, int levels_needed, List *initial_rels)
{
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

	transform_join_hints(global, root, levels_needed, initial_rels);
	rebuild_scan_path(global, root, levels_needed, initial_rels);

	/*
	 * GEQOを使用する条件を満たした場合は、GEQOを用いた結合方式の検索を行う。
	 * このとき、スキャン方式のヒントとSetヒントのみが有効になり、結合方式や結合
	 * 順序はヒント句は無効になりGEQOのアルゴリズムで決定される。
	 */
	if (enable_geqo && levels_needed >= geqo_threshold)
		return geqo(root, levels_needed, initial_rels);

	return standard_join_search_org(root, levels_needed, initial_rels);
}

#define standard_join_search standard_join_search_org
#define join_search_one_level pg_hint_plan_join_search_one_level
#define make_join_rel pg_hint_plan_make_join_rel
#include "core.c"

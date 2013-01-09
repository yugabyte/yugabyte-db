/*-------------------------------------------------------------------------
 *
 * pg_hint_plan.c
 *		  do instructions or hints to the planner using C-style block comments
 *		  of the SQL.
 *
 * Copyright (c) 2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "commands/prepare.h"
#include "mb/pg_wchar.h"
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
#include "parser/scansup.h"
#include "tcop/utility.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#if PG_VERSION_NUM >= 90200
#include "catalog/pg_class.h"
#endif

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#if PG_VERSION_NUM < 90100
#error unsupported PostgreSQL version
#endif

#define BLOCK_COMMENT_START		"/*"
#define BLOCK_COMMENT_END		"*/"
#define HINT_COMMENT_KEYWORD	"+"
#define HINT_START				BLOCK_COMMENT_START HINT_COMMENT_KEYWORD
#define HINT_END				BLOCK_COMMENT_END

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
#define HINT_INDEXONLYSCAN		"IndexOnlyScan"
#define HINT_NOINDEXONLYSCAN	"NoIndexOnlyScan"
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
	ENABLE_SEQSCAN = 0x01,
	ENABLE_INDEXSCAN = 0x02,
	ENABLE_BITMAPSCAN = 0x04,
	ENABLE_TIDSCAN = 0x08,
#if PG_VERSION_NUM >= 90200
	ENABLE_INDEXONLYSCAN = 0x10
#endif
} SCAN_TYPE_BITS;

enum
{
	ENABLE_NESTLOOP = 0x01,
	ENABLE_MERGEJOIN = 0x02,
	ENABLE_HASHJOIN = 0x04
} JOIN_TYPE_BITS;

#if PG_VERSION_NUM >= 90200
#define ENABLE_ALL_SCAN (ENABLE_SEQSCAN | ENABLE_INDEXSCAN | \
						 ENABLE_BITMAPSCAN | ENABLE_TIDSCAN | \
						 ENABLE_INDEXONLYSCAN)
#else
#define ENABLE_ALL_SCAN (ENABLE_SEQSCAN | ENABLE_INDEXSCAN | \
						 ENABLE_BITMAPSCAN | ENABLE_TIDSCAN)
#endif
#define ENABLE_ALL_JOIN (ENABLE_NESTLOOP | ENABLE_MERGEJOIN | ENABLE_HASHJOIN)
#define DISABLE_ALL_SCAN 0
#define DISABLE_ALL_JOIN 0

typedef struct Hint Hint;
typedef struct HintState HintState;

typedef Hint *(*HintCreateFunction) (const char *hint_str,
									 const char *keyword);
typedef void (*HintDeleteFunction) (Hint *hint);
typedef void (*HintDumpFunction) (Hint *hint, StringInfo buf);
typedef int (*HintCmpFunction) (const Hint *a, const Hint *b);
typedef const char *(*HintParseFunction) (Hint *hint, HintState *hstate,
										  Query *parse, const char *str);

/* hint types */
#define NUM_HINT_TYPE	4
typedef enum HintType
{
	HINT_TYPE_SCAN_METHOD,
	HINT_TYPE_JOIN_METHOD,
	HINT_TYPE_LEADING,
	HINT_TYPE_SET
} HintType;

static const char *HintTypeName[] = {
	"scan method",
	"join method",
	"leading",
	"set"
};

/* hint status */
typedef enum HintStatus
{
	HINT_STATE_NOTUSED = 0,		/* specified relation not used in query */
	HINT_STATE_USED,			/* hint is used */
	HINT_STATE_DUPLICATION,		/* specified hint duplication */
	HINT_STATE_ERROR			/* execute error (parse error does not include
								 * it) */
} HintStatus;

#define hint_state_enabled(hint) ((hint)->base.state == HINT_STATE_NOTUSED || \
								  (hint)->base.state == HINT_STATE_USED)

/* common data for all hints. */
struct Hint
{
	const char		   *hint_str;		/* must not do pfree */
	const char		   *keyword;		/* must not do pfree */
	HintType			type;
	HintStatus			state;
	HintDeleteFunction	delete_func;
	HintDumpFunction	dump_func;
	HintCmpFunction		cmp_func;
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
struct HintState
{
	char		   *hint_str;			/* original hint string */

	/* all hint */
	int				nall_hints;			/* # of valid all hints */
	int				max_all_hints;		/* # of slots for all hints */
	Hint		  **all_hints;			/* parsed all hints */

	/* # of each hints */
	int				num_hints[NUM_HINT_TYPE];

	/* for scan method hints */
	ScanMethodHint **scan_hints;		/* parsed scan hints */
	int				init_scan_mask;		/* initial value scan parameter */
	Index			parent_relid;		/* inherit parent table relid */
	ScanMethodHint *parent_hint;		/* inherit parent table scan hint */

	/* for join method hints */
	JoinMethodHint **join_hints;		/* parsed join hints */
	int				init_join_mask;		/* initial value join parameter */
	List		  **join_hint_level;

	/* for Leading hint */
	LeadingHint	   *leading_hint;		/* parsed last specified Leading hint */

	/* for Set hints */
	SetHint		  **set_hints;			/* parsed Set hints */
	GucContext		context;			/* which GUC parameters can we set? */
};

/*
 * Describes a hint parser module which is bound with particular hint keyword.
 */
typedef struct HintParser
{
	char			   *keyword;
	HintCreateFunction	create_func;
} HintParser;

/* Module callbacks */
void		_PG_init(void);
void		_PG_fini(void);

static void push_hint(HintState *hstate);
static void pop_hint(void);

static void pg_hint_plan_ProcessUtility(Node *parsetree,
										const char *queryString,
										ParamListInfo params, bool isTopLevel,
										DestReceiver *dest,
										char *completionTag);
static PlannedStmt *pg_hint_plan_planner(Query *parse, int cursorOptions,
										 ParamListInfo boundParams);
static void pg_hint_plan_get_relation_info(PlannerInfo *root,
										   Oid relationObjectId,
										   bool inhparent, RelOptInfo *rel);
static RelOptInfo *pg_hint_plan_join_search(PlannerInfo *root,
											int levels_needed,
											List *initial_rels);

static Hint *ScanMethodHintCreate(const char *hint_str, const char *keyword);
static void ScanMethodHintDelete(ScanMethodHint *hint);
static void ScanMethodHintDump(ScanMethodHint *hint, StringInfo buf);
static int ScanMethodHintCmp(const ScanMethodHint *a, const ScanMethodHint *b);
static const char *ScanMethodHintParse(ScanMethodHint *hint, HintState *hstate,
									   Query *parse, const char *str);
static Hint *JoinMethodHintCreate(const char *hint_str, const char *keyword);
static void JoinMethodHintDelete(JoinMethodHint *hint);
static void JoinMethodHintDump(JoinMethodHint *hint, StringInfo buf);
static int JoinMethodHintCmp(const JoinMethodHint *a, const JoinMethodHint *b);
static const char *JoinMethodHintParse(JoinMethodHint *hint, HintState *hstate,
									   Query *parse, const char *str);
static Hint *LeadingHintCreate(const char *hint_str, const char *keyword);
static void LeadingHintDelete(LeadingHint *hint);
static void LeadingHintDump(LeadingHint *hint, StringInfo buf);
static int LeadingHintCmp(const LeadingHint *a, const LeadingHint *b);
static const char *LeadingHintParse(LeadingHint *hint, HintState *hstate,
									Query *parse, const char *str);
static Hint *SetHintCreate(const char *hint_str, const char *keyword);
static void SetHintDelete(SetHint *hint);
static void SetHintDump(SetHint *hint, StringInfo buf);
static int SetHintCmp(const SetHint *a, const SetHint *b);
static const char *SetHintParse(SetHint *hint, HintState *hstate, Query *parse,
								const char *str);

RelOptInfo *pg_hint_plan_standard_join_search(PlannerInfo *root,
											  int levels_needed,
											  List *initial_rels);
void pg_hint_plan_join_search_one_level(PlannerInfo *root, int level);
static void make_rels_by_clause_joins(PlannerInfo *root, RelOptInfo *old_rel,
									  ListCell *other_rels);
static void make_rels_by_clauseless_joins(PlannerInfo *root,
										  RelOptInfo *old_rel,
										  ListCell *other_rels);
static bool has_join_restriction(PlannerInfo *root, RelOptInfo *rel);
static void set_append_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
									Index rti, RangeTblEntry *rte);
#if PG_VERSION_NUM >= 90200
static void generate_mergeappend_paths(PlannerInfo *root, RelOptInfo *rel,
						   List *live_childrels,
						   List *all_child_pathkeys);
#endif
static List *accumulate_append_subpath(List *subpaths, Path *path);
#if PG_VERSION_NUM < 90200
static void set_dummy_rel_pathlist(RelOptInfo *rel);
#endif
RelOptInfo *pg_hint_plan_make_join_rel(PlannerInfo *root, RelOptInfo *rel1,
									   RelOptInfo *rel2);

/* GUC variables */
static bool	pg_hint_plan_enable_hint = true;
static bool	pg_hint_plan_debug_print = false;
static int	pg_hint_plan_parse_messages = INFO;

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
	 * {"fatal", FATAL, true},
	 * {"panic", PANIC, true},
	 */
	{NULL, 0, false}
};

/* Saved hook values in case of unload */
static ProcessUtility_hook_type prev_ProcessUtility = NULL;
static planner_hook_type prev_planner = NULL;
static get_relation_info_hook_type prev_get_relation_info = NULL;
static join_search_hook_type prev_join_search = NULL;

/* Hold reference to currently active hint */
static HintState *current_hint = NULL;

/*
 * List of hint contexts.  We treat the head of the list as the Top of the
 * context stack, so current_hint always points the first element of this list.
 */
static List *HintStateStack = NIL;

/*
 * Holds statement name during executing EXECUTE command.  NULL for other
 * statements.
 */
static char	   *stmt_name = NULL;

static const HintParser parsers[] = {
	{HINT_SEQSCAN, ScanMethodHintCreate},
	{HINT_INDEXSCAN, ScanMethodHintCreate},
	{HINT_BITMAPSCAN, ScanMethodHintCreate},
	{HINT_TIDSCAN, ScanMethodHintCreate},
	{HINT_NOSEQSCAN, ScanMethodHintCreate},
	{HINT_NOINDEXSCAN, ScanMethodHintCreate},
	{HINT_NOBITMAPSCAN, ScanMethodHintCreate},
	{HINT_NOTIDSCAN, ScanMethodHintCreate},
#if PG_VERSION_NUM >= 90200
	{HINT_INDEXONLYSCAN, ScanMethodHintCreate},
	{HINT_NOINDEXONLYSCAN, ScanMethodHintCreate},
#endif
	{HINT_NESTLOOP, JoinMethodHintCreate},
	{HINT_MERGEJOIN, JoinMethodHintCreate},
	{HINT_HASHJOIN, JoinMethodHintCreate},
	{HINT_NONESTLOOP, JoinMethodHintCreate},
	{HINT_NOMERGEJOIN, JoinMethodHintCreate},
	{HINT_NOHASHJOIN, JoinMethodHintCreate},
	{HINT_LEADING, LeadingHintCreate},
	{HINT_SET, SetHintCreate},
	{NULL, NULL}
};

/*
 * Module load callbacks
 */
void
_PG_init(void)
{
	/* Define custom GUC variables. */
	DefineCustomBoolVariable("pg_hint_plan.enable_hint",
			 "Force planner to use plans specified in the hint comment preceding to the query.",
							 NULL,
							 &pg_hint_plan_enable_hint,
							 true,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("pg_hint_plan.debug_print",
							 "Logs results of hint parsing.",
							 NULL,
							 &pg_hint_plan_debug_print,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomEnumVariable("pg_hint_plan.parse_messages",
							 "Message level of parse errors.",
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

/*
 * create and delete functions the hint object
 */

static Hint *
ScanMethodHintCreate(const char *hint_str, const char *keyword)
{
	ScanMethodHint *hint;

	hint = palloc(sizeof(ScanMethodHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.type = HINT_TYPE_SCAN_METHOD;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) ScanMethodHintDelete;
	hint->base.dump_func = (HintDumpFunction) ScanMethodHintDump;
	hint->base.cmp_func = (HintCmpFunction) ScanMethodHintCmp;
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
	hint->base.type = HINT_TYPE_JOIN_METHOD;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) JoinMethodHintDelete;
	hint->base.dump_func = (HintDumpFunction) JoinMethodHintDump;
	hint->base.cmp_func = (HintCmpFunction) JoinMethodHintCmp;
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
	hint->base.type = HINT_TYPE_LEADING;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction)LeadingHintDelete;
	hint->base.dump_func = (HintDumpFunction) LeadingHintDump;
	hint->base.cmp_func = (HintCmpFunction) LeadingHintCmp;
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
	hint->base.type = HINT_TYPE_SET;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) SetHintDelete;
	hint->base.dump_func = (HintDumpFunction) SetHintDump;
	hint->base.cmp_func = (HintCmpFunction) SetHintCmp;
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

static HintState *
HintStateCreate(void)
{
	HintState   *hstate;

	hstate = palloc(sizeof(HintState));
	hstate->hint_str = NULL;
	hstate->nall_hints = 0;
	hstate->max_all_hints = 0;
	hstate->all_hints = NULL;
	memset(hstate->num_hints, 0, sizeof(hstate->num_hints));
	hstate->scan_hints = NULL;
	hstate->init_scan_mask = 0;
	hstate->parent_relid = 0;
	hstate->parent_hint = NULL;
	hstate->join_hints = NULL;
	hstate->init_join_mask = 0;
	hstate->join_hint_level = NULL;
	hstate->leading_hint = NULL;
	hstate->context = superuser() ? PGC_SUSET : PGC_USERSET;
	hstate->set_hints = NULL;

	return hstate;
}

static void
HintStateDelete(HintState *hstate)
{
	int			i;

	if (!hstate)
		return;

	if (hstate->hint_str)
		pfree(hstate->hint_str);

	for (i = 0; i < hstate->num_hints[HINT_TYPE_SCAN_METHOD]; i++)
		hstate->all_hints[i]->delete_func(hstate->all_hints[i]);
	if (hstate->all_hints)
		pfree(hstate->all_hints);
}

/*
 * dump functions
 */

static void
dump_quote_value(StringInfo buf, const char *value)
{
	bool		need_quote = false;
	const char *str;

	for (str = value; *str != '\0'; str++)
	{
		if (isspace(*str) || *str == ')' || *str == '"')
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
ScanMethodHintDump(ScanMethodHint *hint, StringInfo buf)
{
	ListCell   *l;

	appendStringInfo(buf, "%s(", hint->base.keyword);
	dump_quote_value(buf, hint->relname);
	foreach(l, hint->indexnames)
	{
		appendStringInfoCharMacro(buf, ' ');
		dump_quote_value(buf, (char *) lfirst(l));
	}
	appendStringInfoString(buf, ")\n");
}

static void
JoinMethodHintDump(JoinMethodHint *hint, StringInfo buf)
{
	int	i;

	appendStringInfo(buf, "%s(", hint->base.keyword);
	dump_quote_value(buf, hint->relnames[0]);
	for (i = 1; i < hint->nrels; i++)
	{
		appendStringInfoCharMacro(buf, ' ');
		dump_quote_value(buf, hint->relnames[i]);
	}
	appendStringInfoString(buf, ")\n");

}

static void
LeadingHintDump(LeadingHint *hint, StringInfo buf)
{
	bool		is_first;
	ListCell   *l;

	appendStringInfo(buf, "%s(", HINT_LEADING);
	is_first = true;
	foreach(l, hint->relations)
	{
		if (is_first)
			is_first = false;
		else
			appendStringInfoCharMacro(buf, ' ');

		dump_quote_value(buf, (char *) lfirst(l));
	}

	appendStringInfoString(buf, ")\n");
}

static void
SetHintDump(SetHint *hint, StringInfo buf)
{
	appendStringInfo(buf, "%s(", HINT_SET);
	dump_quote_value(buf, hint->name);
	appendStringInfoCharMacro(buf, ' ');
	dump_quote_value(buf, hint->value);
	appendStringInfo(buf, ")\n");
}

static void
all_hint_dump(HintState *hstate, StringInfo buf, const char *title,
			  HintStatus state)
{
	int	i;

	appendStringInfo(buf, "%s:\n", title);
	for (i = 0; i < hstate->nall_hints; i++)
	{
		if (hstate->all_hints[i]->state != state)
			continue;

		hstate->all_hints[i]->dump_func(hstate->all_hints[i], buf);
	}
}

static void
HintStateDump(HintState *hstate)
{
	StringInfoData	buf;

	if (!hstate)
	{
		elog(LOG, "pg_hint_plan:\nno hint");
		return;
	}

	initStringInfo(&buf);

	appendStringInfoString(&buf, "pg_hint_plan:\n");
	all_hint_dump(hstate, &buf, "used hint", HINT_STATE_USED);
	all_hint_dump(hstate, &buf, "not used hint", HINT_STATE_NOTUSED);
	all_hint_dump(hstate, &buf, "duplication hint", HINT_STATE_DUPLICATION);
	all_hint_dump(hstate, &buf, "error hint", HINT_STATE_ERROR);

	elog(LOG, "%s", buf.data);

	pfree(buf.data);
}

/*
 * compare functions
 */

static int
RelnameCmp(const void *a, const void *b)
{
	const char *relnamea = *((const char **) a);
	const char *relnameb = *((const char **) b);

	return strcmp(relnamea, relnameb);
}

static int
ScanMethodHintCmp(const ScanMethodHint *a, const ScanMethodHint *b)
{
	return RelnameCmp(&a->relname, &b->relname);
}

static int
JoinMethodHintCmp(const JoinMethodHint *a, const JoinMethodHint *b)
{
	int	i;

	if (a->nrels != b->nrels)
		return a->nrels - b->nrels;

	for (i = 0; i < a->nrels; i++)
	{
		int	result;
		if ((result = RelnameCmp(&a->relnames[i], &b->relnames[i])) != 0)
			return result;
	}

	return 0;
}

static int
LeadingHintCmp(const LeadingHint *a, const LeadingHint *b)
{
	return 0;
}

static int
SetHintCmp(const SetHint *a, const SetHint *b)
{
	return strcmp(a->name, b->name);
}

static int
HintCmp(const void *a, const void *b)
{
	const Hint *hinta = *((const Hint **) a);
	const Hint *hintb = *((const Hint **) b);

	if (hinta->type != hintb->type)
		return hinta->type - hintb->type;

	return hinta->cmp_func(hinta, hintb);
}

/*
 * Returns byte offset of hint b from hint a.  If hint a was specified before
 * b, positive value is returned.
 */
static int
HintCmpWithPos(const void *a, const void *b)
{
	const Hint *hinta = *((const Hint **) a);
	const Hint *hintb = *((const Hint **) b);
	int		result;

	result = HintCmp(a, b);
	if (result == 0)
		result = hinta->hint_str - hintb->hint_str;

	return result;
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
		parse_ereport(str, ("Opening parenthesis is necessary."));
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
		parse_ereport(str, ("Closing parenthesis is necessary."));
		return NULL;
	}

	str++;

	return str;
}

/*
 * Parse a token from str, and store malloc'd copy into word.  A token can be
 * quoted with '"'.  Return value is pointer to unparsed portion of original
 * string, or NULL if an error occurred.
 *
 * Parsed token is truncated within NAMEDATALEN-1 bytes, when truncate is true.
 */
static const char *
parse_quote_value(const char *str, char **word, bool truncate)
{
	StringInfoData	buf;
	bool			in_quote;

	/* Skip leading spaces. */
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
			/* Double quotation must be closed. */
			if (*str == '\0')
			{
				pfree(buf.data);
				parse_ereport(str, ("Unterminated quoted string."));
				return NULL;
			}

			/*
			 * Skip escaped double quotation.
			 * 
			 * We don't allow slash-asterisk and asterisk-slash (delimiters of
			 * block comments) to be an object name, so users must specify
			 * alias for such object names.
			 *
			 * Those special names can be allowed if we care escaped slashes
			 * and asterisks, but we don't.
			 */
			if (*str == '"')
			{
				str++;
				if (*str != '"')
					break;
			}
		}
		else if (isspace(*str) || *str == ')' || *str == '"' || *str == '\0')
			break;

		appendStringInfoCharMacro(&buf, *str++);
	}

	if (buf.len == 0)
	{
		parse_ereport(str, ("Zero-length delimited string."));

		pfree(buf.data);

		return NULL;
	}

	/* Truncate name if it's too long */
	if (truncate)
		truncate_identifier(buf.data, strlen(buf.data), true);

	*word = buf.data;

	return str;
}

static void
parse_hints(HintState *hstate, Query *parse, const char *str)
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

			/* parser of each hint does parse in a parenthesis. */
			if ((str = skip_opened_parenthesis(str)) == NULL ||
				(str = hint->parser_func(hint, hstate, parse, str)) == NULL ||
				(str = skip_closed_parenthesis(str)) == NULL)
			{
				hint->delete_func(hint);
				pfree(buf.data);
				return;
			}

			/*
			 * Add hint information into all_hints array.  If we don't have
			 * enough space, double the array.
			 */
			if (hstate->nall_hints == 0)
			{
				hstate->max_all_hints = HINT_ARRAY_DEFAULT_INITSIZE;
				hstate->all_hints = (Hint **)
					palloc(sizeof(Hint *) * hstate->max_all_hints);
			}
			else if (hstate->nall_hints == hstate->max_all_hints)
			{
				hstate->max_all_hints *= 2;
				hstate->all_hints = (Hint **)
					repalloc(hstate->all_hints,
							 sizeof(Hint *) * hstate->max_all_hints);
			}

			hstate->all_hints[hstate->nall_hints] = hint;
			hstate->nall_hints++;

			skip_space(str);

			break;
		}

		if (parser->keyword == NULL)
		{
			parse_ereport(head,
						  ("Unrecognized hint keyword \"%s\".", buf.data));
			pfree(buf.data);
			return;
		}
	}

	pfree(buf.data);
}

/*
 * Do basic parsing of the query head comment.
 */
static HintState *
parse_head_comment(Query *parse)
{
	const char *p;
	char	   *head;
	char	   *tail;
	int			len;
	int			i;
	HintState   *hstate;

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

	/* We don't support nested block comments. */
	if ((head = strstr(p, BLOCK_COMMENT_START)) != NULL && head < tail)
	{
		parse_ereport(head, ("Nested block comments are not supported."));
		return NULL;
	}

	/* Make a copy of hint. */
	len = tail - p;
	head = palloc(len + 1);
	memcpy(head, p, len);
	head[len] = '\0';
	p = head;

	hstate = HintStateCreate();
	hstate->hint_str = head;

	/* parse each hint. */
	parse_hints(hstate, parse, p);

	/* When nothing specified a hint, we free HintState and returns NULL. */
	if (hstate->nall_hints == 0)
	{
		HintStateDelete(hstate);
		return NULL;
	}

	/* Sort hints in order of original position. */
	qsort(hstate->all_hints, hstate->nall_hints, sizeof(Hint *),
		  HintCmpWithPos);

	/*
	 * If we have hints which are specified for an object, mark preceding one
	 * as 'duplicated' to ignore it in planner phase.
	 */
	for (i = 0; i < hstate->nall_hints; i++)
	{
		Hint   *cur_hint = hstate->all_hints[i];
		Hint   *next_hint;

		/* Count up hints per hint-type. */
		hstate->num_hints[cur_hint->type]++;

		/* If we don't have next, nothing to compare. */
		if (i + 1 >= hstate->nall_hints)
			break;
		next_hint = hstate->all_hints[i + 1];

		/*
		 * We need to pass address of hint pointers, because HintCmp has
		 * been designed to be used with qsort.
		 */
		if (HintCmp(&cur_hint, &next_hint) == 0)
		{
			parse_ereport(cur_hint->hint_str,
						  ("Conflict %s hint.", HintTypeName[cur_hint->type]));
			cur_hint->state = HINT_STATE_DUPLICATION;
		}
	}

	/*
	 * Make sure that per-type array pointers point proper position in the
	 * array which consists of all hints.
	 */
	hstate->scan_hints = (ScanMethodHint **) hstate->all_hints;
	hstate->join_hints = (JoinMethodHint **) hstate->all_hints +
		hstate->num_hints[HINT_TYPE_SCAN_METHOD];
	hstate->leading_hint = (LeadingHint *) hstate->all_hints[
		hstate->num_hints[HINT_TYPE_SCAN_METHOD] +
		hstate->num_hints[HINT_TYPE_JOIN_METHOD] +
		hstate->num_hints[HINT_TYPE_LEADING] - 1];
	hstate->set_hints = (SetHint **) hstate->all_hints +
		hstate->num_hints[HINT_TYPE_SCAN_METHOD] +
		hstate->num_hints[HINT_TYPE_JOIN_METHOD] +
		hstate->num_hints[HINT_TYPE_LEADING];

	return hstate;
}

/*
 * Parse inside of parentheses of scan-method hints.
 */
static const char *
ScanMethodHintParse(ScanMethodHint *hint, HintState *hstate, Query *parse,
					const char *str)
{
	const char *keyword = hint->base.keyword;

	/* Given hint is invalid if relation name can't be parsed. */
	if ((str = parse_quote_value(str, &hint->relname, true))
		== NULL)
		return NULL;

	skip_space(str);

	/* Parse index name(s) if given hint accepts. */
	if (strcmp(keyword, HINT_INDEXSCAN) == 0 ||
#if PG_VERSION_NUM >= 90200
		strcmp(keyword, HINT_INDEXONLYSCAN) == 0 ||
#endif
		strcmp(keyword, HINT_BITMAPSCAN) == 0)
	{
		while (*str != ')' && *str != '\0')
		{
			char	   *indexname;

			str = parse_quote_value(str, &indexname, true);
			if (str == NULL)
				return NULL;

			hint->indexnames = lappend(hint->indexnames, indexname);
			skip_space(str);
		}
	}

	/* Set a bit for specified hint. */
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
#if PG_VERSION_NUM >= 90200
	else if (strcasecmp(keyword, HINT_INDEXONLYSCAN) == 0)
		hint->enforce_mask = ENABLE_INDEXSCAN | ENABLE_INDEXONLYSCAN;
	else if (strcasecmp(keyword, HINT_NOINDEXONLYSCAN) == 0)
		hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_INDEXONLYSCAN;
#endif
	else
	{
		parse_ereport(str, ("Unrecognized hint keyword \"%s\".", keyword));
		return NULL;
	}

	return str;
}

static const char *
JoinMethodHintParse(JoinMethodHint *hint, HintState *hstate, Query *parse,
					const char *str)
{
	char	   *relname;
	const char *keyword = hint->base.keyword;

	skip_space(str);

	hint->relnames = palloc(sizeof(char *));

	while ((str = parse_quote_value(str, &relname, true))
		   != NULL)
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

	/* A join hint requires at least two relations to be specified. */
	if (hint->nrels < 2)
	{
		parse_ereport(str,
					  ("%s hint requires at least two relations.",
					   hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;
	}

	/* Sort hints in alphabetical order of relation names. */
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

	return str;
}

static const char *
LeadingHintParse(LeadingHint *hint, HintState *hstate, Query *parse,
				 const char *str)
{
	skip_space(str);

	while (*str != ')')
	{
		char   *relname;

		if ((str = parse_quote_value(str, &relname, true))
			== NULL)
			return NULL;

		hint->relations = lappend(hint->relations, relname);

		skip_space(str);
	}

	/* A Leading hint requires at least two relations to be specified. */
	if (list_length(hint->relations) < 2)
	{
		parse_ereport(hint->base.hint_str,
					  ("%s hint requires at least two relations.",
					   HINT_LEADING));
		hint->base.state = HINT_STATE_ERROR;
	}

	return str;
}

static const char *
SetHintParse(SetHint *hint, HintState *hstate, Query *parse, const char *str)
{
	if ((str = parse_quote_value(str, &hint->name, true))
		== NULL ||
		(str = parse_quote_value(str, &hint->value, false))
		== NULL)
		return NULL;

	return str;
}

/*
 * set GUC parameter functions
 */

static int
set_config_option_wrapper(const char *name, const char *value,
						  GucContext context, GucSource source,
						  GucAction action, bool changeVal, int elevel)
{
	int				result = 0;
	MemoryContext	ccxt = CurrentMemoryContext;

	PG_TRY();
	{
#if PG_VERSION_NUM >= 90200
		result = set_config_option(name, value, context, source,
								   action, changeVal, 0);
#else
		result = set_config_option(name, value, context, source,
								   action, changeVal);
#endif
	}
	PG_CATCH();
	{
		ErrorData	   *errdata;

		/* Save error info */
		MemoryContextSwitchTo(ccxt);
		errdata = CopyErrorData();
		FlushErrorState();

		ereport(elevel, (errcode(errdata->sqlerrcode),
				errmsg("%s", errdata->message),
				errdata->detail ? errdetail("%s", errdata->detail) : 0,
				errdata->hint ? errhint("%s", errdata->hint) : 0));
		FreeErrorData(errdata);
	}
	PG_END_TRY();

	return result;
}

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

		result = set_config_option_wrapper(hint->name, hint->value, context,
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
	set_config_option_wrapper((name), \
		(mask & (type_bits)) ? "true" : "false", \
		context, PGC_S_SESSION, GUC_ACTION_SAVE, true, ERROR)

static void
set_scan_config_options(unsigned char enforce_mask, GucContext context)
{
	unsigned char	mask;

	if (enforce_mask == ENABLE_SEQSCAN || enforce_mask == ENABLE_INDEXSCAN ||
		enforce_mask == ENABLE_BITMAPSCAN || enforce_mask == ENABLE_TIDSCAN
#if PG_VERSION_NUM >= 90200
		|| enforce_mask == (ENABLE_INDEXSCAN | ENABLE_INDEXONLYSCAN)
#endif
		)
		mask = enforce_mask;
	else
		mask = enforce_mask & current_hint->init_scan_mask;

	SET_CONFIG_OPTION("enable_seqscan", ENABLE_SEQSCAN);
	SET_CONFIG_OPTION("enable_indexscan", ENABLE_INDEXSCAN);
	SET_CONFIG_OPTION("enable_bitmapscan", ENABLE_BITMAPSCAN);
	SET_CONFIG_OPTION("enable_tidscan", ENABLE_TIDSCAN);
#if PG_VERSION_NUM >= 90200
	SET_CONFIG_OPTION("enable_indexonlyscan", ENABLE_INDEXONLYSCAN);
#endif
}

static void
set_join_config_options(unsigned char enforce_mask, GucContext context)
{
	unsigned char	mask;

	if (enforce_mask == ENABLE_NESTLOOP || enforce_mask == ENABLE_MERGEJOIN ||
		enforce_mask == ENABLE_HASHJOIN)
		mask = enforce_mask;
	else
		mask = enforce_mask & current_hint->init_join_mask;

	SET_CONFIG_OPTION("enable_nestloop", ENABLE_NESTLOOP);
	SET_CONFIG_OPTION("enable_mergejoin", ENABLE_MERGEJOIN);
	SET_CONFIG_OPTION("enable_hashjoin", ENABLE_HASHJOIN);
}

/*
 * pg_hint_plan hook functions
 */

static void
pg_hint_plan_ProcessUtility(Node *parsetree, const char *queryString,
							ParamListInfo params, bool isTopLevel,
							DestReceiver *dest, char *completionTag)
{
	Node				   *node;

	if (!pg_hint_plan_enable_hint)
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
	if (IsA(node, ExplainStmt))
	{
		/*
		 * Draw out parse tree of actual query from Query struct of EXPLAIN
		 * statement.
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
	 * If the query was a EXECUTE or CREATE TABLE AS EXECUTE, get query string
	 * specified to preceding PREPARE command to use it as source of hints.
	 */
	if (IsA(node, ExecuteStmt))
	{
		ExecuteStmt	   *stmt;

		stmt = (ExecuteStmt *) node;
		stmt_name = stmt->name;
	}
#if PG_VERSION_NUM >= 90200
	/*
	 * CREATE AS EXECUTE behavior has changed since 9.2, so we must handle it
	 * specially here.
	 */
	if (IsA(node, CreateTableAsStmt))
	{
		CreateTableAsStmt	   *stmt;
		Query		   *query;

		stmt = (CreateTableAsStmt *) node;
		Assert(IsA(stmt->query, Query));
		query = (Query *) stmt->query;

		if (query->commandType == CMD_UTILITY &&
			IsA(query->utilityStmt, ExecuteStmt))
		{
			ExecuteStmt *estmt = (ExecuteStmt *) query->utilityStmt;
			stmt_name = estmt->name;
		}
	}
#endif
	if (stmt_name)
	{
		PG_TRY();
		{
			if (prev_ProcessUtility)
				(*prev_ProcessUtility) (parsetree, queryString, params,
										isTopLevel, dest, completionTag);
			else
				standard_ProcessUtility(parsetree, queryString, params,
										isTopLevel, dest, completionTag);
		}
		PG_CATCH();
		{
			stmt_name = NULL;
			PG_RE_THROW();
		}
		PG_END_TRY();

		stmt_name = NULL;

		return;
	}

	if (prev_ProcessUtility)
		(*prev_ProcessUtility) (parsetree, queryString, params,
								isTopLevel, dest, completionTag);
	else
		standard_ProcessUtility(parsetree, queryString, params,
								isTopLevel, dest, completionTag);
}

/*
 * Push a hint into hint stack which is implemented with List struct.  Head of
 * list is top of stack.
 */
static void
push_hint(HintState *hstate)
{
	/* Prepend new hint to the list means pushing to stack. */
	HintStateStack = lcons(hstate, HintStateStack);

	/* Pushed hint is the one which should be used hereafter. */
	current_hint = hstate;
}

/* Pop a hint from hint stack.  Popped hint is automatically discarded. */
static void
pop_hint(void)
{
	/* Hint stack must not be empty. */
	if(HintStateStack == NIL)
		elog(ERROR, "hint stack is empty");

	/*
	 * Take a hint at the head from the list, and free it.  Switch current_hint
	 * to point new head (NULL if the list is empty).
	 */
	HintStateStack = list_delete_first(HintStateStack);
	HintStateDelete(current_hint);
	if(HintStateStack == NIL)
		current_hint = NULL;
	else
		current_hint = (HintState *) lfirst(list_head(HintStateStack));
}

static PlannedStmt *
pg_hint_plan_planner(Query *parse, int cursorOptions, ParamListInfo boundParams)
{
	int				save_nestlevel;
	PlannedStmt	   *result;
	HintState	   *hstate;

	/*
	 * Use standard planner if pg_hint_plan is disabled.  Other hook functions
	 * try to change plan with current_hint if any, so set it to NULL.
	 */
	if (!pg_hint_plan_enable_hint)
	{
		current_hint = NULL;

		if (prev_planner)
			return (*prev_planner) (parse, cursorOptions, boundParams);
		else
			return standard_planner(parse, cursorOptions, boundParams);
	}

	/* Create hint struct from parse tree. */
	hstate = parse_head_comment(parse);

	/*
	 * Use standard planner if the statement has not valid hint.  Other hook
	 * functions try to change plan with current_hint if any, so set it to
	 * NULL.
	 */
	if (!hstate)
	{
		current_hint = NULL;

		if (prev_planner)
			return (*prev_planner) (parse, cursorOptions, boundParams);
		else
			return standard_planner(parse, cursorOptions, boundParams);
	}

	/*
	 * Push new hint struct to the hint stack to disable previous hint context.
	 */
	push_hint(hstate);

	/* Set GUC parameters which are specified with Set hint. */
	save_nestlevel = set_config_options(current_hint->set_hints,
										current_hint->num_hints[HINT_TYPE_SET],
										current_hint->context);

	if (enable_seqscan)
		current_hint->init_scan_mask |= ENABLE_SEQSCAN;
	if (enable_indexscan)
		current_hint->init_scan_mask |= ENABLE_INDEXSCAN;
	if (enable_bitmapscan)
		current_hint->init_scan_mask |= ENABLE_BITMAPSCAN;
	if (enable_tidscan)
		current_hint->init_scan_mask |= ENABLE_TIDSCAN;
#if PG_VERSION_NUM >= 90200
	if (enable_indexonlyscan)
		current_hint->init_scan_mask |= ENABLE_INDEXONLYSCAN;
#endif
	if (enable_nestloop)
		current_hint->init_join_mask |= ENABLE_NESTLOOP;
	if (enable_mergejoin)
		current_hint->init_join_mask |= ENABLE_MERGEJOIN;
	if (enable_hashjoin)
		current_hint->init_join_mask |= ENABLE_HASHJOIN;

	/*
	 * Use PG_TRY mechanism to recover GUC parameters and current_hint to the
	 * state when this planner started when error occurred in planner.
	 */
	PG_TRY();
	{
		if (prev_planner)
			result = (*prev_planner) (parse, cursorOptions, boundParams);
		else
			result = standard_planner(parse, cursorOptions, boundParams);
	}
	PG_CATCH();
	{
		/*
		 * Rollback changes of GUC parameters, and pop current hint context
		 * from hint stack to rewind the state.
		 */
		AtEOXact_GUC(true, save_nestlevel);
		pop_hint();
		PG_RE_THROW();
	}
	PG_END_TRY();

	/* Print hint in debug mode. */
	if (pg_hint_plan_debug_print)
		HintStateDump(current_hint);

	/*
	 * Rollback changes of GUC parameters, and pop current hint context from
	 * hint stack to rewind the state.
	 */
	AtEOXact_GUC(true, save_nestlevel);
	pop_hint();

	return result;
}

/*
 * Return scan method hint which matches given aliasname.
 */
static ScanMethodHint *
find_scan_hint(PlannerInfo *root, RelOptInfo *rel)
{
	RangeTblEntry  *rte;
	int				i;

	/*
	 * We can't apply scan method hint if the relation is:
	 *   - not a base relation
	 *   - not an ordinary relation (such as join and subquery)
	 */
	if (rel->reloptkind != RELOPT_BASEREL || rel->rtekind != RTE_RELATION)
		return NULL;

	rte = root->simple_rte_array[rel->relid];

	/* We can't force scan method of foreign tables */
	if (rte->relkind == RELKIND_FOREIGN_TABLE)
		return NULL;

	/* Find scan method hint, which matches given names, from the list. */
	for (i = 0; i < current_hint->num_hints[HINT_TYPE_SCAN_METHOD]; i++)
	{
		ScanMethodHint *hint = current_hint->scan_hints[i];

		/* We ignore disabled hints. */
		if (!hint_state_enabled(hint))
			continue;

		if (RelnameCmp(&rte->eref->aliasname, &hint->relname) == 0)
			return hint;
	}

	return NULL;
}

static void
delete_indexes(ScanMethodHint *hint, RelOptInfo *rel)
{
	ListCell	   *cell;
	ListCell	   *prev;
	ListCell	   *next;

	/*
	 * We delete all the IndexOptInfo list and prevent you from being usable by
	 * a scan.
	 */
	if (hint->enforce_mask == ENABLE_SEQSCAN ||
		hint->enforce_mask == ENABLE_TIDSCAN)
	{
		list_free_deep(rel->indexlist);
		rel->indexlist = NIL;
		hint->base.state = HINT_STATE_USED;

		return;
	}

	/*
	 * When a list of indexes is not specified, we just use all indexes.
	 */
	if (hint->indexnames == NIL)
		return;

	/*
	 * Leaving only an specified index, we delete it from a IndexOptInfo list
	 * other than it.
	 */
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

static void
pg_hint_plan_get_relation_info(PlannerInfo *root, Oid relationObjectId,
							   bool inhparent, RelOptInfo *rel)
{
	ScanMethodHint *hint;

	if (prev_get_relation_info)
		(*prev_get_relation_info) (root, relationObjectId, inhparent, rel);

	/* Do nothing if we don't have valid hint in this context. */
	if (!current_hint)
		return;

	if (inhparent)
	{
		/* store does relids of parent table. */
		current_hint->parent_relid = rel->relid;
	}
	else if (current_hint->parent_relid != 0)
	{
		/*
		 * We use the same GUC parameter if this table is the child table of a
		 * table called pg_hint_plan_get_relation_info just before that.
		 */
		ListCell   *l;

		/* append_rel_list contains all append rels; ignore others */
		foreach(l, root->append_rel_list)
		{
			AppendRelInfo *appinfo = (AppendRelInfo *) lfirst(l);

			/* This rel is child table. */
			if (appinfo->parent_relid == current_hint->parent_relid &&
				appinfo->child_relid == rel->relid)
			{
				if (current_hint->parent_hint)
					delete_indexes(current_hint->parent_hint, rel);

				return;
			}
		}

		/* This rel is not inherit table. */
		current_hint->parent_relid = 0;
		current_hint->parent_hint = NULL;
	}

	/*
	 * If scan method hint was given, reset GUC parameters which control
	 * planner behavior about choosing scan methods.
	 */
	if ((hint = find_scan_hint(root, rel)) == NULL)
	{
		set_scan_config_options(current_hint->init_scan_mask,
								current_hint->context);
		return;
	}
	set_scan_config_options(hint->enforce_mask, current_hint->context);
	hint->base.state = HINT_STATE_USED;
	if (inhparent)
		current_hint->parent_hint = hint;

	delete_indexes(hint, rel);
}

/*
 * Return index of relation which matches given aliasname, or 0 if not found.
 * If same aliasname was used multiple times in a query, return -1.
 */
static int
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

		if (RelnameCmp(&aliasname,
					   &root->simple_rte_array[i]->eref->aliasname) != 0)
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
				parse_ereport(str,
							  ("Relation name \"%s\" is ambiguous.",
							   aliasname));
				return -1;
			}

			found = i;
			break;
		}

	}

	return found;
}

/*
 * Return join hint which matches given joinrelids.
 */
static JoinMethodHint *
find_join_hint(Relids joinrelids)
{
	List	   *join_hint;
	ListCell   *l;

	join_hint = current_hint->join_hint_level[bms_num_members(joinrelids)];

	foreach(l, join_hint)
	{
		JoinMethodHint *hint = (JoinMethodHint *) lfirst(l);

		if (bms_equal(joinrelids, hint->joinrelids))
			return hint;
	}

	return NULL;
}

/*
 * Transform join method hint into handy form.
 * 
 *   - create bitmap of relids from alias names, to make it easier to check
 *     whether a join path matches a join method hint.
 *   - add join method hints which are necessary to enforce join order
 *     specified by Leading hint
 */
static void
transform_join_hints(HintState *hstate, PlannerInfo *root, int nbaserel,
		List *initial_rels, JoinMethodHint **join_method_hints)
{
	int				i;
	int				relid;
	LeadingHint	   *lhint;
	Relids			joinrelids;
	int				njoinrels;
	ListCell	   *l;

	/*
	 * Create bitmap of relids from alias names for each join method hint.
	 * Bitmaps are more handy than strings in join searching.
	 */
	for (i = 0; i < hstate->num_hints[HINT_TYPE_JOIN_METHOD]; i++)
	{
		JoinMethodHint *hint = hstate->join_hints[i];
		int	j;

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
							  ("Relation name \"%s\" is duplicated.", relname));
				hint->base.state = HINT_STATE_ERROR;
				break;
			}

			hint->joinrelids = bms_add_member(hint->joinrelids, relid);
		}

		if (relid <= 0 || hint->base.state == HINT_STATE_ERROR)
			continue;

		hstate->join_hint_level[hint->nrels] =
			lappend(hstate->join_hint_level[hint->nrels], hint);
	}

	/* Do nothing if no Leading hint was supplied. */
	if (hstate->num_hints[HINT_TYPE_LEADING] == 0)
		return;

	/* Do nothing if Leading hint is invalid. */
	lhint = hstate->leading_hint;
	if (!hint_state_enabled(lhint))
		return;

	/*
	 * We need join method hints which fit specified join order in every join
	 * level.  For example, Leading(A B C) virtually requires following join
	 * method hints, if no join method hint supplied:
	 *   - level 1: none
	 *   - level 2: NestLoop(A B), MergeJoin(A B), HashJoin(A B)
	 *   - level 3: NestLoop(A B C), MergeJoin(A B C), HashJoin(A B C)
	 *
	 * If we already have join method hint which fits specified join order in
	 * that join level, we leave it as-is and don't add new hints.
	 */
	joinrelids = NULL;
	njoinrels = 0;
	foreach(l, lhint->relations)
	{
		char   *relname = (char *)lfirst(l);
		JoinMethodHint *hint;

		/*
		 * Find relid of the relation which has given name.  If we have the
		 * name given in Leading hint multiple times in the join, nothing to
		 * do.
		 */
		relid = find_relid_aliasname(root, relname, initial_rels,
									 hstate->hint_str);
		if (relid == -1)
		{
			bms_free(joinrelids);
			return;
		}
		if (relid == 0)
			continue;

		/* Found relid must not be in joinrelids. */
		if (bms_is_member(relid, joinrelids))
		{
			parse_ereport(lhint->base.hint_str,
						  ("Relation name \"%s\" is duplicated.", relname));
			lhint->base.state = HINT_STATE_ERROR;
			bms_free(joinrelids);
			return;
		}

		/* Create bitmap of relids for current join level. */
		joinrelids = bms_add_member(joinrelids, relid);
		njoinrels++;

		/* We never have join method hint for single relation. */
		if (njoinrels < 2)
			continue;

		/*
		 * If we don't have join method hint, create new one for the
		 * join combination with all join methods are enabled.
		 */
		hint = find_join_hint(joinrelids);
		if (hint == NULL)
		{
			/*
			 * Here relnames is not set, since Relids bitmap is sufficient to
			 * control paths of this query afterward.
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

	/*
	 * Delete all join hints which have different combination from Leading
	 * hint.
	 */
	for (i = 2; i <= njoinrels; i++)
	{
		list_free(hstate->join_hint_level[i]);

		hstate->join_hint_level[i] = lappend(NIL, join_method_hints[i]);
	}

	if (hint_state_enabled(lhint))
		set_join_config_options(DISABLE_ALL_JOIN, current_hint->context);

	lhint->base.state = HINT_STATE_USED;

}

/*
 * set_plain_rel_pathlist
 *	  Build access paths for a plain relation (no subquery, no inheritance)
 *
 * This function was copied and edited from set_plain_rel_pathlist() in
 * src/backend/optimizer/path/allpaths.c
 */
static void
set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	/* Consider sequential scan */
#if PG_VERSION_NUM >= 90200
	add_path(rel, create_seqscan_path(root, rel, NULL));
#else
	add_path(rel, create_seqscan_path(root, rel));
#endif

	/* Consider index scans */
	create_index_paths(root, rel);

	/* Consider TID scans */
	create_tidscan_paths(root, rel);

	/* Now find the cheapest of the paths for this rel */
	set_cheapest(rel);
}

static void
rebuild_scan_path(HintState *hstate, PlannerInfo *root, int level,
				  List *initial_rels)
{
	ListCell   *l;

	foreach(l, initial_rels)
	{
		RelOptInfo	   *rel = (RelOptInfo *) lfirst(l);
		RangeTblEntry  *rte;
		ScanMethodHint *hint;

		/* Skip relations which we can't choose scan method. */
		if (rel->reloptkind != RELOPT_BASEREL || rel->rtekind != RTE_RELATION)
			continue;

		rte = root->simple_rte_array[rel->relid];

		/* We can't force scan method of foreign tables */
		if (rte->relkind == RELKIND_FOREIGN_TABLE)
			continue;

		/*
		 * Create scan paths with GUC parameters which are at the beginning of
		 * planner if scan method hint is not specified, otherwise use
		 * specified hints and mark the hint as used.
		 */
		if ((hint = find_scan_hint(root, rel)) == NULL)
			set_scan_config_options(hstate->init_scan_mask,
									hstate->context);
		else
		{
			set_scan_config_options(hint->enforce_mask, hstate->context);
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
	set_scan_config_options(hstate->init_scan_mask, hstate->context);
}

/*
 * wrapper of make_join_rel()
 *
 * call make_join_rel() after changing enable_* parameters according to given
 * hints.
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

	set_join_config_options(hint->enforce_mask, current_hint->context);

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
pg_hint_plan_join_search(PlannerInfo *root, int levels_needed,
						 List *initial_rels)
{
	JoinMethodHint **join_method_hints;
	int			nbaserel;
	RelOptInfo *rel;
	int			i;

	/*
	 * Use standard planner (or geqo planner) if pg_hint_plan is disabled or no
	 * valid hint is supplied.
	 */
	if (!current_hint)
	{
		if (prev_join_search)
			return (*prev_join_search) (root, levels_needed, initial_rels);
		else if (enable_geqo && levels_needed >= geqo_threshold)
			return geqo(root, levels_needed, initial_rels);
		else
			return standard_join_search(root, levels_needed, initial_rels);
	}

	/* We apply scan method hint rebuild scan path. */
	rebuild_scan_path(current_hint, root, levels_needed, initial_rels);

	/*
	 * In the case using GEQO, only scan method hints and Set hints have
	 * effect.  Join method and join order is not controllable by hints.
	 */
	if (enable_geqo && levels_needed >= geqo_threshold)
		return geqo(root, levels_needed, initial_rels);

	nbaserel = get_num_baserels(initial_rels);
	current_hint->join_hint_level = palloc0(sizeof(List *) * (nbaserel + 1));
	join_method_hints = palloc0(sizeof(JoinMethodHint *) * (nbaserel + 1));

	transform_join_hints(current_hint, root, nbaserel, initial_rels,
						 join_method_hints);

	rel = pg_hint_plan_standard_join_search(root, levels_needed, initial_rels);

	for (i = 2; i <= nbaserel; i++)
	{
		list_free(current_hint->join_hint_level[i]);

		/* free Leading hint only */
		if (join_method_hints[i] != NULL &&
			join_method_hints[i]->enforce_mask == ENABLE_ALL_JOIN)
			JoinMethodHintDelete(join_method_hints[i]);
	}
	pfree(current_hint->join_hint_level);
	pfree(join_method_hints);

	if (current_hint->num_hints[HINT_TYPE_LEADING] > 0 &&
		hint_state_enabled(current_hint->leading_hint))
		set_join_config_options(current_hint->init_join_mask,
								current_hint->context);

	return rel;
}

/*
 * set_rel_pathlist
 *	  Build access paths for a base relation
 *
 * This function was copied and edited from set_rel_pathlist() in
 * src/backend/optimizer/path/allpaths.c
 */
static void
set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
				 Index rti, RangeTblEntry *rte)
{
#if PG_VERSION_NUM >= 90200
	if (IS_DUMMY_REL(rel))
	{
		/* We already proved the relation empty, so nothing more to do */
	}
	else if (rte->inh)
#else
	if (rte->inh)
#endif
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
		set_scan_config_options(hint->enforce_mask, current_hint->context); \
		hint->base.state = HINT_STATE_USED; \
	} \
	add_paths_to_joinrel((root), (joinrel), (outerrel), (innerrel), (jointype), (sjinfo), (restrictlist)); \
	if (hint != NULL) \
		set_scan_config_options(current_hint->init_scan_mask, current_hint->context); \
} while(0)
#include "make_join_rel.c"

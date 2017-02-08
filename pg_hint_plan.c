/*-------------------------------------------------------------------------
 *
 * pg_hint_plan.c
 *		  hinting on how to execute a query for PostgreSQL
 *
 * Copyright (c) 2012-2017, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */
#include <string.h>

#include "postgres.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_index.h"
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
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "utils/resowner.h"

#include "catalog/pg_class.h"

#include "executor/spi.h"
#include "catalog/pg_type.h"

#include "plpgsql.h"

/* partially copied from pg_stat_statements */
#include "normalize_query.h"

/* PostgreSQL */
#include "access/htup_details.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define BLOCK_COMMENT_START		"/*"
#define BLOCK_COMMENT_END		"*/"
#define HINT_COMMENT_KEYWORD	"+"
#define HINT_START				BLOCK_COMMENT_START HINT_COMMENT_KEYWORD
#define HINT_END				BLOCK_COMMENT_END

/* hint keywords */
#define HINT_SEQSCAN			"SeqScan"
#define HINT_INDEXSCAN			"IndexScan"
#define HINT_INDEXSCANREGEXP	"IndexScanRegexp"
#define HINT_BITMAPSCAN			"BitmapScan"
#define HINT_BITMAPSCANREGEXP	"BitmapScanRegexp"
#define HINT_TIDSCAN			"TidScan"
#define HINT_NOSEQSCAN			"NoSeqScan"
#define HINT_NOINDEXSCAN		"NoIndexScan"
#define HINT_NOBITMAPSCAN		"NoBitmapScan"
#define HINT_NOTIDSCAN			"NoTidScan"
#define HINT_INDEXONLYSCAN		"IndexOnlyScan"
#define HINT_INDEXONLYSCANREGEXP	"IndexOnlyScanRegexp"
#define HINT_NOINDEXONLYSCAN	"NoIndexOnlyScan"
#define HINT_PARALLEL			"Parallel"

#define HINT_NESTLOOP			"NestLoop"
#define HINT_MERGEJOIN			"MergeJoin"
#define HINT_HASHJOIN			"HashJoin"
#define HINT_NONESTLOOP			"NoNestLoop"
#define HINT_NOMERGEJOIN		"NoMergeJoin"
#define HINT_NOHASHJOIN			"NoHashJoin"
#define HINT_LEADING			"Leading"
#define HINT_SET				"Set"
#define HINT_ROWS				"Rows"

#define HINT_ARRAY_DEFAULT_INITSIZE 8

#define hint_ereport(str, detail) \
	ereport(pg_hint_plan_message_level, \
			(errhidestmt(hidestmt),	\
			 errmsg("pg_hint_plan%s: hint syntax error at or near \"%s\"", qnostr, (str)), \
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
	ENABLE_INDEXONLYSCAN = 0x10
} SCAN_TYPE_BITS;

enum
{
	ENABLE_NESTLOOP = 0x01,
	ENABLE_MERGEJOIN = 0x02,
	ENABLE_HASHJOIN = 0x04
} JOIN_TYPE_BITS;

#define ENABLE_ALL_SCAN (ENABLE_SEQSCAN | ENABLE_INDEXSCAN | \
						 ENABLE_BITMAPSCAN | ENABLE_TIDSCAN | \
						 ENABLE_INDEXONLYSCAN)
#define ENABLE_ALL_JOIN (ENABLE_NESTLOOP | ENABLE_MERGEJOIN | ENABLE_HASHJOIN)
#define DISABLE_ALL_SCAN 0
#define DISABLE_ALL_JOIN 0

/* hint keyword of enum type*/
typedef enum HintKeyword
{
	HINT_KEYWORD_SEQSCAN,
	HINT_KEYWORD_INDEXSCAN,
	HINT_KEYWORD_INDEXSCANREGEXP,
	HINT_KEYWORD_BITMAPSCAN,
	HINT_KEYWORD_BITMAPSCANREGEXP,
	HINT_KEYWORD_TIDSCAN,
	HINT_KEYWORD_NOSEQSCAN,
	HINT_KEYWORD_NOINDEXSCAN,
	HINT_KEYWORD_NOBITMAPSCAN,
	HINT_KEYWORD_NOTIDSCAN,
	HINT_KEYWORD_INDEXONLYSCAN,
	HINT_KEYWORD_INDEXONLYSCANREGEXP,
	HINT_KEYWORD_NOINDEXONLYSCAN,

	HINT_KEYWORD_NESTLOOP,
	HINT_KEYWORD_MERGEJOIN,
	HINT_KEYWORD_HASHJOIN,
	HINT_KEYWORD_NONESTLOOP,
	HINT_KEYWORD_NOMERGEJOIN,
	HINT_KEYWORD_NOHASHJOIN,

	HINT_KEYWORD_LEADING,
	HINT_KEYWORD_SET,
	HINT_KEYWORD_ROWS,
	HINT_KEYWORD_PARALLEL,

	HINT_KEYWORD_UNRECOGNIZED
} HintKeyword;

#define SCAN_HINT_ACCEPTS_INDEX_NAMES(kw) \
	(kw == HINT_KEYWORD_INDEXSCAN ||			\
	 kw == HINT_KEYWORD_INDEXSCANREGEXP ||		\
	 kw == HINT_KEYWORD_INDEXONLYSCAN ||		\
	 kw == HINT_KEYWORD_INDEXONLYSCANREGEXP ||	\
	 kw == HINT_KEYWORD_BITMAPSCAN ||				\
	 kw == HINT_KEYWORD_BITMAPSCANREGEXP)


typedef struct Hint Hint;
typedef struct HintState HintState;

typedef Hint *(*HintCreateFunction) (const char *hint_str,
									 const char *keyword,
									 HintKeyword hint_keyword);
typedef void (*HintDeleteFunction) (Hint *hint);
typedef void (*HintDescFunction) (Hint *hint, StringInfo buf, bool nolf);
typedef int (*HintCmpFunction) (const Hint *a, const Hint *b);
typedef const char *(*HintParseFunction) (Hint *hint, HintState *hstate,
										  Query *parse, const char *str);

/* hint types */
#define NUM_HINT_TYPE	6
typedef enum HintType
{
	HINT_TYPE_SCAN_METHOD,
	HINT_TYPE_JOIN_METHOD,
	HINT_TYPE_LEADING,
	HINT_TYPE_SET,
	HINT_TYPE_ROWS,
	HINT_TYPE_PARALLEL
} HintType;

static const char *HintTypeName[] = {
	"scan method",
	"join method",
	"leading",
	"set",
	"rows",
	"parallel"
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

static unsigned int qno = 0;
static char qnostr[32];

/* common data for all hints. */
struct Hint
{
	const char		   *hint_str;		/* must not do pfree */
	const char		   *keyword;		/* must not do pfree */
	HintKeyword			hint_keyword;
	HintType			type;
	HintStatus			state;
	HintDeleteFunction	delete_func;
	HintDescFunction	desc_func;
	HintCmpFunction		cmp_func;
	HintParseFunction	parse_func;
};

/* scan method hints */
typedef struct ScanMethodHint
{
	Hint			base;
	char		   *relname;
	List		   *indexnames;
	bool			regexp;
	unsigned char	enforce_mask;
} ScanMethodHint;

typedef struct ParentIndexInfo
{
	bool		indisunique;
	Oid			method;
	List	   *column_names;
	char	   *expression_str;
	Oid		   *indcollation;
	Oid		   *opclass;
	int16	   *indoption;
	char	   *indpred_str;
} ParentIndexInfo;

/* join method hints */
typedef struct JoinMethodHint
{
	Hint			base;
	int				nrels;
	int				inner_nrels;
	char		  **relnames;
	unsigned char	enforce_mask;
	Relids			joinrelids;
	Relids			inner_joinrelids;
} JoinMethodHint;

/* join order hints */
typedef struct OuterInnerRels
{
	char   *relation;
	List   *outer_inner_pair;
} OuterInnerRels;

typedef struct LeadingHint
{
	Hint			base;
	List		   *relations;	/* relation names specified in Leading hint */
	OuterInnerRels *outer_inner;
} LeadingHint;

/* change a run-time parameter hints */
typedef struct SetHint
{
	Hint	base;
	char   *name;				/* name of variable */
	char   *value;
	List   *words;
} SetHint;

/* rows hints */
typedef enum RowsValueType {
	RVT_ABSOLUTE,		/* Rows(... #1000) */
	RVT_ADD,			/* Rows(... +1000) */
	RVT_SUB,			/* Rows(... -1000) */
	RVT_MULTI,			/* Rows(... *1.2) */
} RowsValueType;
typedef struct RowsHint
{
	Hint			base;
	int				nrels;
	int				inner_nrels;
	char		  **relnames;
	Relids			joinrelids;
	Relids			inner_joinrelids;
	char		   *rows_str;
	RowsValueType	value_type;
	double			rows;
} RowsHint;

/* parallel hints */
typedef struct ParallelHint
{
	Hint			base;
	char		   *relname;
	char		   *nworkers_str;	/* original string of nworkers */
	int				nworkers;		/* num of workers specified by Worker */
	bool			force_parallel;	/* force parallel scan */
} ParallelHint;

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

	/* Initial values of parameters  */
	int				init_scan_mask;		/* enable_* mask */
	int				init_nworkers;		/* max_parallel_workers_per_gather */
	int				init_min_para_size;	/* min_parallel_relation_size*/
	int				init_paratup_cost;	/* parallel_tuple_cost */
	int				init_parasetup_cost;/* parallel_setup_cost */

	Index			parent_relid;		/* inherit parent of table relid */
	ScanMethodHint *parent_scan_hint;	/* scan hint for the parent */
	ParallelHint   *parent_parallel_hint; /* parallel hint for the parent */
	List		   *parent_index_infos; /* list of parent table's index */

	JoinMethodHint **join_hints;		/* parsed join hints */
	int				init_join_mask;		/* initial value join parameter */
	List		  **join_hint_level;
	LeadingHint	  **leading_hint;		/* parsed Leading hints */
	SetHint		  **set_hints;			/* parsed Set hints */
	GucContext		context;			/* which GUC parameters can we set? */
	RowsHint	  **rows_hints;			/* parsed Rows hints */
	ParallelHint  **parallel_hints;		/* parsed Parallel hints */
};

/*
 * Describes a hint parser module which is bound with particular hint keyword.
 */
typedef struct HintParser
{
	char			   *keyword;
	HintCreateFunction	create_func;
	HintKeyword			hint_keyword;
} HintParser;

/* Module callbacks */
void		_PG_init(void);
void		_PG_fini(void);

static void push_hint(HintState *hstate);
static void pop_hint(void);

static void pg_hint_plan_ProcessUtility(Node *parsetree,
							const char *queryString,
							ProcessUtilityContext context,
							ParamListInfo params,
							DestReceiver *dest, char *completionTag);
static PlannedStmt *pg_hint_plan_planner(Query *parse, int cursorOptions,
										 ParamListInfo boundParams);
static void pg_hint_plan_get_relation_info(PlannerInfo *root,
										   Oid relationObjectId,
										   bool inhparent, RelOptInfo *rel);
static RelOptInfo *pg_hint_plan_join_search(PlannerInfo *root,
											int levels_needed,
											List *initial_rels);

/* Scan method hint callbacks */
static Hint *ScanMethodHintCreate(const char *hint_str, const char *keyword,
								  HintKeyword hint_keyword);
static void ScanMethodHintDelete(ScanMethodHint *hint);
static void ScanMethodHintDesc(ScanMethodHint *hint, StringInfo buf, bool nolf);
static int ScanMethodHintCmp(const ScanMethodHint *a, const ScanMethodHint *b);
static const char *ScanMethodHintParse(ScanMethodHint *hint, HintState *hstate,
									   Query *parse, const char *str);

/* Join method hint callbacks */
static Hint *JoinMethodHintCreate(const char *hint_str, const char *keyword,
								  HintKeyword hint_keyword);
static void JoinMethodHintDelete(JoinMethodHint *hint);
static void JoinMethodHintDesc(JoinMethodHint *hint, StringInfo buf, bool nolf);
static int JoinMethodHintCmp(const JoinMethodHint *a, const JoinMethodHint *b);
static const char *JoinMethodHintParse(JoinMethodHint *hint, HintState *hstate,
									   Query *parse, const char *str);

/* Leading hint callbacks */
static Hint *LeadingHintCreate(const char *hint_str, const char *keyword,
							   HintKeyword hint_keyword);
static void LeadingHintDelete(LeadingHint *hint);
static void LeadingHintDesc(LeadingHint *hint, StringInfo buf, bool nolf);
static int LeadingHintCmp(const LeadingHint *a, const LeadingHint *b);
static const char *LeadingHintParse(LeadingHint *hint, HintState *hstate,
									Query *parse, const char *str);

/* Set hint callbacks */
static Hint *SetHintCreate(const char *hint_str, const char *keyword,
						   HintKeyword hint_keyword);
static void SetHintDelete(SetHint *hint);
static void SetHintDesc(SetHint *hint, StringInfo buf, bool nolf);
static int SetHintCmp(const SetHint *a, const SetHint *b);
static const char *SetHintParse(SetHint *hint, HintState *hstate, Query *parse,
								const char *str);

/* Rows hint callbacks */
static Hint *RowsHintCreate(const char *hint_str, const char *keyword,
							HintKeyword hint_keyword);
static void RowsHintDelete(RowsHint *hint);
static void RowsHintDesc(RowsHint *hint, StringInfo buf, bool nolf);
static int RowsHintCmp(const RowsHint *a, const RowsHint *b);
static const char *RowsHintParse(RowsHint *hint, HintState *hstate,
								 Query *parse, const char *str);

/* Parallel hint callbacks */
static Hint *ParallelHintCreate(const char *hint_str, const char *keyword,
								HintKeyword hint_keyword);
static void ParallelHintDelete(ParallelHint *hint);
static void ParallelHintDesc(ParallelHint *hint, StringInfo buf, bool nolf);
static int ParallelHintCmp(const ParallelHint *a, const ParallelHint *b);
static const char *ParallelHintParse(ParallelHint *hint, HintState *hstate,
									 Query *parse, const char *str);

static void quote_value(StringInfo buf, const char *value);

static const char *parse_quoted_value(const char *str, char **word,
									  bool truncate);

RelOptInfo *pg_hint_plan_standard_join_search(PlannerInfo *root,
											  int levels_needed,
											  List *initial_rels);
void pg_hint_plan_join_search_one_level(PlannerInfo *root, int level);
void pg_hint_plan_set_rel_pathlist(PlannerInfo * root, RelOptInfo *rel,
								   Index rti, RangeTblEntry *rte);
static void create_plain_partial_paths(PlannerInfo *root,
													RelOptInfo *rel);
static int compute_parallel_worker(RelOptInfo *rel, BlockNumber pages);

static void make_rels_by_clause_joins(PlannerInfo *root, RelOptInfo *old_rel,
									  ListCell *other_rels);
static void make_rels_by_clauseless_joins(PlannerInfo *root,
										  RelOptInfo *old_rel,
										  ListCell *other_rels);
static bool has_join_restriction(PlannerInfo *root, RelOptInfo *rel);
static void set_append_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
									Index rti, RangeTblEntry *rte);
static void generate_mergeappend_paths(PlannerInfo *root, RelOptInfo *rel,
						   List *live_childrels,
						   List *all_child_pathkeys);
static Path *get_cheapest_parameterized_child_path(PlannerInfo *root,
									  RelOptInfo *rel,
									  Relids required_outer);
static List *accumulate_append_subpath(List *subpaths, Path *path);
RelOptInfo *pg_hint_plan_make_join_rel(PlannerInfo *root, RelOptInfo *rel1,
									   RelOptInfo *rel2);

static void pg_hint_plan_plpgsql_stmt_beg(PLpgSQL_execstate *estate,
										  PLpgSQL_stmt *stmt);
static void pg_hint_plan_plpgsql_stmt_end(PLpgSQL_execstate *estate,
										  PLpgSQL_stmt *stmt);
static void plpgsql_query_erase_callback(ResourceReleasePhase phase,
										 bool isCommit,
										 bool isTopLevel,
										 void *arg);
static int set_config_option_noerror(const char *name, const char *value,
						  GucContext context, GucSource source,
						  GucAction action, bool changeVal, int elevel);
static void setup_scan_method_enforcement(ScanMethodHint *scanhint,
										  HintState *state);
static int set_config_int32_option(const char *name, int32 value,
									GucContext context);

/* GUC variables */
static bool	pg_hint_plan_enable_hint = true;
static int debug_level = 0;
static int	pg_hint_plan_message_level = INFO;
/* Default is off, to keep backward compatibility. */
static bool	pg_hint_plan_enable_hint_table = false;

/* Internal static variables. */
static bool	hidestmt = false;				/* Allow or inhibit STATEMENT: output */

static int plpgsql_recurse_level = 0;		/* PLpgSQL recursion level            */
static int hint_inhibit_level = 0;			/* Inhibit hinting if this is above 0 */
											/* (This could not be above 1)        */
static int max_hint_nworkers = -1;		/* Maximum nworkers of Workers hints */

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

static const struct config_enum_entry parse_debug_level_options[] = {
	{"off", 0, false},
	{"on", 1, false},
	{"detailed", 2, false},
	{"verbose", 3, false},
	{"0", 0, true},
	{"1", 1, true},
	{"2", 2, true},
	{"3", 3, true},
	{"no", 0, true},
	{"yes", 1, true},
	{"false", 0, true},
	{"true", 1, true},
	{NULL, 0, false}
};

/* Saved hook values in case of unload */
static ProcessUtility_hook_type prev_ProcessUtility = NULL;
static planner_hook_type prev_planner = NULL;
static get_relation_info_hook_type prev_get_relation_info = NULL;
static join_search_hook_type prev_join_search = NULL;
static set_rel_pathlist_hook_type prev_set_rel_pathlist = NULL;

/* Hold reference to currently active hint */
static HintState *current_hint_state = NULL;

/*
 * List of hint contexts.  We treat the head of the list as the Top of the
 * context stack, so current_hint_state always points the first element of this
 * list.
 */
static List *HintStateStack = NIL;

/*
 * Holds statement name during executing EXECUTE command.  NULL for other
 * statements.
 */
static char	   *stmt_name = NULL;

static const HintParser parsers[] = {
	{HINT_SEQSCAN, ScanMethodHintCreate, HINT_KEYWORD_SEQSCAN},
	{HINT_INDEXSCAN, ScanMethodHintCreate, HINT_KEYWORD_INDEXSCAN},
	{HINT_INDEXSCANREGEXP, ScanMethodHintCreate, HINT_KEYWORD_INDEXSCANREGEXP},
	{HINT_BITMAPSCAN, ScanMethodHintCreate, HINT_KEYWORD_BITMAPSCAN},
	{HINT_BITMAPSCANREGEXP, ScanMethodHintCreate,
	 HINT_KEYWORD_BITMAPSCANREGEXP},
	{HINT_TIDSCAN, ScanMethodHintCreate, HINT_KEYWORD_TIDSCAN},
	{HINT_NOSEQSCAN, ScanMethodHintCreate, HINT_KEYWORD_NOSEQSCAN},
	{HINT_NOINDEXSCAN, ScanMethodHintCreate, HINT_KEYWORD_NOINDEXSCAN},
	{HINT_NOBITMAPSCAN, ScanMethodHintCreate, HINT_KEYWORD_NOBITMAPSCAN},
	{HINT_NOTIDSCAN, ScanMethodHintCreate, HINT_KEYWORD_NOTIDSCAN},
	{HINT_INDEXONLYSCAN, ScanMethodHintCreate, HINT_KEYWORD_INDEXONLYSCAN},
	{HINT_INDEXONLYSCANREGEXP, ScanMethodHintCreate,
	 HINT_KEYWORD_INDEXONLYSCANREGEXP},
	{HINT_NOINDEXONLYSCAN, ScanMethodHintCreate, HINT_KEYWORD_NOINDEXONLYSCAN},

	{HINT_NESTLOOP, JoinMethodHintCreate, HINT_KEYWORD_NESTLOOP},
	{HINT_MERGEJOIN, JoinMethodHintCreate, HINT_KEYWORD_MERGEJOIN},
	{HINT_HASHJOIN, JoinMethodHintCreate, HINT_KEYWORD_HASHJOIN},
	{HINT_NONESTLOOP, JoinMethodHintCreate, HINT_KEYWORD_NONESTLOOP},
	{HINT_NOMERGEJOIN, JoinMethodHintCreate, HINT_KEYWORD_NOMERGEJOIN},
	{HINT_NOHASHJOIN, JoinMethodHintCreate, HINT_KEYWORD_NOHASHJOIN},

	{HINT_LEADING, LeadingHintCreate, HINT_KEYWORD_LEADING},
	{HINT_SET, SetHintCreate, HINT_KEYWORD_SET},
	{HINT_ROWS, RowsHintCreate, HINT_KEYWORD_ROWS},
	{HINT_PARALLEL, ParallelHintCreate, HINT_KEYWORD_PARALLEL},

	{NULL, NULL, HINT_KEYWORD_UNRECOGNIZED}
};

PLpgSQL_plugin  plugin_funcs = {
	NULL,
	NULL,
	NULL,
	pg_hint_plan_plpgsql_stmt_beg,
	pg_hint_plan_plpgsql_stmt_end,
	NULL,
	NULL,
};

/*
 * Module load callbacks
 */
void
_PG_init(void)
{
	PLpgSQL_plugin	**var_ptr;

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

	DefineCustomEnumVariable("pg_hint_plan.debug_print",
							 "Logs results of hint parsing.",
							 NULL,
							 &debug_level,
							 false,
							 parse_debug_level_options,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomEnumVariable("pg_hint_plan.parse_messages",
							 "Message level of parse errors.",
							 NULL,
							 &pg_hint_plan_message_level,
							 INFO,
							 parse_messages_level_options,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomEnumVariable("pg_hint_plan.message_level",
							 "Message level of debug messages.",
							 NULL,
							 &pg_hint_plan_message_level,
							 INFO,
							 parse_messages_level_options,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("pg_hint_plan.enable_hint_table",
							 "Let pg_hint_plan look up the hint table.",
							 NULL,
							 &pg_hint_plan_enable_hint_table,
							 false,
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
	prev_set_rel_pathlist = set_rel_pathlist_hook;
	set_rel_pathlist_hook = pg_hint_plan_set_rel_pathlist;

	/* setup PL/pgSQL plugin hook */
	var_ptr = (PLpgSQL_plugin **) find_rendezvous_variable("PLpgSQL_plugin");
	*var_ptr = &plugin_funcs;

	RegisterResourceReleaseCallback(plpgsql_query_erase_callback, NULL);
}

/*
 * Module unload callback
 * XXX never called
 */
void
_PG_fini(void)
{
	PLpgSQL_plugin	**var_ptr;

	/* Uninstall hooks. */
	ProcessUtility_hook = prev_ProcessUtility;
	planner_hook = prev_planner;
	get_relation_info_hook = prev_get_relation_info;
	join_search_hook = prev_join_search;
	set_rel_pathlist_hook = prev_set_rel_pathlist;

	/* uninstall PL/pgSQL plugin hook */
	var_ptr = (PLpgSQL_plugin **) find_rendezvous_variable("PLpgSQL_plugin");
	*var_ptr = NULL;
}

/*
 * create and delete functions the hint object
 */

static Hint *
ScanMethodHintCreate(const char *hint_str, const char *keyword,
					 HintKeyword hint_keyword)
{
	ScanMethodHint *hint;

	hint = palloc(sizeof(ScanMethodHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.hint_keyword = hint_keyword;
	hint->base.type = HINT_TYPE_SCAN_METHOD;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) ScanMethodHintDelete;
	hint->base.desc_func = (HintDescFunction) ScanMethodHintDesc;
	hint->base.cmp_func = (HintCmpFunction) ScanMethodHintCmp;
	hint->base.parse_func = (HintParseFunction) ScanMethodHintParse;
	hint->relname = NULL;
	hint->indexnames = NIL;
	hint->regexp = false;
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
JoinMethodHintCreate(const char *hint_str, const char *keyword,
					 HintKeyword hint_keyword)
{
	JoinMethodHint *hint;

	hint = palloc(sizeof(JoinMethodHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.hint_keyword = hint_keyword;
	hint->base.type = HINT_TYPE_JOIN_METHOD;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) JoinMethodHintDelete;
	hint->base.desc_func = (HintDescFunction) JoinMethodHintDesc;
	hint->base.cmp_func = (HintCmpFunction) JoinMethodHintCmp;
	hint->base.parse_func = (HintParseFunction) JoinMethodHintParse;
	hint->nrels = 0;
	hint->inner_nrels = 0;
	hint->relnames = NULL;
	hint->enforce_mask = 0;
	hint->joinrelids = NULL;
	hint->inner_joinrelids = NULL;

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
	bms_free(hint->inner_joinrelids);
	pfree(hint);
}

static Hint *
LeadingHintCreate(const char *hint_str, const char *keyword,
				  HintKeyword hint_keyword)
{
	LeadingHint	   *hint;

	hint = palloc(sizeof(LeadingHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.hint_keyword = hint_keyword;
	hint->base.type = HINT_TYPE_LEADING;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction)LeadingHintDelete;
	hint->base.desc_func = (HintDescFunction) LeadingHintDesc;
	hint->base.cmp_func = (HintCmpFunction) LeadingHintCmp;
	hint->base.parse_func = (HintParseFunction) LeadingHintParse;
	hint->relations = NIL;
	hint->outer_inner = NULL;

	return (Hint *) hint;
}

static void
LeadingHintDelete(LeadingHint *hint)
{
	if (!hint)
		return;

	list_free_deep(hint->relations);
	if (hint->outer_inner)
		pfree(hint->outer_inner);
	pfree(hint);
}

static Hint *
SetHintCreate(const char *hint_str, const char *keyword,
			  HintKeyword hint_keyword)
{
	SetHint	   *hint;

	hint = palloc(sizeof(SetHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.hint_keyword = hint_keyword;
	hint->base.type = HINT_TYPE_SET;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) SetHintDelete;
	hint->base.desc_func = (HintDescFunction) SetHintDesc;
	hint->base.cmp_func = (HintCmpFunction) SetHintCmp;
	hint->base.parse_func = (HintParseFunction) SetHintParse;
	hint->name = NULL;
	hint->value = NULL;
	hint->words = NIL;

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
	if (hint->words)
		list_free(hint->words);
	pfree(hint);
}

static Hint *
RowsHintCreate(const char *hint_str, const char *keyword,
			   HintKeyword hint_keyword)
{
	RowsHint *hint;

	hint = palloc(sizeof(RowsHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.hint_keyword = hint_keyword;
	hint->base.type = HINT_TYPE_ROWS;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) RowsHintDelete;
	hint->base.desc_func = (HintDescFunction) RowsHintDesc;
	hint->base.cmp_func = (HintCmpFunction) RowsHintCmp;
	hint->base.parse_func = (HintParseFunction) RowsHintParse;
	hint->nrels = 0;
	hint->inner_nrels = 0;
	hint->relnames = NULL;
	hint->joinrelids = NULL;
	hint->inner_joinrelids = NULL;
	hint->rows_str = NULL;
	hint->value_type = RVT_ABSOLUTE;
	hint->rows = 0;

	return (Hint *) hint;
}

static void
RowsHintDelete(RowsHint *hint)
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
	bms_free(hint->inner_joinrelids);
	pfree(hint);
}

static Hint *
ParallelHintCreate(const char *hint_str, const char *keyword,
				  HintKeyword hint_keyword)
{
	ParallelHint *hint;

	hint = palloc(sizeof(ScanMethodHint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.hint_keyword = hint_keyword;
	hint->base.type = HINT_TYPE_PARALLEL;
	hint->base.state = HINT_STATE_NOTUSED;
	hint->base.delete_func = (HintDeleteFunction) ParallelHintDelete;
	hint->base.desc_func = (HintDescFunction) ParallelHintDesc;
	hint->base.cmp_func = (HintCmpFunction) ParallelHintCmp;
	hint->base.parse_func = (HintParseFunction) ParallelHintParse;
	hint->relname = NULL;
	hint->nworkers = 0;
	hint->nworkers_str = "0";

	return (Hint *) hint;
}

static void
ParallelHintDelete(ParallelHint *hint)
{
	if (!hint)
		return;

	if (hint->relname)
		pfree(hint->relname);
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
	hstate->init_nworkers = 0;
	hstate->init_min_para_size = 0;
	hstate->init_paratup_cost = 0;
	hstate->init_parasetup_cost = 0;
	hstate->parent_relid = 0;
	hstate->parent_scan_hint = NULL;
	hstate->parent_parallel_hint = NULL;
	hstate->parent_index_infos = NIL;
	hstate->join_hints = NULL;
	hstate->init_join_mask = 0;
	hstate->join_hint_level = NULL;
	hstate->leading_hint = NULL;
	hstate->context = superuser() ? PGC_SUSET : PGC_USERSET;
	hstate->set_hints = NULL;
	hstate->rows_hints = NULL;
	hstate->parallel_hints = NULL;

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
	if (hstate->parent_index_infos)
		list_free(hstate->parent_index_infos);
}

/*
 * Copy given value into buf, with quoting with '"' if necessary.
 */
static void
quote_value(StringInfo buf, const char *value)
{
	bool		need_quote = false;
	const char *str;

	for (str = value; *str != '\0'; str++)
	{
		if (isspace(*str) || *str == '(' || *str == ')' || *str == '"')
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
ScanMethodHintDesc(ScanMethodHint *hint, StringInfo buf, bool nolf)
{
	ListCell   *l;

	appendStringInfo(buf, "%s(", hint->base.keyword);
	if (hint->relname != NULL)
	{
		quote_value(buf, hint->relname);
		foreach(l, hint->indexnames)
		{
			appendStringInfoCharMacro(buf, ' ');
			quote_value(buf, (char *) lfirst(l));
		}
	}
	appendStringInfoString(buf, ")");
	if (!nolf)
		appendStringInfoChar(buf, '\n');
}

static void
JoinMethodHintDesc(JoinMethodHint *hint, StringInfo buf, bool nolf)
{
	int	i;

	appendStringInfo(buf, "%s(", hint->base.keyword);
	if (hint->relnames != NULL)
	{
		quote_value(buf, hint->relnames[0]);
		for (i = 1; i < hint->nrels; i++)
		{
			appendStringInfoCharMacro(buf, ' ');
			quote_value(buf, hint->relnames[i]);
		}
	}
	appendStringInfoString(buf, ")");
	if (!nolf)
		appendStringInfoChar(buf, '\n');
}

static void
OuterInnerDesc(OuterInnerRels *outer_inner, StringInfo buf)
{
	if (outer_inner->relation == NULL)
	{
		bool		is_first;
		ListCell   *l;

		is_first = true;

		appendStringInfoCharMacro(buf, '(');
		foreach(l, outer_inner->outer_inner_pair)
		{
			if (is_first)
				is_first = false;
			else
				appendStringInfoCharMacro(buf, ' ');

			OuterInnerDesc(lfirst(l), buf);
		}

		appendStringInfoCharMacro(buf, ')');
	}
	else
		quote_value(buf, outer_inner->relation);
}

static void
LeadingHintDesc(LeadingHint *hint, StringInfo buf, bool nolf)
{
	appendStringInfo(buf, "%s(", HINT_LEADING);
	if (hint->outer_inner == NULL)
	{
		ListCell   *l;
		bool		is_first;

		is_first = true;

		foreach(l, hint->relations)
		{
			if (is_first)
				is_first = false;
			else
				appendStringInfoCharMacro(buf, ' ');

			quote_value(buf, (char *) lfirst(l));
		}
	}
	else
		OuterInnerDesc(hint->outer_inner, buf);

	appendStringInfoString(buf, ")");
	if (!nolf)
		appendStringInfoChar(buf, '\n');
}

static void
SetHintDesc(SetHint *hint, StringInfo buf, bool nolf)
{
	bool		is_first = true;
	ListCell   *l;

	appendStringInfo(buf, "%s(", HINT_SET);
	foreach(l, hint->words)
	{
		if (is_first)
			is_first = false;
		else
			appendStringInfoCharMacro(buf, ' ');

		quote_value(buf, (char *) lfirst(l));
	}
	appendStringInfo(buf, ")");
	if (!nolf)
		appendStringInfoChar(buf, '\n');
}

static void
RowsHintDesc(RowsHint *hint, StringInfo buf, bool nolf)
{
	int	i;

	appendStringInfo(buf, "%s(", hint->base.keyword);
	if (hint->relnames != NULL)
	{
		quote_value(buf, hint->relnames[0]);
		for (i = 1; i < hint->nrels; i++)
		{
			appendStringInfoCharMacro(buf, ' ');
			quote_value(buf, hint->relnames[i]);
		}
	}
	appendStringInfo(buf, " %s", hint->rows_str);
	appendStringInfoString(buf, ")");
	if (!nolf)
		appendStringInfoChar(buf, '\n');
}

static void
ParallelHintDesc(ParallelHint *hint, StringInfo buf, bool nolf)
{
	appendStringInfo(buf, "%s(", hint->base.keyword);
	if (hint->relname != NULL)
	{
		quote_value(buf, hint->relname);

		/* number of workers  */
		appendStringInfoCharMacro(buf, ' ');
		quote_value(buf, hint->nworkers_str);
		/* application mode of num of workers */
		appendStringInfoCharMacro(buf, ' ');
		appendStringInfoString(buf,
							   (hint->force_parallel ? "hard" : "soft"));
	}
	appendStringInfoString(buf, ")");
	if (!nolf)
		appendStringInfoChar(buf, '\n');
}

/*
 * Append string which represents all hints in a given state to buf, with
 * preceding title with them.
 */
static void
desc_hint_in_state(HintState *hstate, StringInfo buf, const char *title,
				   HintStatus state, bool nolf)
{
	int	i, nshown;

	appendStringInfo(buf, "%s:", title);
	if (!nolf)
		appendStringInfoChar(buf, '\n');

	nshown = 0;
	for (i = 0; i < hstate->nall_hints; i++)
	{
		if (hstate->all_hints[i]->state != state)
			continue;

		hstate->all_hints[i]->desc_func(hstate->all_hints[i], buf, nolf);
		nshown++;
	}

	if (nolf && nshown == 0)
		appendStringInfoString(buf, "(none)");
}

/*
 * Dump contents of given hstate to server log with log level LOG.
 */
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
	desc_hint_in_state(hstate, &buf, "used hint", HINT_STATE_USED, false);
	desc_hint_in_state(hstate, &buf, "not used hint", HINT_STATE_NOTUSED, false);
	desc_hint_in_state(hstate, &buf, "duplication hint", HINT_STATE_DUPLICATION, false);
	desc_hint_in_state(hstate, &buf, "error hint", HINT_STATE_ERROR, false);

	elog(LOG, "%s", buf.data);

	pfree(buf.data);
}

static void
HintStateDump2(HintState *hstate)
{
	StringInfoData	buf;

	if (!hstate)
	{
		elog(pg_hint_plan_message_level,
			 "pg_hint_plan%s: HintStateDump: no hint", qnostr);
		return;
	}

	initStringInfo(&buf);
	appendStringInfo(&buf, "pg_hint_plan%s: HintStateDump: ", qnostr);
	desc_hint_in_state(hstate, &buf, "{used hints", HINT_STATE_USED, true);
	desc_hint_in_state(hstate, &buf, "}, {not used hints", HINT_STATE_NOTUSED, true);
	desc_hint_in_state(hstate, &buf, "}, {duplicate hints", HINT_STATE_DUPLICATION, true);
	desc_hint_in_state(hstate, &buf, "}, {error hints", HINT_STATE_ERROR, true);
	appendStringInfoChar(&buf, '}');

	ereport(pg_hint_plan_message_level,
			(errhidestmt(true),
			 errmsg("%s", buf.data)));

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
RowsHintCmp(const RowsHint *a, const RowsHint *b)
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
ParallelHintCmp(const ParallelHint *a, const ParallelHint *b)
{
	return RelnameCmp(&a->relname, &b->relname);
}

static int
HintCmp(const void *a, const void *b)
{
	const Hint *hinta = *((const Hint **) a);
	const Hint *hintb = *((const Hint **) b);

	if (hinta->type != hintb->type)
		return hinta->type - hintb->type;
	if (hinta->state == HINT_STATE_ERROR)
		return -1;
	if (hintb->state == HINT_STATE_ERROR)
		return 1;
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
skip_parenthesis(const char *str, char parenthesis)
{
	skip_space(str);

	if (*str != parenthesis)
	{
		if (parenthesis == '(')
			hint_ereport(str, ("Opening parenthesis is necessary."));
		else if (parenthesis == ')')
			hint_ereport(str, ("Closing parenthesis is necessary."));

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
parse_quoted_value(const char *str, char **word, bool truncate)
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
				hint_ereport(str, ("Unterminated quoted string."));
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
		else if (isspace(*str) || *str == '(' || *str == ')' || *str == '"' ||
				 *str == '\0')
			break;

		appendStringInfoCharMacro(&buf, *str++);
	}

	if (buf.len == 0)
	{
		hint_ereport(str, ("Zero-length delimited string."));

		pfree(buf.data);

		return NULL;
	}

	/* Truncate name if it's too long */
	if (truncate)
		truncate_identifier(buf.data, strlen(buf.data), true);

	*word = buf.data;

	return str;
}

static OuterInnerRels *
OuterInnerRelsCreate(char *name, List *outer_inner_list)
{
	OuterInnerRels *outer_inner;

	outer_inner = palloc(sizeof(OuterInnerRels));
	outer_inner->relation = name;
	outer_inner->outer_inner_pair = outer_inner_list;

	return outer_inner;
}

static const char *
parse_parentheses_Leading_in(const char *str, OuterInnerRels **outer_inner)
{
	List   *outer_inner_pair = NIL;

	if ((str = skip_parenthesis(str, '(')) == NULL)
		return NULL;

	skip_space(str);

	/* Store words in parentheses into outer_inner_list. */
	while(*str != ')' && *str != '\0')
	{
		OuterInnerRels *outer_inner_rels;

		if (*str == '(')
		{
			str = parse_parentheses_Leading_in(str, &outer_inner_rels);
			if (str == NULL)
				break;
		}
		else
		{
			char   *name;

			if ((str = parse_quoted_value(str, &name, true)) == NULL)
				break;
			else
				outer_inner_rels = OuterInnerRelsCreate(name, NIL);
		}

		outer_inner_pair = lappend(outer_inner_pair, outer_inner_rels);
		skip_space(str);
	}

	if (str == NULL ||
		(str = skip_parenthesis(str, ')')) == NULL)
	{
		list_free(outer_inner_pair);
		return NULL;
	}

	*outer_inner = OuterInnerRelsCreate(NULL, outer_inner_pair);

	return str;
}

static const char *
parse_parentheses_Leading(const char *str, List **name_list,
	OuterInnerRels **outer_inner)
{
	char   *name;
	bool	truncate = true;

	if ((str = skip_parenthesis(str, '(')) == NULL)
		return NULL;

	skip_space(str);
	if (*str =='(')
	{
		if ((str = parse_parentheses_Leading_in(str, outer_inner)) == NULL)
			return NULL;
	}
	else
	{
		/* Store words in parentheses into name_list. */
		while(*str != ')' && *str != '\0')
		{
			if ((str = parse_quoted_value(str, &name, truncate)) == NULL)
			{
				list_free(*name_list);
				return NULL;
			}

			*name_list = lappend(*name_list, name);
			skip_space(str);
		}
	}

	if ((str = skip_parenthesis(str, ')')) == NULL)
		return NULL;
	return str;
}

static const char *
parse_parentheses(const char *str, List **name_list, HintKeyword keyword)
{
	char   *name;
	bool	truncate = true;

	if ((str = skip_parenthesis(str, '(')) == NULL)
		return NULL;

	skip_space(str);

	/* Store words in parentheses into name_list. */
	while(*str != ')' && *str != '\0')
	{
		if ((str = parse_quoted_value(str, &name, truncate)) == NULL)
		{
			list_free(*name_list);
			return NULL;
		}

		*name_list = lappend(*name_list, name);
		skip_space(str);

		if (keyword == HINT_KEYWORD_INDEXSCANREGEXP ||
			keyword == HINT_KEYWORD_INDEXONLYSCANREGEXP ||
			keyword == HINT_KEYWORD_BITMAPSCANREGEXP ||
			keyword == HINT_KEYWORD_SET)
		{
			truncate = false;
		}
	}

	if ((str = skip_parenthesis(str, ')')) == NULL)
		return NULL;
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

			hint = parser->create_func(head, keyword, parser->hint_keyword);

			/* parser of each hint does parse in a parenthesis. */
			if ((str = hint->parse_func(hint, hstate, parse, str)) == NULL)
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
			hint_ereport(head,
						 ("Unrecognized hint keyword \"%s\".", buf.data));
			pfree(buf.data);
			return;
		}
	}

	pfree(buf.data);
}


/* 
 * Get hints from table by client-supplied query string and application name.
 */
static const char *
get_hints_from_table(const char *client_query, const char *client_application)
{
	const char *search_query =
		"SELECT hints "
		"  FROM hint_plan.hints "
		" WHERE norm_query_string = $1 "
		"   AND ( application_name = $2 "
		"    OR application_name = '' ) "
		" ORDER BY application_name DESC";
	static SPIPlanPtr plan = NULL;
	char   *hints = NULL;
	Oid		argtypes[2] = { TEXTOID, TEXTOID };
	Datum	values[2];
	bool	nulls[2] = { false, false };
	text   *qry;
	text   *app;

	PG_TRY();
	{
		hint_inhibit_level++;
	
		SPI_connect();
	
		if (plan == NULL)
		{
			SPIPlanPtr	p;
			p = SPI_prepare(search_query, 2, argtypes);
			plan = SPI_saveplan(p);
			SPI_freeplan(p);
		}
	
		qry = cstring_to_text(client_query);
		app = cstring_to_text(client_application);
		values[0] = PointerGetDatum(qry);
		values[1] = PointerGetDatum(app);
	
		SPI_execute_plan(plan, values, nulls, true, 1);
	
		if (SPI_processed > 0)
		{
			char	*buf;
	
			hints = SPI_getvalue(SPI_tuptable->vals[0],
								 SPI_tuptable->tupdesc, 1);
			/*
			 * Here we use SPI_palloc to ensure that hints string is valid even
			 * after SPI_finish call.  We can't use simple palloc because it
			 * allocates memory in SPI's context and that context is deleted in
			 * SPI_finish.
			 */
			buf = SPI_palloc(strlen(hints) + 1);
			strcpy(buf, hints);
			hints = buf;
		}
	
		SPI_finish();
	
		hint_inhibit_level--;
	}
	PG_CATCH();
	{
		hint_inhibit_level--;
		PG_RE_THROW();
	}
	PG_END_TRY();

	return hints;
}

/*
 * Get client-supplied query string.
 */
static const char *
get_query_string(void)
{
	const char *p;

	if (plpgsql_recurse_level > 0)
	{
		/*
		 * This is quite ugly but this is the only point I could find where
		 * we can get the query string.
		 */
		p = (char*)error_context_stack->arg;
	}
	else if (stmt_name)
	{
		PreparedStatement  *entry;

		entry = FetchPreparedStatement(stmt_name, true);
		p = entry->plansource->query_string;
	}
	else
		p = debug_query_string;

	return p;
}

/*
 * Get hints from the head block comment in client-supplied query string.
 */
static const char *
get_hints_from_comment(const char *p)
{
	const char *hint_head;
	char	   *head;
	char	   *tail;
	int			len;

	if (p == NULL)
		return NULL;

	/* extract query head comment. */
	hint_head = strstr(p, HINT_START);
	if (hint_head == NULL)
		return NULL;
	for (;p < hint_head; p++)
	{
		/*
		 * Allow these characters precedes hint comment:
		 *   - digits
		 *   - alphabets which are in ASCII range
		 *   - space, tabs and new-lines
		 *   - underscores, for identifier
		 *   - commas, for SELECT clause, EXPLAIN and PREPARE
		 *   - parentheses, for EXPLAIN and PREPARE
		 *
		 * Note that we don't use isalpha() nor isalnum() in ctype.h here to
		 * avoid behavior which depends on locale setting.
		 */
		if (!(*p >= '0' && *p <= '9') &&
			!(*p >= 'A' && *p <= 'Z') &&
			!(*p >= 'a' && *p <= 'z') &&
			!isspace(*p) &&
			*p != '_' &&
			*p != ',' &&
			*p != '(' && *p != ')')
			return NULL;
	}

	len = strlen(HINT_START);
	head = (char *) p;
	p += len;
	skip_space(p);

	/* find hint end keyword. */
	if ((tail = strstr(p, HINT_END)) == NULL)
	{
		hint_ereport(head, ("Unterminated block comment."));
		return NULL;
	}

	/* We don't support nested block comments. */
	if ((head = strstr(p, BLOCK_COMMENT_START)) != NULL && head < tail)
	{
		hint_ereport(head, ("Nested block comments are not supported."));
		return NULL;
	}

	/* Make a copy of hint. */
	len = tail - p;
	head = palloc(len + 1);
	memcpy(head, p, len);
	head[len] = '\0';
	p = head;

	return p;
}

/*
 * Parse hints that got, create hint struct from parse tree and parse hints.
 */
static HintState *
create_hintstate(Query *parse, const char *hints)
{
	const char *p;
	int			i;
	HintState   *hstate;

	if (hints == NULL)
		return NULL;

	/* -1 means that no Parallel hint is specified. */
	max_hint_nworkers = -1;

	p = hints;
	hstate = HintStateCreate();
	hstate->hint_str = (char *) hints;

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

	/* Count number of hints per hint-type. */
	for (i = 0; i < hstate->nall_hints; i++)
	{
		Hint   *cur_hint = hstate->all_hints[i];
		hstate->num_hints[cur_hint->type]++;
	}

	/*
	 * If an object (or a set of objects) has multiple hints of same hint-type,
	 * only the last hint is valid and others are ignored in planning.
	 * Hints except the last are marked as 'duplicated' to remember the order.
	 */
	for (i = 0; i < hstate->nall_hints - 1; i++)
	{
		Hint   *cur_hint = hstate->all_hints[i];
		Hint   *next_hint = hstate->all_hints[i + 1];

		/*
		 * Leading hint is marked as 'duplicated' in transform_join_hints.
		 */
		if (cur_hint->type == HINT_TYPE_LEADING &&
			next_hint->type == HINT_TYPE_LEADING)
			continue;

		/*
		 * Note that we need to pass addresses of hint pointers, because
		 * HintCmp is designed to sort array of Hint* by qsort.
		 */
		if (HintCmp(&cur_hint, &next_hint) == 0)
		{
			hint_ereport(cur_hint->hint_str,
						 ("Conflict %s hint.", HintTypeName[cur_hint->type]));
			cur_hint->state = HINT_STATE_DUPLICATION;
		}
	}

	/*
	 * Make sure that per-type array pointers point proper position in the
	 * array which consists of all hints.
	 */
	hstate->scan_hints = (ScanMethodHint **) hstate->all_hints;
	hstate->join_hints = (JoinMethodHint **) (hstate->scan_hints +
		hstate->num_hints[HINT_TYPE_SCAN_METHOD]);
	hstate->leading_hint = (LeadingHint **) (hstate->join_hints +
		hstate->num_hints[HINT_TYPE_JOIN_METHOD]);
	hstate->set_hints = (SetHint **) (hstate->leading_hint +
		hstate->num_hints[HINT_TYPE_LEADING]);
	hstate->rows_hints = (RowsHint **) (hstate->set_hints +
		hstate->num_hints[HINT_TYPE_SET]);
	hstate->parallel_hints = (ParallelHint **) (hstate->set_hints +
		hstate->num_hints[HINT_TYPE_ROWS]);

	return hstate;
}

/*
 * Parse inside of parentheses of scan-method hints.
 */
static const char *
ScanMethodHintParse(ScanMethodHint *hint, HintState *hstate, Query *parse,
					const char *str)
{
	const char	   *keyword = hint->base.keyword;
	HintKeyword		hint_keyword = hint->base.hint_keyword;
	List		   *name_list = NIL;
	int				length;

	if ((str = parse_parentheses(str, &name_list, hint_keyword)) == NULL)
		return NULL;

	/* Parse relation name and index name(s) if given hint accepts. */
	length = list_length(name_list);

	/* at least twp parameters required */
	if (length < 1)
	{
		hint_ereport(str,
					 ("%s hint requires a relation.",  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;
		return str;
	}

	hint->relname = linitial(name_list);
	hint->indexnames = list_delete_first(name_list);

	/* check whether the hint accepts index name(s) */
	if (length > 1 && !SCAN_HINT_ACCEPTS_INDEX_NAMES(hint_keyword))
	{
		hint_ereport(str,
					 ("%s hint accepts only one relation.",
					  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;
		return str;
	}

	/* Set a bit for specified hint. */
	switch (hint_keyword)
	{
		case HINT_KEYWORD_SEQSCAN:
			hint->enforce_mask = ENABLE_SEQSCAN;
			break;
		case HINT_KEYWORD_INDEXSCAN:
			hint->enforce_mask = ENABLE_INDEXSCAN;
			break;
		case HINT_KEYWORD_INDEXSCANREGEXP:
			hint->enforce_mask = ENABLE_INDEXSCAN;
			hint->regexp = true;
			break;
		case HINT_KEYWORD_BITMAPSCAN:
			hint->enforce_mask = ENABLE_BITMAPSCAN;
			break;
		case HINT_KEYWORD_BITMAPSCANREGEXP:
			hint->enforce_mask = ENABLE_BITMAPSCAN;
			hint->regexp = true;
			break;
		case HINT_KEYWORD_TIDSCAN:
			hint->enforce_mask = ENABLE_TIDSCAN;
			break;
		case HINT_KEYWORD_NOSEQSCAN:
			hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_SEQSCAN;
			break;
		case HINT_KEYWORD_NOINDEXSCAN:
			hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_INDEXSCAN;
			break;
		case HINT_KEYWORD_NOBITMAPSCAN:
			hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_BITMAPSCAN;
			break;
		case HINT_KEYWORD_NOTIDSCAN:
			hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_TIDSCAN;
			break;
		case HINT_KEYWORD_INDEXONLYSCAN:
			hint->enforce_mask = ENABLE_INDEXSCAN | ENABLE_INDEXONLYSCAN;
			break;
		case HINT_KEYWORD_INDEXONLYSCANREGEXP:
			hint->enforce_mask = ENABLE_INDEXSCAN | ENABLE_INDEXONLYSCAN;
			hint->regexp = true;
			break;
		case HINT_KEYWORD_NOINDEXONLYSCAN:
			hint->enforce_mask = ENABLE_ALL_SCAN ^ ENABLE_INDEXONLYSCAN;
			break;
		default:
			hint_ereport(str, ("Unrecognized hint keyword \"%s\".", keyword));
			return NULL;
			break;
	}

	return str;
}

static const char *
JoinMethodHintParse(JoinMethodHint *hint, HintState *hstate, Query *parse,
					const char *str)
{
	const char	   *keyword = hint->base.keyword;
	HintKeyword		hint_keyword = hint->base.hint_keyword;
	List		   *name_list = NIL;

	if ((str = parse_parentheses(str, &name_list, hint_keyword)) == NULL)
		return NULL;

	hint->nrels = list_length(name_list);

	if (hint->nrels > 0)
	{
		ListCell   *l;
		int			i = 0;

		/*
		 * Transform relation names from list to array to sort them with qsort
		 * after.
		 */
		hint->relnames = palloc(sizeof(char *) * hint->nrels);
		foreach (l, name_list)
		{
			hint->relnames[i] = lfirst(l);
			i++;
		}
	}

	list_free(name_list);

	/* A join hint requires at least two relations */
	if (hint->nrels < 2)
	{
		hint_ereport(str,
					 ("%s hint requires at least two relations.",
					  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;
		return str;
	}

	/* Sort hints in alphabetical order of relation names. */
	qsort(hint->relnames, hint->nrels, sizeof(char *), RelnameCmp);

	switch (hint_keyword)
	{
		case HINT_KEYWORD_NESTLOOP:
			hint->enforce_mask = ENABLE_NESTLOOP;
			break;
		case HINT_KEYWORD_MERGEJOIN:
			hint->enforce_mask = ENABLE_MERGEJOIN;
			break;
		case HINT_KEYWORD_HASHJOIN:
			hint->enforce_mask = ENABLE_HASHJOIN;
			break;
		case HINT_KEYWORD_NONESTLOOP:
			hint->enforce_mask = ENABLE_ALL_JOIN ^ ENABLE_NESTLOOP;
			break;
		case HINT_KEYWORD_NOMERGEJOIN:
			hint->enforce_mask = ENABLE_ALL_JOIN ^ ENABLE_MERGEJOIN;
			break;
		case HINT_KEYWORD_NOHASHJOIN:
			hint->enforce_mask = ENABLE_ALL_JOIN ^ ENABLE_HASHJOIN;
			break;
		default:
			hint_ereport(str, ("Unrecognized hint keyword \"%s\".", keyword));
			return NULL;
			break;
	}

	return str;
}

static bool
OuterInnerPairCheck(OuterInnerRels *outer_inner)
{
	ListCell *l;
	if (outer_inner->outer_inner_pair == NIL)
	{
		if (outer_inner->relation)
			return true;
		else
			return false;
	}

	if (list_length(outer_inner->outer_inner_pair) == 2)
	{
		foreach(l, outer_inner->outer_inner_pair)
		{
			if (!OuterInnerPairCheck(lfirst(l)))
				return false;
		}
	}
	else
		return false;

	return true;
}

static List *
OuterInnerList(OuterInnerRels *outer_inner)
{
	List		   *outer_inner_list = NIL;
	ListCell	   *l;
	OuterInnerRels *outer_inner_rels;

	foreach(l, outer_inner->outer_inner_pair)
	{
		outer_inner_rels = (OuterInnerRels *)(lfirst(l));

		if (outer_inner_rels->relation != NULL)
			outer_inner_list = lappend(outer_inner_list,
									   outer_inner_rels->relation);
		else
			outer_inner_list = list_concat(outer_inner_list,
										   OuterInnerList(outer_inner_rels));
	}
	return outer_inner_list;
}

static const char *
LeadingHintParse(LeadingHint *hint, HintState *hstate, Query *parse,
				 const char *str)
{
	List		   *name_list = NIL;
	OuterInnerRels *outer_inner = NULL;

	if ((str = parse_parentheses_Leading(str, &name_list, &outer_inner)) ==
		NULL)
		return NULL;

	if (outer_inner != NULL)
		name_list = OuterInnerList(outer_inner);

	hint->relations = name_list;
	hint->outer_inner = outer_inner;

	/* A Leading hint requires at least two relations */
	if ( hint->outer_inner == NULL && list_length(hint->relations) < 2)
	{
		hint_ereport(hint->base.hint_str,
					 ("%s hint requires at least two relations.",
					  HINT_LEADING));
		hint->base.state = HINT_STATE_ERROR;
	}
	else if (hint->outer_inner != NULL &&
			 !OuterInnerPairCheck(hint->outer_inner))
	{
		hint_ereport(hint->base.hint_str,
					 ("%s hint requires two sets of relations when parentheses nests.",
					  HINT_LEADING));
		hint->base.state = HINT_STATE_ERROR;
	}

	return str;
}

static const char *
SetHintParse(SetHint *hint, HintState *hstate, Query *parse, const char *str)
{
	List   *name_list = NIL;

	if ((str = parse_parentheses(str, &name_list, hint->base.hint_keyword))
		== NULL)
		return NULL;

	hint->words = name_list;

	/* We need both name and value to set GUC parameter. */
	if (list_length(name_list) == 2)
	{
		hint->name = linitial(name_list);
		hint->value = lsecond(name_list);
	}
	else
	{
		hint_ereport(hint->base.hint_str,
					 ("%s hint requires name and value of GUC parameter.",
					  HINT_SET));
		hint->base.state = HINT_STATE_ERROR;
	}

	return str;
}

static const char *
RowsHintParse(RowsHint *hint, HintState *hstate, Query *parse,
			  const char *str)
{
	HintKeyword		hint_keyword = hint->base.hint_keyword;
	List		   *name_list = NIL;
	char		   *rows_str;
	char		   *end_ptr;

	if ((str = parse_parentheses(str, &name_list, hint_keyword)) == NULL)
		return NULL;

	/* Last element must be rows specification */
	hint->nrels = list_length(name_list) - 1;

	if (hint->nrels > 0)
	{
		ListCell   *l;
		int			i = 0;

		/*
		 * Transform relation names from list to array to sort them with qsort
		 * after.
		 */
		hint->relnames = palloc(sizeof(char *) * hint->nrels);
		foreach (l, name_list)
		{
			if (hint->nrels <= i)
				break;
			hint->relnames[i] = lfirst(l);
			i++;
		}
	}

	/* Retieve rows estimation */
	rows_str = list_nth(name_list, hint->nrels);
	hint->rows_str = rows_str;		/* store as-is for error logging */
	if (rows_str[0] == '#')
	{
		hint->value_type = RVT_ABSOLUTE;
		rows_str++;
	}
	else if (rows_str[0] == '+')
	{
		hint->value_type = RVT_ADD;
		rows_str++;
	}
	else if (rows_str[0] == '-')
	{
		hint->value_type = RVT_SUB;
		rows_str++;
	}
	else if (rows_str[0] == '*')
	{
		hint->value_type = RVT_MULTI;
		rows_str++;
	}
	else
	{
		hint_ereport(rows_str, ("Unrecognized rows value type notation."));
		hint->base.state = HINT_STATE_ERROR;
		return str;
	}
	hint->rows = strtod(rows_str, &end_ptr);
	if (*end_ptr)
	{
		hint_ereport(rows_str,
					 ("%s hint requires valid number as rows estimation.",
					  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;
		return str;
	}

	/* A join hint requires at least two relations */
	if (hint->nrels < 2)
	{
		hint_ereport(str,
					 ("%s hint requires at least two relations.",
					  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;
		return str;
	}

	list_free(name_list);

	/* Sort relnames in alphabetical order. */
	qsort(hint->relnames, hint->nrels, sizeof(char *), RelnameCmp);

	return str;
}

static const char *
ParallelHintParse(ParallelHint *hint, HintState *hstate, Query *parse,
				  const char *str)
{
	HintKeyword		hint_keyword = hint->base.hint_keyword;
	List		   *name_list = NIL;
	int				length;
	char   *end_ptr;
	int		nworkers;
	bool	force_parallel = false;

	if ((str = parse_parentheses(str, &name_list, hint_keyword)) == NULL)
		return NULL;

	/* Parse relation name and index name(s) if given hint accepts. */
	length = list_length(name_list);

	if (length < 2 || length > 3)
	{
		hint_ereport(str,
					 ("Wrong number of arguments for %s hint.",
					  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;
		return str;
	}

	hint->relname = linitial(name_list);
		
	/* The second parameter is number of workers */
	hint->nworkers_str = list_nth(name_list, 1);
	nworkers = strtod(hint->nworkers_str, &end_ptr);
	if (*end_ptr || nworkers < 0)
	{
		hint_ereport(hint->nworkers_str,
					 ("number of workers must be a positive integer: %s",
					  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;
		return str;
	}

	hint->nworkers = nworkers;

	if (nworkers > max_hint_nworkers)
		max_hint_nworkers = nworkers;

	/* optional third parameter is specified */
	if (length == 3)
	{
		const char *modeparam = (const char *)list_nth(name_list, 2);
		if (strcasecmp(modeparam, "hard") == 0)
			force_parallel = true;
		else if (strcasecmp(modeparam, "soft") != 0)
		{
			hint_ereport(str,
						 ("The mode of Worker hint must be soft or hard."));
			hint->base.state = HINT_STATE_ERROR;
			return str;
		}
	}
	
	hint->force_parallel = force_parallel;

	return str;
}

/*
 * set GUC parameter functions
 */

static int
get_current_scan_mask()
{
	int mask = 0;

	if (enable_seqscan)
		mask |= ENABLE_SEQSCAN;
	if (enable_indexscan)
		mask |= ENABLE_INDEXSCAN;
	if (enable_bitmapscan)
		mask |= ENABLE_BITMAPSCAN;
	if (enable_tidscan)
		mask |= ENABLE_TIDSCAN;
	if (enable_indexonlyscan)
		mask |= ENABLE_INDEXONLYSCAN;

	return mask;
}

static int
get_current_join_mask()
{
	int mask = 0;

	if (enable_nestloop)
		mask |= ENABLE_NESTLOOP;
	if (enable_mergejoin)
		mask |= ENABLE_MERGEJOIN;
	if (enable_hashjoin)
		mask |= ENABLE_HASHJOIN;

	return mask;
}

/*
 * Sets GUC prameters without throwing exception. Reutrns false if something
 * wrong.
 */
static int
set_config_option_noerror(const char *name, const char *value,
						  GucContext context, GucSource source,
						  GucAction action, bool changeVal, int elevel)
{
	int				result = 0;
	MemoryContext	ccxt = CurrentMemoryContext;

	PG_TRY();
	{
		result = set_config_option(name, value, context, source,
								   action, changeVal, 0, false);
	}
	PG_CATCH();
	{
		ErrorData	   *errdata;

		/* Save error info */
		MemoryContextSwitchTo(ccxt);
		errdata = CopyErrorData();
		FlushErrorState();

		ereport(elevel,
				(errcode(errdata->sqlerrcode),
				 errhidestmt(hidestmt),
				 errmsg("%s", errdata->message),
				 errdata->detail ? errdetail("%s", errdata->detail) : 0,
				 errdata->hint ? errhint("%s", errdata->hint) : 0));
		FreeErrorData(errdata);
	}
	PG_END_TRY();

	return result;
}

/*
 * Sets GUC parameter of int32 type without throwing exceptions. Returns false
 * if something wrong.
 */
static int
set_config_int32_option(const char *name, int32 value, GucContext context)
{
	char buf[16];	/* enough for int32 */

	if (snprintf(buf, 16, "%d", value) < 0)
	{
		ereport(pg_hint_plan_message_level,
				(errmsg ("Cannot set integer value: %d: %s",
						 max_hint_nworkers, strerror(errno))));
		return false;
	}

	return
		set_config_option_noerror(name, buf, context,
								  PGC_S_SESSION, GUC_ACTION_SAVE, true,
								  pg_hint_plan_message_level);
}

/* setup scan method enforcement according to given options */
static void
setup_guc_enforcement(SetHint **options, int noptions, GucContext context)
{
	int	i;

	for (i = 0; i < noptions; i++)
	{
		SetHint	   *hint = options[i];
		int			result;

		if (!hint_state_enabled(hint))
			continue;

		result = set_config_option_noerror(hint->name, hint->value, context,
										   PGC_S_SESSION, GUC_ACTION_SAVE, true,
										   pg_hint_plan_message_level);
		if (result != 0)
			hint->base.state = HINT_STATE_USED;
		else
			hint->base.state = HINT_STATE_ERROR;
	}

	return;
}

/*
 * Setup parallel execution environment.
 *
 * If hint is not NULL, set up using it, elsewise reset to initial environment.
 */
static void
setup_parallel_plan_enfocement(ParallelHint *hint, HintState *state)
{
	if (hint)
	{
		hint->base.state = HINT_STATE_USED;
		set_config_int32_option("max_parallel_workers_per_gather",
								hint->nworkers, state->context);
	}
	else
		set_config_int32_option("max_parallel_workers_per_gather",
								state->init_nworkers, state->context);

	/* force means that enforce parallel as far as possible */
	if (hint && hint->force_parallel)
	{
		set_config_int32_option("parallel_tuple_cost", 0, state->context);
		set_config_int32_option("parallel_setup_cost", 0, state->context);
		set_config_int32_option("min_parallel_relation_size", 0,
								state->context);
	}
	else
	{
		set_config_int32_option("parallel_tuple_cost",
								state->init_paratup_cost, state->context);
		set_config_int32_option("parallel_setup_cost",
								state->init_parasetup_cost, state->context);
		set_config_int32_option("min_parallel_relation_size",
								state->init_min_para_size, state->context);
	}
}

#define SET_CONFIG_OPTION(name, type_bits) \
	set_config_option_noerror((name), \
		(mask & (type_bits)) ? "true" : "false", \
		context, PGC_S_SESSION, GUC_ACTION_SAVE, true, ERROR)


/*
 * Setup GUC environment to enforce scan methods. If scanhint is NULL, reset
 * GUCs to the saved state in state.
 */
static void
setup_scan_method_enforcement(ScanMethodHint *scanhint, HintState *state)
{
	unsigned char	enforce_mask = state->init_scan_mask;
	GucContext		context = state->context;
	unsigned char	mask;

	if (scanhint)
	{
		enforce_mask = scanhint->enforce_mask;
		scanhint->base.state = HINT_STATE_USED;
	}

	if (enforce_mask == ENABLE_SEQSCAN || enforce_mask == ENABLE_INDEXSCAN ||
		enforce_mask == ENABLE_BITMAPSCAN || enforce_mask == ENABLE_TIDSCAN
		|| enforce_mask == (ENABLE_INDEXSCAN | ENABLE_INDEXONLYSCAN)
		)
		mask = enforce_mask;
	else
		mask = enforce_mask & current_hint_state->init_scan_mask;

	SET_CONFIG_OPTION("enable_seqscan", ENABLE_SEQSCAN);
	SET_CONFIG_OPTION("enable_indexscan", ENABLE_INDEXSCAN);
	SET_CONFIG_OPTION("enable_bitmapscan", ENABLE_BITMAPSCAN);
	SET_CONFIG_OPTION("enable_tidscan", ENABLE_TIDSCAN);
	SET_CONFIG_OPTION("enable_indexonlyscan", ENABLE_INDEXONLYSCAN);
}

static void
set_join_config_options(unsigned char enforce_mask, GucContext context)
{
	unsigned char	mask;

	if (enforce_mask == ENABLE_NESTLOOP || enforce_mask == ENABLE_MERGEJOIN ||
		enforce_mask == ENABLE_HASHJOIN)
		mask = enforce_mask;
	else
		mask = enforce_mask & current_hint_state->init_join_mask;

	SET_CONFIG_OPTION("enable_nestloop", ENABLE_NESTLOOP);
	SET_CONFIG_OPTION("enable_mergejoin", ENABLE_MERGEJOIN);
	SET_CONFIG_OPTION("enable_hashjoin", ENABLE_HASHJOIN);
}

/*
 * pg_hint_plan hook functions
 */

static void
pg_hint_plan_ProcessUtility(Node *parsetree, const char *queryString,
							ProcessUtilityContext context,
							ParamListInfo params,
							DestReceiver *dest, char *completionTag)
{
	Node				   *node;

	/* 
	 * Use standard planner if pg_hint_plan is disabled or current nesting 
	 * depth is nesting depth of SPI calls. 
	 */
	if (!pg_hint_plan_enable_hint || hint_inhibit_level > 0)
	{
		if (debug_level > 1)
			ereport(pg_hint_plan_message_level,
					(errmsg ("pg_hint_plan: ProcessUtility:"
							 " pg_hint_plan.enable_hint = off")));
		if (prev_ProcessUtility)
			(*prev_ProcessUtility) (parsetree, queryString,
									context, params,
									dest, completionTag);
		else
			standard_ProcessUtility(parsetree, queryString,
									context, params,
									dest, completionTag);
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

	if (stmt_name)
	{
		if (debug_level > 1)
			ereport(pg_hint_plan_message_level,
					(errmsg ("pg_hint_plan: ProcessUtility:"
							 " stmt_name = \"%s\", statement=\"%s\"",
							 stmt_name, queryString)));

		PG_TRY();
		{
			if (prev_ProcessUtility)
				(*prev_ProcessUtility) (parsetree, queryString,
										context, params,
										dest, completionTag);
			else
				standard_ProcessUtility(parsetree, queryString,
										context, params,
										dest, completionTag);
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
			(*prev_ProcessUtility) (parsetree, queryString,
									context, params,
									dest, completionTag);
		else
			standard_ProcessUtility(parsetree, queryString,
									context, params,
									dest, completionTag);
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
	current_hint_state = hstate;
}

/* Pop a hint from hint stack.  Popped hint is automatically discarded. */
static void
pop_hint(void)
{
	/* Hint stack must not be empty. */
	if(HintStateStack == NIL)
		elog(ERROR, "hint stack is empty");

	/*
	 * Take a hint at the head from the list, and free it.  Switch
	 * current_hint_state to point new head (NULL if the list is empty).
	 */
	HintStateStack = list_delete_first(HintStateStack);
	HintStateDelete(current_hint_state);
	if(HintStateStack == NIL)
		current_hint_state = NULL;
	else
		current_hint_state = (HintState *) lfirst(list_head(HintStateStack));
}

static PlannedStmt *
pg_hint_plan_planner(Query *parse, int cursorOptions, ParamListInfo boundParams)
{
	const char	   *hints = NULL;
	const char	   *query;
	char		   *norm_query;
	pgssJumbleState	jstate;
	int				query_len;
	int				save_nestlevel;
	PlannedStmt	   *result;
	HintState	   *hstate;
	char 			msgstr[1024];

	qnostr[0] = 0;
	strcpy(msgstr, "");
	if (debug_level > 1)
		snprintf(qnostr, sizeof(qnostr), "[qno=0x%x]", qno++);
	hidestmt = false;

	/*
	 * Use standard planner if pg_hint_plan is disabled or current nesting 
	 * depth is nesting depth of SPI calls. Other hook functions try to change
	 * plan with current_hint_state if any, so set it to NULL.
	 */
	if (!pg_hint_plan_enable_hint || hint_inhibit_level > 0)
	{
		if (debug_level > 1)
			elog(pg_hint_plan_message_level,
				 "pg_hint_plan%s: planner: enable_hint=%d,"
				 " hint_inhibit_level=%d",
				 qnostr, pg_hint_plan_enable_hint, hint_inhibit_level);
		hidestmt = true;

		goto standard_planner_proc;
	}

	/* Create hint struct from client-supplied query string. */
	query = get_query_string();

	/*
	 * Create hintstate from hint specified for the query, if any.
	 *
	 * First we lookup hint in pg_hint.hints table by normalized query string,
	 * unless pg_hint_plan.enable_hint_table is OFF.
	 * This parameter provides option to avoid overhead of table lookup during
	 * planning.
	 *
	 * If no hint was found, then we try to get hint from special query comment.
	 */
	if (pg_hint_plan_enable_hint_table)
	{
		/*
		 * Search hint information which is stored for the query and the
		 * application.  Query string is normalized before using in condition
		 * in order to allow fuzzy matching.
		 *
		 * XXX: normalizing code is copied from pg_stat_statements.c, so be
		 * careful when supporting PostgreSQL's version up.
		 */
		jstate.jumble = (unsigned char *) palloc(JUMBLE_SIZE);
		jstate.jumble_len = 0;
		jstate.clocations_buf_size = 32;
		jstate.clocations = (pgssLocationLen *)
			palloc(jstate.clocations_buf_size * sizeof(pgssLocationLen));
		jstate.clocations_count = 0;
		JumbleQuery(&jstate, parse);
		/*
		 * generate_normalized_query() copies exact given query_len bytes, so we
		 * add 1 byte for null-termination here.  As comments on
		 * generate_normalized_query says, generate_normalized_query doesn't
		 * take care of null-terminate, but additional 1 byte ensures that '\0'
		 * byte in the source buffer to be copied into norm_query.
		 */
		query_len = strlen(query) + 1;
		norm_query = generate_normalized_query(&jstate,
											   query,
											   &query_len,
											   GetDatabaseEncoding());
		hints = get_hints_from_table(norm_query, application_name);
		if (debug_level > 1)
		{
			if (hints)
				snprintf(msgstr, 1024, "hints from table: \"%s\":"
						 " normalzed_query=\"%s\", application name =\"%s\"",
						 hints, norm_query, application_name);
			else
			{
				ereport(pg_hint_plan_message_level,
						(errhidestmt(hidestmt),
						 errmsg("pg_hint_plan%s:"
								" no match found in table:"
								"  application name = \"%s\","
								" normalzed_query=\"%s\"",
								qnostr, application_name, norm_query)));
				hidestmt = true;
			}
		}
	}
	if (hints == NULL)
	{
		hints = get_hints_from_comment(query);

		if (debug_level > 1)
		{
			snprintf(msgstr, 1024, "hints in comment=\"%s\"",
					 hints ? hints : "(none)");
			if (debug_level > 2 || 
				stmt_name || strcmp(query, debug_query_string))
				snprintf(msgstr + strlen(msgstr), 1024- strlen(msgstr), 
					 ", stmt=\"%s\", query=\"%s\", debug_query_string=\"%s\"",
						 stmt_name, query, debug_query_string);
		}
	}

	hstate = create_hintstate(parse, hints);

	/*
	 * Use standard planner if the statement has not valid hint.  Other hook
	 * functions try to change plan with current_hint_state if any, so set it
	 * to NULL.
	 */
	if (!hstate)
		goto standard_planner_proc;

	/*
	 * Push new hint struct to the hint stack to disable previous hint context.
	 */
	push_hint(hstate);

	/*  Set scan enforcement here. */
	save_nestlevel = NewGUCNestLevel();

	/* Apply Set hints, then save it as the initial state  */
	setup_guc_enforcement(current_hint_state->set_hints,
						   current_hint_state->num_hints[HINT_TYPE_SET],
						   current_hint_state->context);
	
	current_hint_state->init_scan_mask = get_current_scan_mask();
	current_hint_state->init_join_mask = get_current_join_mask();
	current_hint_state->init_min_para_size = min_parallel_relation_size;
	current_hint_state->init_paratup_cost = parallel_tuple_cost;
	current_hint_state->init_parasetup_cost = parallel_setup_cost;

	/*
	 * max_parallel_workers_per_gather should be non-zero here if Workers hint
	 * is specified.
	 */
	if (max_hint_nworkers >= 0)
	{
		current_hint_state->init_nworkers = 0;
		set_config_int32_option("max_parallel_workers_per_gather",
								1, current_hint_state->context);
	}

	if (debug_level > 1)
	{
		ereport(pg_hint_plan_message_level,
				(errhidestmt(hidestmt),
				 errmsg("pg_hint_plan%s: planner: %s",
						qnostr, msgstr))); 
		hidestmt = true;
	}

	/*
	 * Use PG_TRY mechanism to recover GUC parameters and current_hint_state to
	 * the state when this planner started when error occurred in planner.
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
	if (debug_level == 1)
		HintStateDump(current_hint_state);
	else if (debug_level > 1)
		HintStateDump2(current_hint_state);

	/*
	 * Rollback changes of GUC parameters, and pop current hint context from
	 * hint stack to rewind the state.
	 */
	AtEOXact_GUC(true, save_nestlevel);
	pop_hint();

	return result;

standard_planner_proc:
	if (debug_level > 1)
	{
		ereport(pg_hint_plan_message_level,
				(errhidestmt(hidestmt),
				 errmsg("pg_hint_plan%s: planner: no valid hint (%s)",
						qnostr, msgstr)));
		hidestmt = true;
	}
	current_hint_state = NULL;
	if (prev_planner)
		return (*prev_planner) (parse, cursorOptions, boundParams);
	else
		return standard_planner(parse, cursorOptions, boundParams);
}

/*
 * Find scan method hint to be applied to the given relation
 */
static ScanMethodHint *
find_scan_hint(PlannerInfo *root, Index relid, RelOptInfo *rel)
{
	RangeTblEntry  *rte;
	int				i;

	/*
	 * We don't apply scan method hint if the relation is:
	 *   - not a base relation
	 *   - not an ordinary relation (such as join and subquery)
	 */
	if (rel && (rel->reloptkind != RELOPT_BASEREL ||
				rel->rtekind != RTE_RELATION))
		return NULL;

	rte = root->simple_rte_array[relid];

	/* We don't force scan method of foreign tables */
	if (rte->relkind == RELKIND_FOREIGN_TABLE)
		return NULL;

	/* Find scan method hint, which matches given names, from the list. */
	for (i = 0; i < current_hint_state->num_hints[HINT_TYPE_SCAN_METHOD]; i++)
	{
		ScanMethodHint *hint = current_hint_state->scan_hints[i];

		/* We ignore disabled hints. */
		if (!hint_state_enabled(hint))
			continue;

		if (RelnameCmp(&rte->eref->aliasname, &hint->relname) == 0)
			return hint;
	}

	return NULL;
}

static ParallelHint *
find_parallel_hint(PlannerInfo *root, Index relid, RelOptInfo *rel)
{
	RangeTblEntry  *rte;
	int				i;

	/*
	 * We don't apply scan method hint if the relation is:
	 *   - not a base relation and inheritance children
	 *   - not an ordinary relation (such as join and subquery)
	 */
	if (rel && ((rel->reloptkind != RELOPT_BASEREL &&
				 rel->reloptkind != RELOPT_OTHER_MEMBER_REL) ||
				rel->rtekind != RTE_RELATION))
		return NULL;

	rte = root->simple_rte_array[relid];

	/* We can't force scan method of foreign tables */
	if (rte->relkind == RELKIND_FOREIGN_TABLE)
		return NULL;

	/* Find scan method hint, which matches given names, from the list. */
	for (i = 0; i < current_hint_state->num_hints[HINT_TYPE_PARALLEL]; i++)
	{
		ParallelHint *hint = current_hint_state->parallel_hints[i];

		/* We ignore disabled hints. */
		if (!hint_state_enabled(hint))
			continue;

		if (RelnameCmp(&rte->eref->aliasname, &hint->relname) == 0)
			return hint;
	}

	return NULL;
}

/*
 * regexeq
 *
 * Returns TRUE on match, FALSE on no match.
 *
 *   s1 --- the data to match against
 *   s2 --- the pattern
 *
 * Because we copy s1 to NameData, make the size of s1 less than NAMEDATALEN.
 */
static bool
regexpeq(const char *s1, const char *s2)
{
	NameData	name;
	text	   *regexp;
	Datum		result;

	strcpy(name.data, s1);
	regexp = cstring_to_text(s2);

	result = DirectFunctionCall2Coll(nameregexeq,
									 DEFAULT_COLLATION_OID,
									 NameGetDatum(&name),
									 PointerGetDatum(regexp));
	return DatumGetBool(result);
}

static void
delete_indexes(ScanMethodHint *hint, RelOptInfo *rel, Oid relationObjectId)
{
	ListCell	   *cell;
	ListCell	   *prev;
	ListCell	   *next;
	StringInfoData	buf;

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
	if (debug_level > 0)
		initStringInfo(&buf);

	for (cell = list_head(rel->indexlist); cell; cell = next)
	{
		IndexOptInfo   *info = (IndexOptInfo *) lfirst(cell);
		char		   *indexname = get_rel_name(info->indexoid);
		ListCell	   *l;
		bool			use_index = false;

		next = lnext(cell);

		foreach(l, hint->indexnames)
		{
			char   *hintname = (char *) lfirst(l);
			bool	result;

			if (hint->regexp)
				result = regexpeq(indexname, hintname);
			else
				result = RelnameCmp(&indexname, &hintname) == 0;

			if (result)
			{
				use_index = true;
				if (debug_level > 0)
				{
					appendStringInfoCharMacro(&buf, ' ');
					quote_value(&buf, indexname);
				}

				break;
			}
		}

		/*
		 * to make the index a candidate when definition of this index is
		 * matched with the index's definition of current_hint_state.
		 */
		if (OidIsValid(relationObjectId) && !use_index)
		{
			foreach(l, current_hint_state->parent_index_infos)
			{
				int					i;
				HeapTuple			ht_idx;
				ParentIndexInfo	   *p_info = (ParentIndexInfo *)lfirst(l);

				/* check to match the parameter of unique */
				if (p_info->indisunique != info->unique)
					continue;

				/* check to match the parameter of index's method */
				if (p_info->method != info->relam)
					continue;

				/* to check to match the indexkey's configuration */
				if ((list_length(p_info->column_names)) !=
					 info->ncolumns)
					continue;

				/* check to match the indexkey's configuration */
				for (i = 0; i < info->ncolumns; i++)
				{
					char       *c_attname = NULL;
					char       *p_attname = NULL;

					p_attname =
						list_nth(p_info->column_names, i);

					/* both are expressions */
					if (info->indexkeys[i] == 0 && !p_attname)
						continue;

					/* one's column is expression, the other is not */
					if (info->indexkeys[i] == 0 || !p_attname)
						break;

					c_attname = get_attname(relationObjectId,
												info->indexkeys[i]);

					if (strcmp(p_attname, c_attname) != 0)
						break;

					if (p_info->indcollation[i] != info->indexcollations[i])
						break;

					if (p_info->opclass[i] != info->opcintype[i])
						break;

					if (((p_info->indoption[i] & INDOPTION_DESC) != 0) !=
						info->reverse_sort[i])
						break;

					if (((p_info->indoption[i] & INDOPTION_NULLS_FIRST) != 0) !=
						info->nulls_first[i])
						break;

				}

				if (i != info->ncolumns)
					continue;

				if ((p_info->expression_str && (info->indexprs != NIL)) ||
					(p_info->indpred_str && (info->indpred != NIL)))
				{
					/*
					 * Fetch the pg_index tuple by the Oid of the index
					 */
					ht_idx = SearchSysCache1(INDEXRELID,
											 ObjectIdGetDatum(info->indexoid));

					/* check to match the expression's parameter of index */
					if (p_info->expression_str &&
						!heap_attisnull(ht_idx, Anum_pg_index_indexprs))
					{
						Datum       exprsDatum;
						bool        isnull;
						Datum       result;

						/*
						 * to change the expression's parameter of child's
						 * index to strings
						 */
						exprsDatum = SysCacheGetAttr(INDEXRELID, ht_idx,
													 Anum_pg_index_indexprs,
													 &isnull);

						result = DirectFunctionCall2(pg_get_expr,
													 exprsDatum,
													 ObjectIdGetDatum(
														 relationObjectId));

						if (strcmp(p_info->expression_str,
								   text_to_cstring(DatumGetTextP(result))) != 0)
						{
							/* Clean up */
							ReleaseSysCache(ht_idx);

							continue;
						}
					}

					/* Check to match the predicate's parameter of index */
					if (p_info->indpred_str &&
						!heap_attisnull(ht_idx, Anum_pg_index_indpred))
					{
						Datum       predDatum;
						bool        isnull;
						Datum       result;

						/*
						 * to change the predicate's parameter of child's
						 * index to strings
						 */
						predDatum = SysCacheGetAttr(INDEXRELID, ht_idx,
													 Anum_pg_index_indpred,
													 &isnull);

						result = DirectFunctionCall2(pg_get_expr,
													 predDatum,
													 ObjectIdGetDatum(
														 relationObjectId));

						if (strcmp(p_info->indpred_str,
								   text_to_cstring(DatumGetTextP(result))) != 0)
						{
							/* Clean up */
							ReleaseSysCache(ht_idx);

							continue;
						}
					}

					/* Clean up */
					ReleaseSysCache(ht_idx);
				}
				else if (p_info->expression_str || (info->indexprs != NIL))
					continue;
				else if	(p_info->indpred_str || (info->indpred != NIL))
					continue;

				use_index = true;

				/* to log the candidate of index */
				if (debug_level > 0)
				{
					appendStringInfoCharMacro(&buf, ' ');
					quote_value(&buf, indexname);
				}

				break;
			}
		}

		if (!use_index)
			rel->indexlist = list_delete_cell(rel->indexlist, cell, prev);
		else
			prev = cell;

		pfree(indexname);
	}

	if (debug_level == 1)
	{
		char   *relname;
		StringInfoData  rel_buf;

		if (OidIsValid(relationObjectId))
			relname = get_rel_name(relationObjectId);
		else
			relname = hint->relname;

		initStringInfo(&rel_buf);
		quote_value(&rel_buf, relname);

		ereport(LOG,
				(errmsg("available indexes for %s(%s):%s",
					 hint->base.keyword,
					 rel_buf.data,
					 buf.data)));
		pfree(buf.data);
		pfree(rel_buf.data);
	}
}

/* 
 * Return information of index definition.
 */
static ParentIndexInfo *
get_parent_index_info(Oid indexoid, Oid relid)
{
	ParentIndexInfo	*p_info = palloc(sizeof(ParentIndexInfo));
	Relation	    indexRelation;
	Form_pg_index	index;
	char		   *attname;
	int				i;

	indexRelation = index_open(indexoid, RowExclusiveLock);

	index = indexRelation->rd_index;

	p_info->indisunique = index->indisunique;
	p_info->method = indexRelation->rd_rel->relam;

	p_info->column_names = NIL;
	p_info->indcollation = (Oid *) palloc(sizeof(Oid) * index->indnatts);
	p_info->opclass = (Oid *) palloc(sizeof(Oid) * index->indnatts);
	p_info->indoption = (int16 *) palloc(sizeof(Oid) * index->indnatts);

	for (i = 0; i < index->indnatts; i++)
	{
		attname = get_attname(relid, index->indkey.values[i]);
		p_info->column_names = lappend(p_info->column_names, attname);

		p_info->indcollation[i] = indexRelation->rd_indcollation[i];
		p_info->opclass[i] = indexRelation->rd_opcintype[i];
		p_info->indoption[i] = indexRelation->rd_indoption[i];
	}

	/*
	 * to check to match the expression's parameter of index with child indexes
 	 */
	p_info->expression_str = NULL;
	if(!heap_attisnull(indexRelation->rd_indextuple, Anum_pg_index_indexprs))
	{
		Datum       exprsDatum;
		bool		isnull;
		Datum		result;

		exprsDatum = SysCacheGetAttr(INDEXRELID, indexRelation->rd_indextuple,
									 Anum_pg_index_indexprs, &isnull);

		result = DirectFunctionCall2(pg_get_expr,
									 exprsDatum,
									 ObjectIdGetDatum(relid));

		p_info->expression_str = text_to_cstring(DatumGetTextP(result));
	}

	/*
	 * to check to match the predicate's parameter of index with child indexes
 	 */
	p_info->indpred_str = NULL;
	if(!heap_attisnull(indexRelation->rd_indextuple, Anum_pg_index_indpred))
	{
		Datum       predDatum;
		bool		isnull;
		Datum		result;

		predDatum = SysCacheGetAttr(INDEXRELID, indexRelation->rd_indextuple,
									 Anum_pg_index_indpred, &isnull);

		result = DirectFunctionCall2(pg_get_expr,
									 predDatum,
									 ObjectIdGetDatum(relid));

		p_info->indpred_str = text_to_cstring(DatumGetTextP(result));
	}

	index_close(indexRelation, NoLock);

	return p_info;
}

/*
 * Set planner guc parameters according to corresponding scan hints.
 */
static void
setup_hint_enforcement(PlannerInfo *root, Oid relationObjectId,
					   bool inhparent, RelOptInfo *rel)
{
	Index new_parent_relid = 0;
	ListCell *l;
	ScanMethodHint *scanhint = NULL;

	/*
	 * We could register the parent relation of the following children here
	 * when inhparent == true but inheritnce planner doesn't call this function
	 * for parents. Since we cannot distinguish who called this function we
	 * cannot do other than always seeking the parent regardless of who called
	 * this function.
	 */
	if (inhparent)
	{
		if (debug_level > 1)
			ereport(pg_hint_plan_message_level,
					(errhidestmt(true),
					 errmsg ("pg_hint_plan%s: get_relation_info"
							 " skipping inh parent: relation=%u(%s), inhparent=%d,"
							 " current_hint_state=%p, hint_inhibit_level=%d",
							 qnostr, relationObjectId,
							 get_rel_name(relationObjectId),
							 inhparent, current_hint_state, hint_inhibit_level)));
		return;
	}

	/* Find the parent for this relation other than the registered parent */
	foreach (l, root->append_rel_list)
	{
		AppendRelInfo *appinfo = (AppendRelInfo *) lfirst(l);

		if (appinfo->child_relid == rel->relid)
		{
			if (current_hint_state->parent_relid != appinfo->parent_relid)
				new_parent_relid = appinfo->parent_relid;
			break;
		}
	}

	if (!l)
	{
		/* This relation doesn't have a parent. Cancel current_hint_state. */
		current_hint_state->parent_relid = 0;
		current_hint_state->parent_scan_hint = NULL;
		current_hint_state->parent_parallel_hint = NULL;
	}

	if (new_parent_relid > 0)
	{
		/*
		 * Here we found a new parent for the current relation. Scan continues
		 * hint to other childrens of this parent so remember it * to avoid
		 * hinthintredundant setup cost.
		 */
		ScanMethodHint *parent_scan_hint = NULL;
		ParallelHint   *parent_parallel_hint = NULL;

		current_hint_state->parent_relid = new_parent_relid;
				
		/* Find hints for the parent */
		current_hint_state->parent_scan_hint = parent_scan_hint = 
			find_scan_hint(root, current_hint_state->parent_relid, NULL);

		current_hint_state->parent_parallel_hint = parent_parallel_hint = 
			find_parallel_hint(root, current_hint_state->parent_relid, NULL);

		if (parent_scan_hint)
		{
			/*
			 * If hint is found for the parent, apply it for this child instead
			 * of its own.
			 */
			parent_scan_hint->base.state = HINT_STATE_USED;

			/* Apply index mask in the same manner to the parent. */
			if (parent_scan_hint->indexnames)
			{
				Oid			parentrel_oid;
				Relation	parent_rel;

				parentrel_oid =
					root->simple_rte_array[current_hint_state->parent_relid]->relid;
				parent_rel = heap_open(parentrel_oid, NoLock);

				/* Search the parent relation for indexes match the hint spec */
				foreach(l, RelationGetIndexList(parent_rel))
				{
					Oid         indexoid = lfirst_oid(l);
					char       *indexname = get_rel_name(indexoid);
					ListCell   *lc;
					ParentIndexInfo *parent_index_info;

					foreach(lc, parent_scan_hint->indexnames)
					{
						if (RelnameCmp(&indexname, &lfirst(lc)) == 0)
							break;
					}
					if (!lc)
						continue;

					parent_index_info =
						get_parent_index_info(indexoid, parentrel_oid);
					current_hint_state->parent_index_infos =
						lappend(current_hint_state->parent_index_infos, parent_index_info);
				}
				heap_close(parent_rel, NoLock);
			}
		}

		/* Setup planner environment */
		setup_scan_method_enforcement(parent_scan_hint, current_hint_state);
		setup_parallel_plan_enfocement(
			find_parallel_hint(root, rel->relid, rel), current_hint_state);
	}

	/* Process index restriction hint inheritance */
	if (current_hint_state->parent_scan_hint != 0)
	{
		delete_indexes(current_hint_state->parent_scan_hint, rel,
					   relationObjectId);

		/* Scan fixation status is the same to the parent. */
		if (debug_level > 1)
			ereport(pg_hint_plan_message_level,
					(errhidestmt(true),
					 errmsg("pg_hint_plan%s: get_relation_info:"
							" index deletion by parent hint: "
							"relation=%u(%s), inhparent=%d, current_hint_state=%p,"
							" hint_inhibit_level=%d",
							qnostr, relationObjectId,
							get_rel_name(relationObjectId),
							inhparent, current_hint_state, hint_inhibit_level)));
		return;
	}

	/* This table doesn't have a parent hint. Apply its own hint if any. */
	if ((scanhint = find_scan_hint(root, rel->relid, rel)) != NULL)
	{
		setup_scan_method_enforcement(scanhint,	current_hint_state);

		delete_indexes(scanhint, rel, InvalidOid);

		if (debug_level > 1)
			ereport(pg_hint_plan_message_level,
					(errhidestmt(true),
					 errmsg ("pg_hint_plan%s: get_relation_info"
							 " index deletion:"
							 " relation=%u(%s), inhparent=%d, current_hint=%p,"
							 " hint_inhibit_level=%d, scanmask=0x%x",
							 qnostr, relationObjectId,
							 get_rel_name(relationObjectId),
							 inhparent, current_hint_state, hint_inhibit_level,
							 scanhint->enforce_mask)));
		return;
	}

	/* Do the same for parallel plan enforcement */
	setup_parallel_plan_enfocement(find_parallel_hint(root, rel->relid, rel),
								   current_hint_state);

	/* Nothing to apply. Reset the scan mask to intial state */
	if (debug_level > 1)
		ereport(pg_hint_plan_message_level,
				(errhidestmt (true),
				 errmsg ("pg_hint_plan%s: get_relation_info"
						 " no hint applied:"
						 " relation=%u(%s), inhparent=%d, current_hint=%p,"
						 " hint_inhibit_level=%d, scanmask=0x%x",
						 qnostr, relationObjectId,
						 get_rel_name(relationObjectId),
						 inhparent, current_hint_state, hint_inhibit_level,
						 current_hint_state->init_scan_mask)));
	setup_scan_method_enforcement(NULL,	current_hint_state);
}

static void
pg_hint_plan_get_relation_info(PlannerInfo *root, Oid relationObjectId,
							   bool inhparent, RelOptInfo *rel)
{
	if (prev_get_relation_info)
		(*prev_get_relation_info) (root, relationObjectId, inhparent, rel);

	/* 
	 * Do nothing if we don't have a valid hint in this context or current
	 * nesting depth is at SPI calls.
	 */
	if (!current_hint_state || hint_inhibit_level > 0)
	{
		if (debug_level > 1)
			ereport(pg_hint_plan_message_level,
					(errhidestmt (true),
					 errmsg ("pg_hint_plan%s: get_relation_info"
							 " no hint to apply: relation=%u(%s), inhparent=%d,"
							 " current_hint_state=%p, hint_inhibit_level=%d",
							 qnostr, relationObjectId,
							 get_rel_name(relationObjectId),
							 inhparent, current_hint_state, hint_inhibit_level)));
		return;
	}

	setup_hint_enforcement(root, relationObjectId, inhparent, rel);

	return;
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
				hint_ereport(str,
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

	join_hint = current_hint_state->join_hint_level[bms_num_members(joinrelids)];

	foreach(l, join_hint)
	{
		JoinMethodHint *hint = (JoinMethodHint *) lfirst(l);

		if (bms_equal(joinrelids, hint->joinrelids))
			return hint;
	}

	return NULL;
}

static Relids
OuterInnerJoinCreate(OuterInnerRels *outer_inner, LeadingHint *leading_hint,
	PlannerInfo *root, List *initial_rels, HintState *hstate, int nbaserel)
{
	OuterInnerRels *outer_rels;
	OuterInnerRels *inner_rels;
	Relids			outer_relids;
	Relids			inner_relids;
	Relids			join_relids;
	JoinMethodHint *hint;

	if (outer_inner->relation != NULL)
	{
		return bms_make_singleton(
					find_relid_aliasname(root, outer_inner->relation,
										 initial_rels,
										 leading_hint->base.hint_str));
	}

	outer_rels = lfirst(outer_inner->outer_inner_pair->head);
	inner_rels = lfirst(outer_inner->outer_inner_pair->tail);

	outer_relids = OuterInnerJoinCreate(outer_rels,
										leading_hint,
										root,
										initial_rels,
										hstate,
										nbaserel);
	inner_relids = OuterInnerJoinCreate(inner_rels,
										leading_hint,
										root,
										initial_rels,
										hstate,
										nbaserel);

	join_relids = bms_add_members(outer_relids, inner_relids);

	if (bms_num_members(join_relids) > nbaserel)
		return join_relids;

	/*
	 * If we don't have join method hint, create new one for the
	 * join combination with all join methods are enabled.
	 */
	hint = find_join_hint(join_relids);
	if (hint == NULL)
	{
		/*
		 * Here relnames is not set, since Relids bitmap is sufficient to
		 * control paths of this query afterward.
		 */
		hint = (JoinMethodHint *) JoinMethodHintCreate(
					leading_hint->base.hint_str,
					HINT_LEADING,
					HINT_KEYWORD_LEADING);
		hint->base.state = HINT_STATE_USED;
		hint->nrels = bms_num_members(join_relids);
		hint->enforce_mask = ENABLE_ALL_JOIN;
		hint->joinrelids = bms_copy(join_relids);
		hint->inner_nrels = bms_num_members(inner_relids);
		hint->inner_joinrelids = bms_copy(inner_relids);

		hstate->join_hint_level[hint->nrels] =
			lappend(hstate->join_hint_level[hint->nrels], hint);
	}
	else
	{
		hint->inner_nrels = bms_num_members(inner_relids);
		hint->inner_joinrelids = bms_copy(inner_relids);
	}

	return join_relids;
}

static Relids
create_bms_of_relids(Hint *base, PlannerInfo *root, List *initial_rels,
		int nrels, char **relnames)
{
	int		relid;
	Relids	relids = NULL;
	int		j;
	char   *relname;

	for (j = 0; j < nrels; j++)
	{
		relname = relnames[j];

		relid = find_relid_aliasname(root, relname, initial_rels,
									 base->hint_str);

		if (relid == -1)
			base->state = HINT_STATE_ERROR;

		/*
		 * the aliasname is not found(relid == 0) or same aliasname was used
		 * multiple times in a query(relid == -1)
		 */
		if (relid <= 0)
		{
			relids = NULL;
			break;
		}
		if (bms_is_member(relid, relids))
		{
			hint_ereport(base->hint_str,
						 ("Relation name \"%s\" is duplicated.", relname));
			base->state = HINT_STATE_ERROR;
			break;
		}

		relids = bms_add_member(relids, relid);
	}
	return relids;
}
/*
 * Transform join method hint into handy form.
 *
 *   - create bitmap of relids from alias names, to make it easier to check
 *     whether a join path matches a join method hint.
 *   - add join method hints which are necessary to enforce join order
 *     specified by Leading hint
 */
static bool
transform_join_hints(HintState *hstate, PlannerInfo *root, int nbaserel,
		List *initial_rels, JoinMethodHint **join_method_hints)
{
	int				i;
	int				relid;
	Relids			joinrelids;
	int				njoinrels;
	ListCell	   *l;
	char		   *relname;
	LeadingHint	   *lhint = NULL;

	/*
	 * Create bitmap of relids from alias names for each join method hint.
	 * Bitmaps are more handy than strings in join searching.
	 */
	for (i = 0; i < hstate->num_hints[HINT_TYPE_JOIN_METHOD]; i++)
	{
		JoinMethodHint *hint = hstate->join_hints[i];

		if (!hint_state_enabled(hint) || hint->nrels > nbaserel)
			continue;

		hint->joinrelids = create_bms_of_relids(&(hint->base), root,
									 initial_rels, hint->nrels, hint->relnames);

		if (hint->joinrelids == NULL || hint->base.state == HINT_STATE_ERROR)
			continue;

		hstate->join_hint_level[hint->nrels] =
			lappend(hstate->join_hint_level[hint->nrels], hint);
	}

	/*
	 * Create bitmap of relids from alias names for each rows hint.
	 * Bitmaps are more handy than strings in join searching.
	 */
	for (i = 0; i < hstate->num_hints[HINT_TYPE_ROWS]; i++)
	{
		RowsHint *hint = hstate->rows_hints[i];

		if (!hint_state_enabled(hint) || hint->nrels > nbaserel)
			continue;

		hint->joinrelids = create_bms_of_relids(&(hint->base), root,
									 initial_rels, hint->nrels, hint->relnames);
	}

	/* Do nothing if no Leading hint was supplied. */
	if (hstate->num_hints[HINT_TYPE_LEADING] == 0)
		return false;

	/*
	 * Decide whether to use Leading hint
 	 */
	for (i = 0; i < hstate->num_hints[HINT_TYPE_LEADING]; i++)
	{
		LeadingHint	   *leading_hint = (LeadingHint *)hstate->leading_hint[i];
		Relids			relids;

		if (leading_hint->base.state == HINT_STATE_ERROR)
			continue;

		relid = 0;
		relids = NULL;

		foreach(l, leading_hint->relations)
		{
			relname = (char *)lfirst(l);;

			relid = find_relid_aliasname(root, relname, initial_rels,
										 leading_hint->base.hint_str);
			if (relid == -1)
				leading_hint->base.state = HINT_STATE_ERROR;

			if (relid <= 0)
				break;

			if (bms_is_member(relid, relids))
			{
				hint_ereport(leading_hint->base.hint_str,
							 ("Relation name \"%s\" is duplicated.", relname));
				leading_hint->base.state = HINT_STATE_ERROR;
				break;
			}

			relids = bms_add_member(relids, relid);
		}

		if (relid <= 0 || leading_hint->base.state == HINT_STATE_ERROR)
			continue;

		if (lhint != NULL)
		{
			hint_ereport(lhint->base.hint_str,
				 ("Conflict %s hint.", HintTypeName[lhint->base.type]));
			lhint->base.state = HINT_STATE_DUPLICATION;
		}
		leading_hint->base.state = HINT_STATE_USED;
		lhint = leading_hint;
	}

	/* check to exist Leading hint marked with 'used'. */
	if (lhint == NULL)
		return false;

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
	if (lhint->outer_inner == NULL)
	{
		foreach(l, lhint->relations)
		{
			JoinMethodHint *hint;

			relname = (char *)lfirst(l);

			/*
			 * Find relid of the relation which has given name.  If we have the
			 * name given in Leading hint multiple times in the join, nothing to
			 * do.
			 */
			relid = find_relid_aliasname(root, relname, initial_rels,
										 hstate->hint_str);

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
				 * Here relnames is not set, since Relids bitmap is sufficient
				 * to control paths of this query afterward.
				 */
				hint = (JoinMethodHint *) JoinMethodHintCreate(
											lhint->base.hint_str,
											HINT_LEADING,
											HINT_KEYWORD_LEADING);
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
			return false;

		/*
		 * Delete all join hints which have different combination from Leading
		 * hint.
		 */
		for (i = 2; i <= njoinrels; i++)
		{
			list_free(hstate->join_hint_level[i]);

			hstate->join_hint_level[i] = lappend(NIL, join_method_hints[i]);
		}
	}
	else
	{
		joinrelids = OuterInnerJoinCreate(lhint->outer_inner,
										  lhint,
                                          root,
                                          initial_rels,
										  hstate,
										  nbaserel);

		njoinrels = bms_num_members(joinrelids);
		Assert(njoinrels >= 2);

		/*
		 * Delete all join hints which have different combination from Leading
		 * hint.
		 */
		for (i = 2;i <= njoinrels; i++)
		{
			if (hstate->join_hint_level[i] != NIL)
			{
				ListCell *prev = NULL;
				ListCell *next = NULL;
				for(l = list_head(hstate->join_hint_level[i]); l; l = next)
				{

					JoinMethodHint *hint = (JoinMethodHint *)lfirst(l);

					next = lnext(l);

					if (hint->inner_nrels == 0 &&
						!(bms_intersect(hint->joinrelids, joinrelids) == NULL ||
						  bms_equal(bms_union(hint->joinrelids, joinrelids),
						  hint->joinrelids)))
					{
						hstate->join_hint_level[i] =
							list_delete_cell(hstate->join_hint_level[i], l,
											 prev);
					}
					else
						prev = l;
				}
			}
		}

		bms_free(joinrelids);
	}

	if (hint_state_enabled(lhint))
	{
		set_join_config_options(DISABLE_ALL_JOIN, current_hint_state->context);
		return true;
	}
	return false;
}

/*
 * set_plain_rel_pathlist
 *	  Build access paths for a plain relation (no subquery, no inheritance)
 *
 * This function was copied and edited from set_plain_rel_pathlist() in
 * src/backend/optimizer/path/allpaths.c
 *
 * - removed parallel stuff.
 */
static void
set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Relids		required_outer;

	/*
	 * We don't support pushing join clauses into the quals of a seqscan, but
	 * it could still have required parameterization due to LATERAL refs in
	 * its tlist.
	 */
	required_outer = rel->lateral_relids;

	/* Consider sequential scan */
	add_path(rel, create_seqscan_path(root, rel, required_outer, 0));

	/* If appropriate, consider parallel sequential scan */
	if (rel->consider_parallel && required_outer == NULL)
	{
		ParallelHint *phint = find_parallel_hint(root, rel->relid, rel);

		/* Consider parallel paths only if not inhibited by hint */
		if (!phint || phint->nworkers > 0)
			create_plain_partial_paths(root, rel);

		/*
		 * Overwirte parallel_workers if requested. partial_pathlist seems to
		 * have up to one path but looping over all possible paths don't harm.
		 */
		if (phint && phint->nworkers > 0 && phint->force_parallel)
		{
			ListCell *l;
			foreach (l, rel->partial_pathlist)
			{
				Path *ppath = (Path *) lfirst(l);
				
				Assert(ppath->parallel_workers > 0);
				ppath->parallel_workers	= phint->nworkers;
			}
		}			
	}

	/* Consider index scans */
	create_index_paths(root, rel);

	/* Consider TID scans */
	create_tidscan_paths(root, rel);
}

/*
 * Clear exiting access paths and create new ones applying hints.
 * This does the similar to set_rel_pathlist
 */
static void
rebuild_scan_path(HintState *hstate, PlannerInfo *root, int level,
				  List *initial_rels)
{
	ListCell   *l;

	foreach(l, initial_rels)
	{
		RelOptInfo	   *rel = (RelOptInfo *) lfirst(l);
		RangeTblEntry  *rte;

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
		setup_scan_method_enforcement(find_scan_hint(root, rel->relid, rel),
									  hstate);

		/* Setup parallel environment according to the hint */
		setup_parallel_plan_enfocement(
			find_parallel_hint(root, rel->relid, rel), current_hint_state);

		/* remove existing partial paths from this baserel */
		list_free_deep(rel->partial_pathlist);
		rel->partial_pathlist = NIL;

		/* remove existing paths from this baserel */
		list_free_deep(rel->pathlist);
		rel->pathlist = NIL;

		if (rte->inh)
		{
			ListCell *l;

			/* remove partial paths from all chlidren */
			foreach (l, root->append_rel_list)
			{
				AppendRelInfo  *appinfo = (AppendRelInfo *) lfirst(l);
				RelOptInfo	   *childrel;

				if (appinfo->parent_relid != rel->relid)
					continue;

				childrel = root->simple_rel_array[appinfo->child_relid];
				list_free_deep(childrel->partial_pathlist);
				childrel->partial_pathlist = NIL;
			}
			/* It's an "append relation", process accordingly */
			set_append_rel_pathlist(root, rel, rel->relid, rte);
		}
		else
		{
			set_plain_rel_pathlist(root, rel, rte);
		}

		/*
		 * If this is a baserel, consider gathering any partial paths we may
		 * hinthave created for it.
		 */
		if (rel->reloptkind == RELOPT_BASEREL)
			generate_gather_paths(root, rel);

		/* Now find the cheapest of the paths for this rel */
		set_cheapest(rel);
	}

	/*
	 * Restore the GUC variables we set above.
	 */
	setup_scan_method_enforcement(NULL, hstate);
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

	if (hint->inner_nrels == 0)
	{
		save_nestlevel = NewGUCNestLevel();

		set_join_config_options(hint->enforce_mask,
								current_hint_state->context);

		rel = pg_hint_plan_make_join_rel(root, rel1, rel2);
		hint->base.state = HINT_STATE_USED;

		/*
		 * Restore the GUC variables we set above.
		 */
		AtEOXact_GUC(true, save_nestlevel);
	}
	else
		rel = pg_hint_plan_make_join_rel(root, rel1, rel2);

	return rel;
}

/*
 * TODO : comment
 */
static void
add_paths_to_joinrel_wrapper(PlannerInfo *root,
							 RelOptInfo *joinrel,
							 RelOptInfo *outerrel,
							 RelOptInfo *innerrel,
							 JoinType jointype,
							 SpecialJoinInfo *sjinfo,
							 List *restrictlist)
{
	ScanMethodHint *scan_hint = NULL;
	Relids			joinrelids;
	JoinMethodHint *join_hint;
	int				save_nestlevel;

	if ((scan_hint = find_scan_hint(root, innerrel->relid, innerrel)) != NULL)
		setup_scan_method_enforcement(scan_hint, current_hint_state);

	joinrelids = bms_union(outerrel->relids, innerrel->relids);
	join_hint = find_join_hint(joinrelids);
	bms_free(joinrelids);

	if (join_hint && join_hint->inner_nrels != 0)
	{
		save_nestlevel = NewGUCNestLevel();

		if (bms_equal(join_hint->inner_joinrelids, innerrel->relids))
		{

			set_join_config_options(join_hint->enforce_mask,
									current_hint_state->context);

			add_paths_to_joinrel(root, joinrel, outerrel, innerrel, jointype,
								 sjinfo, restrictlist);
			join_hint->base.state = HINT_STATE_USED;
		}
		else
		{
			set_join_config_options(DISABLE_ALL_JOIN,
									current_hint_state->context);
			add_paths_to_joinrel(root, joinrel, outerrel, innerrel, jointype,
								 sjinfo, restrictlist);
		}

		/*
		 * Restore the GUC variables we set above.
		 */
		AtEOXact_GUC(true, save_nestlevel);
	}
	else
		add_paths_to_joinrel(root, joinrel, outerrel, innerrel, jointype,
							 sjinfo, restrictlist);

	/* Reset the environment if changed */
	if (scan_hint != NULL)
		setup_scan_method_enforcement(NULL, current_hint_state);
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
	JoinMethodHint	  **join_method_hints;
	int					nbaserel;
	RelOptInfo		   *rel;
	int					i;
	bool				leading_hint_enable;

	/*
	 * Use standard planner (or geqo planner) if pg_hint_plan is disabled or no
	 * valid hint is supplied or current nesting depth is nesting depth of SPI
	 * calls.
	 */
	if (!current_hint_state || hint_inhibit_level > 0)
	{
		if (prev_join_search)
			return (*prev_join_search) (root, levels_needed, initial_rels);
		else if (enable_geqo && levels_needed >= geqo_threshold)
			return geqo(root, levels_needed, initial_rels);
		else
			return standard_join_search(root, levels_needed, initial_rels);
	}

	/* apply scan method hint rebuild scan path. */
	rebuild_scan_path(current_hint_state, root, levels_needed, initial_rels);

	/*
	 * In the case using GEQO, only scan method hints and Set hints have
	 * effect.  Join method and join order is not controllable by hints.
	 */
	if (enable_geqo && levels_needed >= geqo_threshold)
		return geqo(root, levels_needed, initial_rels);

	nbaserel = get_num_baserels(initial_rels);
	current_hint_state->join_hint_level =
		palloc0(sizeof(List *) * (nbaserel + 1));
	join_method_hints = palloc0(sizeof(JoinMethodHint *) * (nbaserel + 1));

	leading_hint_enable = transform_join_hints(current_hint_state,
											   root, nbaserel,
											   initial_rels, join_method_hints);

	rel = pg_hint_plan_standard_join_search(root, levels_needed, initial_rels);

	for (i = 2; i <= nbaserel; i++)
	{
		list_free(current_hint_state->join_hint_level[i]);

		/* free Leading hint only */
		if (join_method_hints[i] != NULL &&
			join_method_hints[i]->enforce_mask == ENABLE_ALL_JOIN)
			JoinMethodHintDelete(join_method_hints[i]);
	}
	pfree(current_hint_state->join_hint_level);
	pfree(join_method_hints);

	if (leading_hint_enable)
		set_join_config_options(current_hint_state->init_join_mask,
								current_hint_state->context);

	return rel;
}

/*
 * Force number of wokers if instructed by hint
 */
void
pg_hint_plan_set_rel_pathlist(PlannerInfo * root, RelOptInfo *rel,
							  Index rti, RangeTblEntry *rte)
{
	ParallelHint   *phint;
	List		   *oldpathlist;
	ListCell	   *l;
	bool			regenerate = false;

	/* Hint has not been parsed yet, or this is not a parallel'able relation */
	if (current_hint_state == NULL || rel->partial_pathlist == NIL)
		return;

	phint = find_parallel_hint(root, rel->relid, rel);

	if (phint == NULL || !phint->force_parallel)
		return;

	/*
	 * This relation contains gather paths previously created and they prevent
	 * adding new gahter path with same cost. Remove them.
	 */
	oldpathlist = rel->pathlist;
	rel->pathlist = NIL;
	foreach (l, oldpathlist)
	{
		Path *path = (Path *) lfirst(l);

		if (IsA(path, GatherPath) &&
			path->parallel_workers != phint->nworkers)
		{
			pfree(path);
			regenerate = true;
		}
		else
			rel->pathlist = lappend(rel->pathlist, path);
	}
	list_free(oldpathlist);

	if (regenerate)
	{
		foreach (l, rel->partial_pathlist)
		{
			Path *ppath = (Path *) lfirst(l);
			
			if (phint && phint->nworkers > 0 && phint->force_parallel)
			{
				Assert(ppath->parallel_workers > 0);
				ppath->parallel_workers	= phint->nworkers;
			}			
		}
		
		generate_gather_paths(root, rel);
	}
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
	if (IS_DUMMY_REL(rel))
	{
		/* We already proved the relation empty, so nothing more to do */
	}
	else if (rte->inh)
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
				if(rte->tablesample != NULL)
					elog(ERROR, "sampled relation is not supported");

				/* Plain relation */
				set_plain_rel_pathlist(root, rel, rte);
			}
			else
				elog(ERROR, "unexpected relkind: %c", rte->relkind);
		}
		else
			elog(ERROR, "unexpected rtekind: %d", (int) rel->rtekind);
	}

	/*
	 * Allow a plugin to editorialize on the set of Paths for this base
	 * relation.  It could add new paths (such as CustomPaths) by calling
	 * add_path(), or delete or modify paths added by the core code.
	 */
	if (set_rel_pathlist_hook)
		(*set_rel_pathlist_hook) (root, rel, rti, rte);

	/* Now find the cheapest of the paths for this rel */
	set_cheapest(rel);
}

/*
 * stmt_beg callback is called when each query in PL/pgSQL function is about
 * to be executed.  At that timing, we save query string in the global variable
 * plpgsql_query_string to use it in planner hook.  It's safe to use one global
 * variable for the purpose, because its content is only necessary until
 * planner hook is called for the query, so recursive PL/pgSQL function calls
 * don't harm this mechanism.
 */
static void
pg_hint_plan_plpgsql_stmt_beg(PLpgSQL_execstate *estate, PLpgSQL_stmt *stmt)
{
	plpgsql_recurse_level++;
}

/*
 * stmt_end callback is called then each query in PL/pgSQL function has
 * finished.  At that timing, we clear plpgsql_query_string to tell planner
 * hook that next call is not for a query written in PL/pgSQL block.
 */
static void
pg_hint_plan_plpgsql_stmt_end(PLpgSQL_execstate *estate, PLpgSQL_stmt *stmt)
{
	plpgsql_recurse_level--;
}

void plpgsql_query_erase_callback(ResourceReleasePhase phase,
								  bool isCommit,
								  bool isTopLevel,
								  void *arg)
{
	if (phase != RESOURCE_RELEASE_AFTER_LOCKS)
		return;
	/* Cancel plpgsql nest level*/
	plpgsql_recurse_level = 0;
}

#define standard_join_search pg_hint_plan_standard_join_search
#define join_search_one_level pg_hint_plan_join_search_one_level
#define make_join_rel make_join_rel_wrapper
#include "core.c"

#undef make_join_rel
#define make_join_rel pg_hint_plan_make_join_rel
#define add_paths_to_joinrel add_paths_to_joinrel_wrapper
#include "make_join_rel.c"

#include "pg_stat_statements.c"

/*-------------------------------------------------------------------------
 *
 * pg_hint_plan.c
 *		  hinting on how to execute a query for PostgreSQL
 *
 * Copyright (c) 2012-2023, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */
#include <string.h>

#include "postgres.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/relation.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_index.h"
#include "commands/prepare.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "nodes/params.h"
#include "optimizer/appendinfo.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/geqo.h"
#include "optimizer/joininfo.h"
#include "optimizer/optimizer.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/plancat.h"
#include "optimizer/planner.h"
#include "optimizer/prep.h"
#include "optimizer/restrictinfo.h"
#include "parser/analyze.h"
#include "parser/parsetree.h"
#include "parser/scansup.h"
#include "partitioning/partbounds.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/float.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
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

/* YB includes */
#include "catalog/namespace.h"
#include "common/hashfn.h"
#include "pg_yb_utils.h"
#include "utils/catcache.h"
#include "utils/inval.h"
#include "utils/plancache.h"
#include "yb/yql/pggate/ybc_pggate.h"

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
#define HINT_BATCHEDNL			"YbBatchedNL"
#define HINT_MERGEJOIN			"MergeJoin"
#define HINT_HASHJOIN			"HashJoin"
#define HINT_NONESTLOOP			"NoNestLoop"
#define HINT_NOBATCHEDNL		"NoYbBatchedNL"
#define HINT_NOMERGEJOIN		"NoMergeJoin"
#define HINT_NOHASHJOIN			"NoHashJoin"
#define HINT_LEADING			"Leading"
#define HINT_SET				"Set"
#define HINT_ROWS				"Rows"
#define HINT_MEMOIZE			"Memoize"
#define HINT_NOMEMOIZE			"NoMemoize"

#define HINT_ARRAY_DEFAULT_INITSIZE 8

#define hint_ereport(str, detail) hint_parse_ereport(str, detail)
#define hint_parse_ereport(str, detail) \
	do { \
		ereport(pg_hint_plan_parse_message_level,		\
			(errmsg("pg_hint_plan: hint syntax error at or near \"%s\"", (str)), \
			 errdetail detail)); \
	} while(0)

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
	ENABLE_HASHJOIN = 0x04,
	ENABLE_MEMOIZE	= 0x08,
	ENABLE_BATCHEDNL = 0x10
} JOIN_TYPE_BITS;

#define ENABLE_ALL_SCAN (ENABLE_SEQSCAN | ENABLE_INDEXSCAN | \
						 ENABLE_BITMAPSCAN | ENABLE_TIDSCAN | \
						 ENABLE_INDEXONLYSCAN)
#define ENABLE_ALL_JOIN (ENABLE_NESTLOOP | ENABLE_MERGEJOIN | \
								 ENABLE_HASHJOIN | ENABLE_BATCHEDNL)
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
	HINT_KEYWORD_BATCHEDNL,
	HINT_KEYWORD_MERGEJOIN,
	HINT_KEYWORD_HASHJOIN,
	HINT_KEYWORD_NONESTLOOP,
	HINT_KEYWORD_NOBATCHEDNL,
	HINT_KEYWORD_NOMERGEJOIN,
	HINT_KEYWORD_NOHASHJOIN,

	HINT_KEYWORD_LEADING,
	HINT_KEYWORD_SET,
	HINT_KEYWORD_ROWS,
	HINT_KEYWORD_PARALLEL,
	HINT_KEYWORD_MEMOIZE,
	HINT_KEYWORD_NOMEMOIZE,

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
typedef enum HintType
{
	HINT_TYPE_SCAN_METHOD,
	HINT_TYPE_JOIN_METHOD,
	HINT_TYPE_LEADING,
	HINT_TYPE_SET,
	HINT_TYPE_ROWS,
	HINT_TYPE_PARALLEL,
	HINT_TYPE_MEMOIZE,

	NUM_HINT_TYPE
} HintType;

typedef enum HintTypeBitmap
{
	HINT_BM_SCAN_METHOD = 1,
	HINT_BM_PARALLEL = 2
} HintTypeBitmap;

static const char *HintTypeName[] = {
	"scan method",
	"join method",
	"leading",
	"set",
	"rows",
	"parallel",
	"memoize"
};

StaticAssertDecl(sizeof(HintTypeName) / sizeof(char *) == NUM_HINT_TYPE,
				 "HintTypeName and HintType don't match");

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

/* These variables are used only when debug_level > 1*/
static unsigned int qno = 0;
static unsigned int msgqno = 0;
static char qnostr[32];

static const char *current_hint_str = NULL;

/*
 * We can utilize in-core generated jumble state in post_parse_analyze_hook.
 * On the other hand there's a case where we're forced to get hints in
 * planner_hook, where we don't have a jumble state.  If we a query had not a
 * hint, we need to try to retrieve hints twice or more for one query, which is
 * the quite common case.  To avoid such case, this variables is set true when
 * we *try* hint retrieval.
 */
static bool current_hint_retrieved = false;

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
	/* min_parallel_table_scan_size*/
	int				init_min_para_tablescan_size;
	/* min_parallel_index_scan_size*/
	int				init_min_para_indexscan_size;
	double			init_paratup_cost;	/* parallel_tuple_cost */
	double			init_parasetup_cost;/* parallel_setup_cost */

	PlannerInfo	   *current_root;		/* PlannerInfo for the followings */
	Index			parent_relid;		/* inherit parent of table relid */
	ScanMethodHint *parent_scan_hint;	/* scan hint for the parent */
	ParallelHint   *parent_parallel_hint; /* parallel hint for the parent */
	List		   *parent_index_infos; /* list of parent table's index */

	JoinMethodHint **join_hints;		/* parsed join hints */
	int				init_join_mask;		/* initial value join parameter */
	List		  **join_hint_level;
	List		  **memoize_hint_level;
	LeadingHint	  **leading_hint;		/* parsed Leading hints */
	SetHint		  **set_hints;			/* parsed Set hints */
	GucContext		context;			/* which GUC parameters can we set? */
	RowsHint	  **rows_hints;			/* parsed Rows hints */
	ParallelHint  **parallel_hints;		/* parsed Parallel hints */
	JoinMethodHint **memoize_hints;		/* parsed Memoize hints */
	bool			ybAnyHintFailed;	/* was any problem found with hinting? */
	/* all valid hint aliases */
	List   		   *ybAllAliasNamesInScope;
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

typedef enum YbBadHintMode
{
	BAD_HINT_OFF,
	BAD_HINT_WARN,
	BAD_HINT_REPLAN,
	BAD_HINT_ERROR,
	BAD_HINT_UNRECORNIZED
} YbBadHintMode;

/*
 * Hint cache.
 */
typedef struct YbHintCacheKey {
    const char *query_string;
    const char *application_name;
} YbHintCacheKey;

typedef struct YbHintCacheEntry {
    YbHintCacheKey key;
    const char *hints;
} YbHintCacheEntry;

/*
 * Separate memory context for hint cache so that we can easily clear the cache
 * using MemoryContextReset. Lives under CacheMemoryContext.
 */
static MemoryContext YbHintCacheCtx = NULL;
static HTAB *YbHintCache = NULL;
static Oid YbCachedHintRelationId = InvalidOid;

#define YB_HINT_ATTR_ID 1
#define YB_HINT_ATTR_QUERY_STRING 2
#define YB_HINT_ATTR_APPLICATION_NAME 3
#define YB_HINT_ATTR_HINTS 4

static bool enable_hint_table_check(bool *newval, void **extra, GucSource source);
static void assign_enable_hint_table(bool newval, void *extra);

/* Module callbacks */
void		_PG_init(void);
void		_PG_fini(void);

static void push_hint(HintState *hstate);
static void pop_hint(void);

static void pg_hint_plan_post_parse_analyze(ParseState *pstate, Query *query,
											JumbleState *jstate);
static PlannedStmt *pg_hint_plan_planner(Query *parse, const char *query_string,
										 int cursorOptions,
										 ParamListInfo boundParams);
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

static Hint *MemoizeHintCreate(const char *hint_str, const char *keyword,
							   HintKeyword hint_keyword);

static void quote_value(StringInfo buf, const char *value);

static const char *parse_quoted_value(const char *str, char **word,
									  bool truncate);
static char *ybCheckPlanForDisabledNodes(Plan *plan, PlannedStmt *plannedStmt);
static void ybCheckBadOrUnusedHints(PlannedStmt *result);
static void ybTraceLeadingHint(LeadingHint *leadingHint, char *msg);
RelOptInfo *pg_hint_plan_standard_join_search(PlannerInfo *root,
											  int levels_needed,
											  List *initial_rels);
void pg_hint_plan_join_search_one_level(PlannerInfo *root, int level);
void pg_hint_plan_set_rel_pathlist(PlannerInfo * root, RelOptInfo *rel,
								   Index rti, RangeTblEntry *rte);
static void create_plain_partial_paths(PlannerInfo *root,
									   RelOptInfo *rel);
static void make_rels_by_clause_joins(PlannerInfo *root, RelOptInfo *old_rel,
									  List *other_rels_list,
									  ListCell *other_rels);
static void make_rels_by_clauseless_joins(PlannerInfo *root,
										  RelOptInfo *old_rel,
										  List *other_rels);
static bool has_join_restriction(PlannerInfo *root, RelOptInfo *rel);
static void set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
								   RangeTblEntry *rte);
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
static int set_config_double_option(const char *name, double value,
									GucContext context);

static bool ybCheckValidPathForRelExists(PlannerInfo * root, RelOptInfo *rel);
static char *ybCheckBadIndexHintExists(PlannerInfo *root, RelOptInfo *rel);
extern void ybBuildRelidsString(PlannerInfo *root, Relids relids, StringInfoData *buf);
static void ybAddHintedJoin(PlannerInfo *root, Relids outer_relids, Relids inner_relids);
static void ybAddProhibitedJoin(PlannerInfo *root, NodeTag ybJoinTag, Relids join_relids);
static HintKeyword ybIsNegationHint(Hint *hint);
static NodeTag ybIsNegationJoinHint(Hint *hint);

static HTAB *yb_init_hint_cache(void);
static uint32 yb_hint_hash_fn(const void *key, Size keysize);
static int yb_hint_match_fn(const void *key1, const void *key2, Size keysize);
static const char *yb_get_cached_hints(const char *client_query, const char *client_application, bool *found);
static void yb_set_cached_hints(const char *client_query,
								const char *client_application,
								const char *hints,
								HTAB *hint_cache);
static void YbInvalidateHintCacheCallback(Datum argument, Oid relationId);
static void YbInvalidateHintCache(void);
static void YbRefreshHintCache(void);
static YbHintCacheEntry *YbTupleToHintCacheEntry(TupleDesc tupleDescriptor, HeapTuple heapTuple);

/* GUC variables */
static bool	pg_hint_plan_enable_hint = true;
static int debug_level = 0;
static int	pg_hint_plan_parse_message_level = INFO;
static int	pg_hint_plan_debug_message_level = LOG;
/* Default is off, to keep backward compatibility. */
static bool	pg_hint_plan_enable_hint_table = false;
static bool	pg_hint_plan_hints_anywhere = false;
static int yb_bad_hint_mode = BAD_HINT_OFF;
static bool yb_enable_internal_hint_test = false;
static bool yb_internal_hint_test_fail = false;
static bool yb_use_generated_hints_for_plan = false;
static bool yb_use_query_id_for_hinting = false;
static bool yb_enable_hint_table_cache = true;

static int plpgsql_recurse_level = 0;		/* PLpgSQL recursion level            */
static int recurse_level = 0;		/* recursion level incl. direct SPI calls */
static int hint_inhibit_level = 0;			/* Inhibit hinting if this is above 0 */
											/* (This could not be above 1)        */
static int max_hint_nworkers = -1;		/* Maximum nworkers of Workers hints */

static bool	hint_table_deactivated = false;

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

static const struct config_enum_entry parse_yb_bad_hint_mode[] = {
	{"off", BAD_HINT_OFF, false},
	{"warn", BAD_HINT_WARN, false},
	{"replan", BAD_HINT_REPLAN, false},
	{"error", BAD_HINT_ERROR, false},
	{NULL, 0, false}
};

/* Saved hook values in case of unload */
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
static planner_hook_type prev_planner = NULL;
static join_search_hook_type prev_join_search = NULL;
static set_rel_pathlist_hook_type prev_set_rel_pathlist = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

/* Hold reference to currently active hint */
static HintState *current_hint_state = NULL;

/*
 * List of hint contexts.  We treat the head of the list as the Top of the
 * context stack, so current_hint_state always points the first element of this
 * list.
 */
static List *HintStateStack = NIL;

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
	{HINT_BATCHEDNL, JoinMethodHintCreate, HINT_KEYWORD_BATCHEDNL},
	{HINT_MERGEJOIN, JoinMethodHintCreate, HINT_KEYWORD_MERGEJOIN},
	{HINT_HASHJOIN, JoinMethodHintCreate, HINT_KEYWORD_HASHJOIN},
	{HINT_NONESTLOOP, JoinMethodHintCreate, HINT_KEYWORD_NONESTLOOP},
	{HINT_NOBATCHEDNL, JoinMethodHintCreate, HINT_KEYWORD_NOBATCHEDNL},
	{HINT_NOMERGEJOIN, JoinMethodHintCreate, HINT_KEYWORD_NOMERGEJOIN},
	{HINT_NOHASHJOIN, JoinMethodHintCreate, HINT_KEYWORD_NOHASHJOIN},

	{HINT_LEADING, LeadingHintCreate, HINT_KEYWORD_LEADING},
	{HINT_SET, SetHintCreate, HINT_KEYWORD_SET},
	{HINT_ROWS, RowsHintCreate, HINT_KEYWORD_ROWS},
	{HINT_PARALLEL, ParallelHintCreate, HINT_KEYWORD_PARALLEL},
	{HINT_MEMOIZE, MemoizeHintCreate, HINT_KEYWORD_MEMOIZE},
	{HINT_NOMEMOIZE, MemoizeHintCreate, HINT_KEYWORD_NOMEMOIZE},

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
 * pg_hint_ExecutorEnd
 *
 * Force a hint to be retrieved when we are at the top of a PL recursion
 * level.  This can become necessary to handle hints in queries executed
 * in the extended protocol, where the executor can be executed multiple
 * times in a portal, but it could be possible to fail the hint retrieval.
 */
static void
pg_hint_ExecutorEnd(QueryDesc *queryDesc)
{
	if (plpgsql_recurse_level <= 0)
		current_hint_retrieved = false;

	if (prev_ExecutorEnd)
		prev_ExecutorEnd(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);
}

/*
 * Module load callbacks
 */
void
_PG_init(void)
{
	PLpgSQL_plugin	**var_ptr;

	if (yb_enable_hint_table_cache)
		CacheRegisterRelcacheCallback(YbInvalidateHintCacheCallback, (Datum) 0);

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
							 &pg_hint_plan_parse_message_level,
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
							 &pg_hint_plan_debug_message_level,
							 LOG,
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
							 enable_hint_table_check,
							 assign_enable_hint_table,
							 NULL);

	DefineCustomBoolVariable("pg_hint_plan.hints_anywhere",
							 "Read hints from anywhere in a query.",
							 "This option lets pg_hint_plan ignore syntax so be cautious for false reads.",
							 &pg_hint_plan_hints_anywhere,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomEnumVariable("pg_hint_plan.yb_bad_hint_mode",
							"Level reflecting what action to take on bad hints.",
							NULL,
							&yb_bad_hint_mode,
							BAD_HINT_OFF,
							parse_yb_bad_hint_mode,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pg_hint_plan.yb_enable_internal_hint_test",
							"Internal test for generated hints.",
							NULL,
							&yb_enable_internal_hint_test,
							false,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pg_hint_plan.yb_internal_hint_test_fail",
							"Issue an error if generated hints do not give same plan.",
							NULL,
							&yb_internal_hint_test_fail,
							false,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pg_hint_plan.yb_use_generated_hints_for_plan",
							"Use plan from generated hints as the one to process the query.",
							NULL,
							&yb_use_generated_hints_for_plan,
							false,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pg_hint_plan.yb_use_query_id_for_hinting",
							"Use query id instead of query text for hinting.",
							NULL,
							&yb_use_query_id_for_hinting,
							false,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pg_hint_plan.yb_enable_hint_table_cache",
							"Enables per-session caching for the hint table.",
							NULL,
							&yb_enable_hint_table_cache,
							true,
							PGC_BACKEND,
							0,
							NULL,
							NULL,
							NULL);

	EmitWarningsOnPlaceholders("pg_hint_plan");

	/* Install hooks. */
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = pg_hint_plan_post_parse_analyze;
	prev_planner = planner_hook;
	planner_hook = pg_hint_plan_planner;
	prev_join_search = join_search_hook;
	join_search_hook = pg_hint_plan_join_search;
	prev_set_rel_pathlist = set_rel_pathlist_hook;
	set_rel_pathlist_hook = pg_hint_plan_set_rel_pathlist;
	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = pg_hint_ExecutorEnd;

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
	planner_hook = prev_planner;
	join_search_hook = prev_join_search;
	set_rel_pathlist_hook = prev_set_rel_pathlist;
	ExecutorEnd_hook = prev_ExecutorEnd;

	/* uninstall PL/pgSQL plugin hook */
	var_ptr = (PLpgSQL_plugin **) find_rendezvous_variable("PLpgSQL_plugin");
	*var_ptr = NULL;
}

static bool
enable_hint_table_check(bool *newval, void **extra, GucSource source)
{
	if (*newval)
	{
		EnableQueryId();

		if (!IsQueryIdEnabled())
		{
			GUC_check_errmsg("table hint is not activated because queryid is not available");
			GUC_check_errhint("Set compute_query_id to on or auto to use hint table.");
			return false;
		}
	}

	return true;
}

static void
assign_enable_hint_table(bool newval, void *extra)
{
	if (!newval)
		hint_table_deactivated = false;
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

	hint = palloc(sizeof(ParallelHint));
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


static Hint *
MemoizeHintCreate(const char *hint_str, const char *keyword,
				  HintKeyword hint_keyword)
{
	/*
	 * MemoizeHintCreate shares the same struct with JoinMethodHint and the
	 * only difference is the hint type.
	 */
	JoinMethodHint *hint =
		(JoinMethodHint *)JoinMethodHintCreate(hint_str, keyword, hint_keyword);

	hint->base.type = HINT_TYPE_MEMOIZE;

	return (Hint *) hint;
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
	hstate->init_min_para_tablescan_size = 0;
	hstate->init_min_para_indexscan_size = 0;
	hstate->init_paratup_cost = 0;
	hstate->init_parasetup_cost = 0;
	hstate->current_root = NULL;
	hstate->parent_relid = 0;
	hstate->parent_scan_hint = NULL;
	hstate->parent_parallel_hint = NULL;
	hstate->parent_index_infos = NIL;
	hstate->join_hints = NULL;
	hstate->init_join_mask = 0;
	hstate->join_hint_level = NULL;
	hstate->memoize_hint_level = NULL;
	hstate->leading_hint = NULL;
	hstate->context = superuser() ? PGC_SUSET : PGC_USERSET;
	hstate->set_hints = NULL;
	hstate->rows_hints = NULL;
	hstate->parallel_hints = NULL;
	hstate->ybAnyHintFailed = false;
	hstate->ybAllAliasNamesInScope = NIL;

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

	for (i = 0; i < hstate->nall_hints ; i++)
		hstate->all_hints[i]->delete_func(hstate->all_hints[i]);
	if (hstate->all_hints)
		pfree(hstate->all_hints);
	if (hstate->parent_index_infos)
		list_free(hstate->parent_index_infos);

	if (IsYugaByteEnabled())
	{
		list_free_deep(hstate->ybAllAliasNamesInScope);
	}

	/*
	 * We have another few or dozen of palloced block in the struct, but don't
	 * bother completely clean up all of them since they will be cleaned-up at
	 * the end of this query.
	 */
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
	int i;

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
	if (hint->rows_str != NULL)
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
		elog(pg_hint_plan_debug_message_level, "pg_hint_plan:\nno hint");
		return;
	}

	initStringInfo(&buf);

	appendStringInfoString(&buf, "pg_hint_plan:\n");
	desc_hint_in_state(hstate, &buf, "used hint", HINT_STATE_USED, false);
	desc_hint_in_state(hstate, &buf, "not used hint", HINT_STATE_NOTUSED, false);
	desc_hint_in_state(hstate, &buf, "duplication hint", HINT_STATE_DUPLICATION, false);
	desc_hint_in_state(hstate, &buf, "error hint", HINT_STATE_ERROR, false);

	ereport(pg_hint_plan_debug_message_level,
			(errmsg ("%s", buf.data)));

	pfree(buf.data);
}

static void
HintStateDump2(HintState *hstate)
{
	StringInfoData	buf;

	if (!hstate)
	{
		elog(pg_hint_plan_debug_message_level,
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

	ereport(pg_hint_plan_debug_message_level,
			(errmsg("%s", buf.data),
			 errhidestmt(true),
			 errhidecontext(true)));

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
ybTraceOuterInnerRels(OuterInnerRels *outer_inner_rels, StringInfoData *buf, int indent)
{
	appendStringInfoSpaces(buf, indent);

	appendStringInfo(buf, "relation : %s\n", (outer_inner_rels->relation != NULL ? outer_inner_rels->relation : "<null>"));
	appendStringInfoSpaces(buf, indent);
	appendStringInfoString(buf, "outer-inner pairs : ");

	if (outer_inner_rels->outer_inner_pair == NULL)
	{
		appendStringInfoString(buf, "<null>\n");
	}
	else
	{
		appendStringInfoString(buf, "\n");
		int cnt = 0;
		ListCell *lc;
		foreach(lc, outer_inner_rels->outer_inner_pair)
		{
			OuterInnerRels *outerInnerRels = (OuterInnerRels *) lfirst(lc);
			appendStringInfoSpaces(buf, indent + 2);
			appendStringInfo(buf, "Pair %d\n", cnt);
			ybTraceOuterInnerRels(outerInnerRels, buf, indent + 4);
			++cnt;
		}
	}
}

static void
ybTraceLeadingHint(LeadingHint *leadingHint, char *msg)
{
	StringInfoData buf;
	initStringInfo(&buf);

	if (msg != NULL)
	{
		appendStringInfo(&buf, "%s : \n  ", msg);
	}

	if (leadingHint == NULL)
	{
		appendStringInfoString(&buf, "<null>");
	}
	else
	{
		if (leadingHint->relations != NULL)
		{
			LeadingHintDesc(leadingHint, &buf, true);
		}
		else
		{
			appendStringInfoString(&buf, "<null>");
		}

		appendStringInfo(&buf, " (Used? : %s , Error? : %s)",
						 leadingHint->base.state == HINT_STATE_USED ? 
						 "true" : "false",
						 leadingHint->base.state == HINT_STATE_ERROR ? 
						 "true" : "false");

		OuterInnerRels *outerInnerRels = leadingHint->outer_inner;
		appendStringInfoString(&buf, "\n");
		appendStringInfoSpaces(&buf, 4);
		appendStringInfoString(&buf, "Outer-inner :\n");

		if (outerInnerRels != NULL)
		{
			ybTraceOuterInnerRels(outerInnerRels, &buf, 10);
		}
		else
		{
			appendStringInfoSpaces(&buf, 6);
			appendStringInfoString(&buf, "<null>");
		}
	}

	if (msg != NULL)
	{
		appendStringInfo(&buf, "\n%s", msg);
	}

	ereport(DEBUG1,
			(errmsg("%s", buf.data)));

	pfree(buf.data);
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

			if (pg_strcasecmp(buf.data, keyword) != 0)
				continue;

			hint = parser->create_func(head, keyword, parser->hint_keyword);

			/* parser of each hint does parse in a parenthesis. */
			if ((str = hint->parse_func(hint, hstate, parse, str)) == NULL)
			{
				if (IsYugaByteEnabled())
				{
					hstate->ybAnyHintFailed = true;
				}

				hint->delete_func(hint);
				pfree(buf.data);
				return;
			}

			if (IsYugaByteEnabled() && yb_enable_planner_trace)
			{
				if (hint->hint_keyword == HINT_KEYWORD_LEADING)
				{
					LeadingHint *lhint = (LeadingHint *) hint;
					ybTraceLeadingHint(lhint, "parse_hints : leading hint");
				}
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
			if (IsYugaByteEnabled())
			{
				hstate->ybAnyHintFailed = true;
			}

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
	char 	nulls[2] = {' ', ' '};
	text   *qry;
	text   *app;

	if (IsYugaByteEnabled() && yb_enable_hint_table_cache)
	{
		bool found;
		elog(DEBUG5, "Looking up hints cache for query: %s, application: '%s'", client_query, client_application);
		const char *cached_hints_app = yb_get_cached_hints(client_query, client_application, &found);
		if (found)
		{
			elog(DEBUG4, "Hint cache hit: query: %s, application: '%s', hints: %s", client_query, client_application, cached_hints_app);
			return cached_hints_app;
		}
		const char *cached_hints_empty = yb_get_cached_hints(client_query, "", &found);
		if (found)
		{
			elog(DEBUG4, "Hint cache hit: query: %s, application: '%s', hints: %s", client_query, "", cached_hints_empty);
			return cached_hints_empty;
		}
		elog(DEBUG4, "Hint cache miss for query: %s, application: '%s'", client_query, client_application);
		YbIncrementHintCacheMisses();
		return NULL;
	}

	PG_TRY();
	{
		bool snapshot_set = false;

		hint_inhibit_level++;

		if (!ActiveSnapshotSet())
		{
			PushActiveSnapshot(GetTransactionSnapshot());
			snapshot_set = true;
		}

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

		if (snapshot_set)
			PopActiveSnapshot();

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
 * Get hints from the head block comment in client-supplied query string.
 */
static const char *
get_hints_from_comment(const char *p, bool *ybHitError)
{
	const char *hint_head;
	char	   *head;
	char	   *tail;
	int			len;

	*ybHitError = false;

	if (p == NULL)
		return NULL;

	/* extract query head comment. */
	hint_head = strstr(p, HINT_START);
	if (hint_head == NULL)
		return NULL;
	if (!pg_hint_plan_hints_anywhere)
	{
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
			 *   - squared brackets, for arrays (like arguments of PREPARE
			 *     statements).
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
				*p != '(' && *p != ')' &&
				*p != '[' && *p != ']')
				{
					*ybHitError = true;
					return NULL;
				}
		}
	}

	head = (char *)hint_head;
	p = head + strlen(HINT_START);
	skip_space(p);

	/* find hint end keyword. */
	if ((tail = strstr(p, HINT_END)) == NULL)
	{
		hint_ereport(head, ("Unterminated block comment."));
		*ybHitError = true;
		return NULL;
	}

	/* We don't support nested block comments. */
	if ((head = strstr(p, BLOCK_COMMENT_START)) != NULL && head < tail)
	{
		hint_ereport(head, ("Nested block comments are not supported."));
		*ybHitError = true;
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
			if (IsYugaByteEnabled())
			{
				hstate->ybAnyHintFailed = true;
			}
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
	hstate->parallel_hints = (ParallelHint **) (hstate->rows_hints +
		hstate->num_hints[HINT_TYPE_ROWS]);
	hstate->memoize_hints = (JoinMethodHint **) (hstate->parallel_hints +
		hstate->num_hints[HINT_TYPE_PARALLEL]);

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

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}

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

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}

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

			if (IsYugaByteEnabled())
			{
				hstate->ybAnyHintFailed = true;
			}

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

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}

		return str;
	}

	/* Sort hints in alphabetical order of relation names. */
	qsort(hint->relnames, hint->nrels, sizeof(char *), RelnameCmp);

	switch (hint_keyword)
	{
		case HINT_KEYWORD_NESTLOOP:
			hint->enforce_mask = ENABLE_NESTLOOP;
			break;
		case HINT_KEYWORD_BATCHEDNL:
			hint->enforce_mask = ENABLE_BATCHEDNL;
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
		case HINT_KEYWORD_NOBATCHEDNL:
			hint->enforce_mask = ENABLE_ALL_JOIN ^ ENABLE_BATCHEDNL;
			break;
		case HINT_KEYWORD_NOMERGEJOIN:
			hint->enforce_mask = ENABLE_ALL_JOIN ^ ENABLE_MERGEJOIN;
			break;
		case HINT_KEYWORD_NOHASHJOIN:
			hint->enforce_mask = ENABLE_ALL_JOIN ^ ENABLE_HASHJOIN;
			break;
		case HINT_KEYWORD_MEMOIZE:
		case HINT_KEYWORD_NOMEMOIZE:
			/* nothing to do here */
			break;
		default:
			hint_ereport(str, ("Unrecognized hint keyword \"%s\".", keyword));

			if (IsYugaByteEnabled())
			{
				hstate->ybAnyHintFailed = true;
			}

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

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}
	}
	else if (hint->outer_inner != NULL &&
			 !OuterInnerPairCheck(hint->outer_inner))
	{
		hint_ereport(hint->base.hint_str,
					 ("%s hint requires two sets of relations when parentheses nests.",
					  HINT_LEADING));
		hint->base.state = HINT_STATE_ERROR;

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}
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

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}
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
	ListCell   *l;
	int			i = 0;

	if ((str = parse_parentheses(str, &name_list, hint_keyword)) == NULL)
		return NULL;

	/* Last element must be rows specification */
	hint->nrels = list_length(name_list) - 1;

	if (hint->nrels < 1)
	{
		hint_ereport(str,
					 ("%s hint needs at least one relation followed by one correction term.",
					  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}

		return str;
	}


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

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}

		return str;
	}
	hint->rows = strtod(rows_str, &end_ptr);
	if (*end_ptr)
	{
		hint_ereport(rows_str,
					 ("%s hint requires valid number as rows estimation.",
					  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}

		return str;
	}

	/* A join hint requires at least two relations */
	if (hint->nrels < 2)
	{
		hint_ereport(str,
					 ("%s hint requires at least two relations.",
					  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}

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
		hint_ereport(")",
					 ("wrong number of arguments (%d): %s",
					  length,  hint->base.keyword));
		hint->base.state = HINT_STATE_ERROR;

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}

		return str;
	}

	hint->relname = linitial(name_list);

	/* The second parameter is number of workers */
	hint->nworkers_str = list_nth(name_list, 1);
	nworkers = strtod(hint->nworkers_str, &end_ptr);
	if (*end_ptr || nworkers < 0 || nworkers > max_worker_processes)
	{
		if (*end_ptr)
			hint_ereport(hint->nworkers_str,
						 ("number of workers must be a number: %s",
						  hint->base.keyword));
		else if (nworkers < 0)
			hint_ereport(hint->nworkers_str,
						 ("number of workers must be greater than zero: %s",
						  hint->base.keyword));
		else if (nworkers > max_worker_processes)
			hint_ereport(hint->nworkers_str,
						 ("number of workers = %d is larger than max_worker_processes(%d): %s",
						  nworkers, max_worker_processes, hint->base.keyword));

		if (IsYugaByteEnabled())
		{
			hstate->ybAnyHintFailed = true;
		}

		hint->base.state = HINT_STATE_ERROR;
	}

	hint->nworkers = nworkers;

	/* optional third parameter is specified */
	if (length == 3)
	{
		const char *modeparam = (const char *)list_nth(name_list, 2);
		if (pg_strcasecmp(modeparam, "hard") == 0)
			force_parallel = true;
		else if (pg_strcasecmp(modeparam, "soft") != 0)
		{
			hint_ereport(modeparam,
						 ("enforcement must be soft or hard: %s",
							 hint->base.keyword));
			hint->base.state = HINT_STATE_ERROR;

			if (IsYugaByteEnabled())
			{
				hstate->ybAnyHintFailed = true;
			}
		}
	}

	hint->force_parallel = force_parallel;

	if (hint->base.state != HINT_STATE_ERROR &&
		nworkers > max_hint_nworkers)
		max_hint_nworkers = nworkers;

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
	if (enable_memoize)
		mask |= ENABLE_MEMOIZE;
	if (yb_enable_batchednl)
		mask |= ENABLE_BATCHEDNL;

	return mask;
}

/*
 * Sets GUC parameters without throwing exception. Returns false if something
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
				 errmsg("%s", errdata->message),
				 errdata->detail ? errdetail("%s", errdata->detail) : 0,
				 errdata->hint ? errhint("%s", errdata->hint) : 0));
		msgqno = qno;  /* Don't bother checking debug_level > 1*/
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
		ereport(pg_hint_plan_parse_message_level,
				(errmsg ("Failed to convert integer to string: %d", value)));
		return false;
	}

	return
		set_config_option_noerror(name, buf, context,
								  PGC_S_SESSION, GUC_ACTION_SAVE, true,
								  pg_hint_plan_parse_message_level);
}

/*
 * Sets GUC parameter of double type without throwing exceptions. Returns false
 * if something wrong.
 */
static int
set_config_double_option(const char *name, double value, GucContext context)
{
	char *buf = float8out_internal(value);
	int	  result;

	result = set_config_option_noerror(name, buf, context,
									   PGC_S_SESSION, GUC_ACTION_SAVE, true,
									   pg_hint_plan_parse_message_level);
	pfree(buf);
	return result;
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
										   pg_hint_plan_parse_message_level);
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
setup_parallel_plan_enforcement(ParallelHint *hint, HintState *state)
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
	if (hint && hint->force_parallel && hint->nworkers > 0)
	{
		set_config_double_option("parallel_tuple_cost", 0.0, state->context);
		set_config_double_option("parallel_setup_cost", 0.0, state->context);
		set_config_int32_option("min_parallel_table_scan_size", 0,
								state->context);
		set_config_int32_option("min_parallel_index_scan_size", 0,
								state->context);
	}
	else
	{
		set_config_double_option("parallel_tuple_cost",
								state->init_paratup_cost, state->context);
		set_config_double_option("parallel_setup_cost",
								state->init_parasetup_cost, state->context);
		set_config_int32_option("min_parallel_table_scan_size",
								state->init_min_para_tablescan_size,
								state->context);
		set_config_int32_option("min_parallel_index_scan_size",
								state->init_min_para_indexscan_size,
								state->context);
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
set_join_config_options(unsigned char enforce_mask, bool set_memoize,
						GucContext context)
{
	unsigned char	mask;

	if (enforce_mask == ENABLE_NESTLOOP || enforce_mask == ENABLE_MERGEJOIN ||
		enforce_mask == ENABLE_HASHJOIN || enforce_mask == ENABLE_BATCHEDNL)
		mask = enforce_mask;
	else
		mask = enforce_mask & current_hint_state->init_join_mask;

	SET_CONFIG_OPTION("enable_nestloop", ENABLE_NESTLOOP);
	SET_CONFIG_OPTION("enable_mergejoin", ENABLE_MERGEJOIN);
	SET_CONFIG_OPTION("enable_hashjoin", ENABLE_HASHJOIN);

	if (set_memoize)
		SET_CONFIG_OPTION("enable_memoize", ENABLE_MEMOIZE);

	SET_CONFIG_OPTION("yb_enable_batchednl", ENABLE_BATCHEDNL);

	/*
	 * Hash join may be rejected for the reason of estimated memory usage. Try
	 * getting rid of that limitation.
	 */
	if (enforce_mask == ENABLE_HASHJOIN)
	{
		char			buf[32];
		int				new_multipler;

		/* See final_cost_hashjoin(). */
		new_multipler = MAX_KILOBYTES / work_mem;

		/* See guc.c for the upper limit */
		if (new_multipler >= 1000)
			new_multipler = 1000;

		if (new_multipler > hash_mem_multiplier)
		{
			snprintf(buf, sizeof(buf), UINT64_FORMAT, (uint64)new_multipler);
			set_config_option_noerror("hash_mem_multiplier", buf,
									  context, PGC_S_SESSION, GUC_ACTION_SAVE,
									  true, ERROR);
		}
	}
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

/*
 * Retrieve and store hint string from given query or from the hint table.
 */
static void
get_current_hint_string(Query *query, const char *query_str,
						JumbleState *jstate)
{
	MemoryContext	oldcontext;

	/* We shouldn't get here for internal queries. */
	Assert (hint_inhibit_level == 0);

	/* We shouldn't get here if hint is disabled. */
	Assert (pg_hint_plan_enable_hint);

	/* Do not anything if we have already tried to get hints for this query. */
	if (current_hint_retrieved)
		return;

	/* No way to retrieve hints from empty string. */
	if (!query_str)
		return;

	/* Don't parse the current query hereafter */
	current_hint_retrieved = true;

	/* Make sure trashing old hint string */
	if (current_hint_str)
	{
		pfree((void *)current_hint_str);
		current_hint_str = NULL;
	}

	/* increment the query number */
	qnostr[0] = 0;
	if (debug_level > 1)
		snprintf(qnostr, sizeof(qnostr), "[qno=0x%x]", qno++);

	/* search the hint table for a hint if requested */
	if (pg_hint_plan_enable_hint_table)
	{
		int			query_len;
		char	   *normalized_query;

		if (!IsQueryIdEnabled())
		{
			/*
			 * compute_query_id was turned off while enable_hint_table is
			 * on. Do not go ahead and complain once until it is turned on
			 * again.
			 */
			if (!hint_table_deactivated)
				ereport(WARNING,
						(errmsg ("hint table feature is deactivated because queryid is not available"),
						 errhint("Set compute_query_id to \"auto\" or \"on\" to use hint table.")));

			hint_table_deactivated = true;
			return;
		}

		if (hint_table_deactivated)
		{
			ereport(LOG,
					(errmsg ("hint table feature is reactivated")));
			hint_table_deactivated = false;
		}

		if (!yb_use_query_id_for_hinting || query->queryId == 0)
		{
			/*
			 * If we are not using query ids for hinting, or the query id has
			 * not been set, then compute the query id. This is the path
			 * the original code always takes. If we are using query ids
			 * for hinting, and the query id has already been computed, do not
			 * recompute it in case changes have been nade to the query since
			 * the original computation.
			 */
			if (!jstate)
				jstate = JumbleQuery(query, query_str);

			if (!jstate)
				return;

			/*
			 * Normalize the query string by replacing constants with '?'
			 */
			/*
			 * Search hint string which is stored keyed by query string
			 * and application name.  The query string is normalized to allow
			 * fuzzy matching.
			 *
			 * Adding 1 byte to query_len ensures that the returned string has
			 * a terminating NULL.
			 */
			query_len = strlen(query_str) + 1;
			normalized_query =
				generate_normalized_query(jstate, query_str, 0, &query_len);
		}

		if (IsYugaByteEnabled() && yb_use_query_id_for_hinting)
		{
			/*
			 * Put the query id in a string as a signed 64-bit int since that is
			 * what shows in EXPLAIN.
			 */
			StringInfoData buf;
			initStringInfo(&buf);
			appendStringInfo(&buf, INT64_FORMAT, query->queryId);
			normalized_query = pstrdup(buf.data);

			if (IsYugaByteEnabled() && yb_enable_planner_trace)
			{
				ereport(DEBUG1,
						(errmsg("\nhinting query: %s , id = %ld",
								query_str != NULL ? query_str : "<null>", 
								query->queryId)));
			}

			pfree(buf.data);
		}

		/*
		 * find a hint for the normalized query. the result should be in
		 * TopMemoryContext
		 */
		oldcontext = MemoryContextSwitchTo(TopMemoryContext);
		current_hint_str =
			get_hints_from_table(normalized_query, application_name);
		MemoryContextSwitchTo(oldcontext);

		if (IsYugaByteEnabled() && yb_enable_planner_trace &&
			yb_use_query_id_for_hinting && current_hint_str != NULL)
		{
			ereport(DEBUG1,
					(errmsg("\nfound hints for query id %s in hint table : %s",
							normalized_query, current_hint_str)));
		}

		if (debug_level > 1)
		{
			if (current_hint_str)
				ereport(pg_hint_plan_debug_message_level,
						(errmsg("pg_hint_plan[qno=0x%x]: "
								"hints from table: \"%s\": "
								"normalized_query=\"%s\", "
								"application name =\"%s\"",
								qno, current_hint_str,
								normalized_query, application_name),
						 errhidestmt(msgqno != qno),
						 errhidecontext(msgqno != qno)));
			else
				ereport(pg_hint_plan_debug_message_level,
						(errmsg("pg_hint_plan[qno=0x%x]: "
								"no match found in table:  "
								"application name = \"%s\", "
								"normalized_query=\"%s\"",
								qno, application_name,
								normalized_query),
						 errhidestmt(msgqno != qno),
						 errhidecontext(msgqno != qno)));

			msgqno = qno;
		}

		/* return if we have hint string here */
		if (current_hint_str)
			return;
	}

	/* get hints from the comment */
	oldcontext = MemoryContextSwitchTo(TopMemoryContext);
	bool ybHitError;
	current_hint_str = get_hints_from_comment(query_str, &ybHitError);

	if (ybHitError && yb_bad_hint_mode >= BAD_HINT_WARN)
	{
		ereport(WARNING,
				(errmsg("error trying to get hints from comment")));
	}

	MemoryContextSwitchTo(oldcontext);

	if (debug_level > 1)
	{
		if (debug_level == 2 && query_str && debug_query_string &&
			strcmp(query_str, debug_query_string))
			ereport(pg_hint_plan_debug_message_level,
					(errmsg("hints in comment=\"%s\"",
							current_hint_str ? current_hint_str : "(none)"),
					 errhidestmt(msgqno != qno),
					 errhidecontext(msgqno != qno)));
		else
			ereport(pg_hint_plan_debug_message_level,
					(errmsg("hints in comment=\"%s\", query=\"%s\", debug_query_string=\"%s\"",
							current_hint_str ? current_hint_str : "(none)",
							query_str ? query_str : "(none)",
							debug_query_string ? debug_query_string : "(none)"),
					 errhidestmt(msgqno != qno),
					 errhidecontext(msgqno != qno)));
		msgqno = qno;
	}
}

/*
 * Retrieve hint string from the current query.
 */
static void
pg_hint_plan_post_parse_analyze(ParseState *pstate, Query *query,
								JumbleState *jstate)
{
	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook(pstate, query, jstate);

	if (!pg_hint_plan_enable_hint || hint_inhibit_level > 0)
		return;

	/* always retrieve hint from the top-level query string */
	if (plpgsql_recurse_level == 0)
		current_hint_retrieved = false;

	/*
	 * Jumble state is required when hint table is used.  This is the only
	 * chance to have one already generated in-core.  If it's not the case, no
	 * use to do the work now and pg_hint_plan_planner() will do the all work.
	 */
	if (jstate)
		get_current_hint_string(query, pstate->p_sourcetext, jstate);
}

static bool ybInRecursionForTesting = false;

/*
 * Read and set up hint information
 */
static PlannedStmt *
pg_hint_plan_planner(Query *parse, const char *query_string, int cursorOptions, ParamListInfo boundParams)
{
	int				save_nestlevel;
	PlannedStmt	   *result;
	HintState	   *hstate;
	const char 	   *prev_hint_str = NULL;

	if (IsYugaByteEnabled() && yb_enable_planner_trace)
	{
		ereport(DEBUG1,
				(errmsg("\n++ BEGIN pg_hint_plan_planner\n        query: %s",
						query_string)));
	}

	/*
	 * Use standard planner if pg_hint_plan is disabled or current nesting
	 * depth is nesting depth of SPI calls. Other hook functions try to change
	 * plan with current_hint_state if any, so set it to NULL.
	 */
	if (!pg_hint_plan_enable_hint || hint_inhibit_level > 0)
	{
		if (debug_level > 1)
		{
			ereport(pg_hint_plan_debug_message_level,
					(errmsg ("pg_hint_plan%s: planner: enable_hint=%d,"
							 " hint_inhibit_level=%d",
							 qnostr, pg_hint_plan_enable_hint,
							 hint_inhibit_level),
					 errhidestmt(msgqno != qno)));
			msgqno = qno;
		}

		goto standard_planner_proc;
	}

	/*
	 * SQL commands invoked in plpgsql functions may also have hints. In that
	 * case override the upper level hint by the new hint.
	 */
	if (plpgsql_recurse_level > 0)
	{
		const char	 *tmp_hint_str = current_hint_str;

		/* don't let get_current_hint_string free this string */
		current_hint_str = NULL;

		current_hint_retrieved = false;

		get_current_hint_string(parse, query_string, NULL);

		if (current_hint_str == NULL)
			current_hint_str = tmp_hint_str;
		else if (tmp_hint_str != NULL)
			pfree((void *)tmp_hint_str);
	}
	else
		get_current_hint_string(parse, query_string, NULL);

	/* No hints, go the normal way */
	if (!current_hint_str)
		goto standard_planner_proc;

	/* parse the hint into hint state struct */
	hstate = create_hintstate(parse, pstrdup(current_hint_str));

	/* run standard planner if we're given with no valid hints */
	if (!hstate)
	{
		if (IsYugaByteEnabled())
		{
			if (yb_bad_hint_mode >= BAD_HINT_WARN)
			{
				ereport(WARNING,
						(errmsg("no valid hints found, planning query "
								"without hints")));
			}

			if (yb_bad_hint_mode == BAD_HINT_ERROR)
			{
				ereport(ERROR,
						(errmsg("errors found in hints (no state), "
								"will fail")));
			}
		}

		/* forget invalid hint string */
		if (current_hint_str)
		{
			pfree((void *)current_hint_str);
			current_hint_str = NULL;
		}

		goto standard_planner_proc;
	}
	else if (IsYugaByteEnabled() && hstate->ybAnyHintFailed)
	{
		if (yb_bad_hint_mode >= BAD_HINT_WARN)
		{
			ereport(WARNING,
					(errmsg("errors found in hints")));

			if (yb_bad_hint_mode == BAD_HINT_REPLAN)
			{
				ereport(WARNING,
						(errmsg("replanning query without hints")));
				goto standard_planner_proc;
			}
			else if (yb_bad_hint_mode == BAD_HINT_ERROR)
			{
				ereport(ERROR,
						(errmsg("errors found in hints, will fail")));
			}
		}
	}

	/*
	 * Push new hint struct to the hint stack to disable previous hint context.
	 * There should be no ERROR-level failures until we begin the
	 * PG_TRY/PG_CATCH block below to ensure a consistent stack handling all
	 * the time.
	 */
	push_hint(hstate);

	/*  Set scan enforcement here. */
	save_nestlevel = NewGUCNestLevel();

	/*
	 * The planner call below may replace current_hint_str. Store and restore
	 * it so that the subsequent planning in the upper level doesn't get
	 * confused.
	 */
	recurse_level++;
	prev_hint_str = current_hint_str;
	current_hint_str = NULL;

	/*
	 * Use PG_TRY mechanism to recover GUC parameters and current_hint_state to
	 * the state when this planner started when error occurred in planner.  We
	 * do this here to minimize the window where the hints currently pushed on
	 * the stack could not be popped out of it.
	 */
	PG_TRY();
	{
		/* Apply Set hints, then save it as the initial state  */
		setup_guc_enforcement(current_hint_state->set_hints,
							   current_hint_state->num_hints[HINT_TYPE_SET],
							   current_hint_state->context);

		current_hint_state->init_scan_mask = get_current_scan_mask();
		current_hint_state->init_join_mask = get_current_join_mask();
		current_hint_state->init_min_para_tablescan_size =
			min_parallel_table_scan_size;
		current_hint_state->init_min_para_indexscan_size =
			min_parallel_index_scan_size;
		current_hint_state->init_paratup_cost = parallel_tuple_cost;
		current_hint_state->init_parasetup_cost = parallel_setup_cost;

		/*
		 * max_parallel_workers_per_gather should be non-zero here if Workers
		 * hint is specified.
		 */
		if (max_hint_nworkers > 0 && max_parallel_workers_per_gather < 1)
			set_config_int32_option("max_parallel_workers_per_gather",
								1, current_hint_state->context);
		current_hint_state->init_nworkers = max_parallel_workers_per_gather;

		if (debug_level > 1)
		{
			ereport(pg_hint_plan_debug_message_level,
					(errhidestmt(msgqno != qno),
					 errmsg("pg_hint_plan%s: planner", qnostr)));
			msgqno = qno;
		}

		Query *ybSaveQuery = NULL;
		if (yb_bad_hint_mode == BAD_HINT_REPLAN)
		{
			ybSaveQuery = castNode(Query, copyObject(parse));
		}

		if (prev_planner)
			result = (*prev_planner) (parse, query_string,
									  cursorOptions, boundParams);
		else
			result = standard_planner(parse, query_string,
									  cursorOptions, boundParams);

		current_hint_str = prev_hint_str;
		recurse_level--;

		if (IsYugaByteEnabled())
		{
			ybCheckBadOrUnusedHints(result);

			if (current_hint_state->ybAnyHintFailed)
			{
				if (yb_bad_hint_mode >= BAD_HINT_WARN)
				{
					ereport(WARNING,
							(errmsg("unused hints, and/or hints "
									"causing errors, exist")));

					if (yb_enable_planner_trace)
					{
						ereport(DEBUG1,
								(errmsg("unused hints, and/or hints "
										"causing errors, exist")));
					}

					if (yb_bad_hint_mode == BAD_HINT_REPLAN)
					{
						if (yb_enable_planner_trace)
						{
							ereport(DEBUG1,
									(errmsg("replanning without hints")));
						}

						ereport(WARNING,
								(errmsg("replanning without hints")));

						HintState *save_current_hint_state = current_hint_state;
						current_hint_state = NULL;
						result = standard_planner(ybSaveQuery, query_string,
											cursorOptions, boundParams);
						current_hint_state = save_current_hint_state;
					}
					else if (yb_bad_hint_mode == BAD_HINT_ERROR)
					{
						if (yb_enable_planner_trace)
						{
							ereport(DEBUG1,
									(errmsg("errors found in hints, will fail")));
						}

						ereport(ERROR,
								(errmsg("errors found in hints, will fail")));
					}
				}
			}
		}
	}
	PG_CATCH();
	{
		/*
		 * Rollback changes of GUC parameters, and pop current hint context
		 * from hint stack to rewind the state. current_hint_str will be freed
		 * by context deletion.
		 */
		current_hint_str = prev_hint_str;
		recurse_level--;
		AtEOXact_GUC(true, save_nestlevel);
		pop_hint();
		PG_RE_THROW();
	}
	PG_END_TRY();


	/*
	 * current_hint_str is useless after planning of the top-level query.
	 * There's a case where the caller has multiple queries. This causes hint
	 * parsing multiple times for the same string but we don't have a simple
	 * and reliable way to distinguish that case from the case where of
	 * separate queries.
	 */
	if (recurse_level < 1)
		current_hint_retrieved = false;

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

	if (IsYugaByteEnabled() && yb_enable_planner_trace)
	{
		ereport(DEBUG1,
				(errmsg("\n++ END pg_hint_plan_planner\n        query: %s",
						query_string)));
	}

	return result;

standard_planner_proc:
	if (debug_level > 1)
	{
		ereport(pg_hint_plan_debug_message_level,
				(errhidestmt(msgqno != qno),
				 errmsg("pg_hint_plan%s: planner: no valid hint",
						qnostr)));
		msgqno = qno;
	}
	current_hint_state = NULL;

	Query *ybSaveQuery = NULL;
	if (yb_enable_internal_hint_test && query_string != NULL &&
		strlen(query_string) > 0 && !ybInRecursionForTesting &&
		(parse->commandType == CMD_SELECT ||
		 parse->commandType  == CMD_DELETE ||
		 parse->commandType == CMD_UPDATE ||
		 parse->commandType == CMD_INSERT))
	{
		/*
		 * Save a copy of the query if we want to test hint generation.
		 */
		ybSaveQuery = castNode(Query, copyObject(parse));
	}

	if (prev_planner)
		result =  (*prev_planner) (parse, query_string,
								   cursorOptions, boundParams);
	else
		result = standard_planner(parse, query_string,
								  cursorOptions, boundParams);

	if (yb_enable_internal_hint_test && result != NULL &&
		query_string != NULL && strlen(query_string) > 0 &&
		!ybInRecursionForTesting &&
		(result->commandType == CMD_SELECT ||
			result->commandType == CMD_DELETE ||
			result->commandType  == CMD_UPDATE ||
			result->commandType == CMD_INSERT))
	{
		ybInRecursionForTesting = true;
		char *generatedHintString = ybGenerateHintString(result);
		if (yb_enable_planner_trace)
		{
			ereport(DEBUG1,
					(errmsg("\ngenerated hints: %s\n",
							generatedHintString != NULL ?
							generatedHintString : "none")));
		}

		if ( generatedHintString != NULL)
		{
			StringInfoData buf;
			initStringInfo(&buf);
			appendStringInfoString(&buf, generatedHintString);
			appendStringInfoSpaces(&buf, 1);
			appendStringInfoString(&buf, query_string);
			bool save_current_hint_retrieved = current_hint_retrieved;
			current_hint_retrieved = false;
			HintState *save_current_hint_state = current_hint_state;
			current_hint_state = NULL;
			const char *save_current_hint_str = current_hint_str;
			current_hint_str = NULL;
			bool save_pg_hint_plan_enable_hint_table = pg_hint_plan_enable_hint_table;
			pg_hint_plan_enable_hint_table = false;

			PlannedStmt *hintedResult = pg_hint_plan_planner(ybSaveQuery, 
											   				 buf.data, 
															 cursorOptions,
															 boundParams);

			pg_hint_plan_enable_hint_table = save_pg_hint_plan_enable_hint_table;

			char *generatedHintString2 = ybGenerateHintString(hintedResult);

			if (yb_enable_planner_trace)
			{
				ereport(DEBUG1,
						(errmsg("generated hints (2): %s\n",
								generatedHintString2 != NULL ?
								generatedHintString2 : "none")));
			}

			bool plansAreEqual = ybComparePlanShapesAndMethods(result,
															   result->planTree,
												 			   hintedResult,
															   hintedResult->planTree,
															   true /* trace */ );

			if (yb_enable_planner_trace)
			{
				ereport(DEBUG1,
						(errmsg("\nplans are equal? %s\n",
								plansAreEqual ? "true" : "false")));
			}

			if (!plansAreEqual)
			{
				if (yb_enable_planner_trace)
				{
					ereport(DEBUG1,
							(errmsg("\nplan generated from hints not "
									"equivalent to non-hinted plan\n")));
				}

				if (yb_internal_hint_test_fail)
				{
					ereport(ERROR,
							(errmsg("\nplan generated from hints not "
									"equivalent  to non-hinted plan, "
									"will fail\n")));
				}
				else if (yb_bad_hint_mode >= BAD_HINT_WARN)
				{
					ereport(WARNING,
							(errmsg("\nplan generated from hints not "
									"equivalent to non-hinted plan\n")));
				}
			}

			if (yb_use_generated_hints_for_plan)
			{
				result = hintedResult;
				if (yb_enable_planner_trace)
				{
					ereport(DEBUG1,
							(errmsg("\nused plan from generated hints (2)\n")));
				}
			}

			current_hint_retrieved = save_current_hint_retrieved;
			current_hint_state = save_current_hint_state;
			current_hint_str = save_current_hint_str;
		}

		ybInRecursionForTesting = false;
	}

	/* The upper-level planner still needs the current hint state */
	if (HintStateStack != NIL)
		current_hint_state = (HintState *) lfirst(list_head(HintStateStack));

	if (IsYugaByteEnabled() && yb_enable_planner_trace)
	{
		ereport(DEBUG1,
				(errmsg("\n++ END pg_hint_plan_planner\n        query: %s",
						query_string)));
	}

	return result;
}

static char *
ybAliasForHinting(List *aliasMapping, RelOptInfo *rel, RangeTblEntry *rte)
{
	char *aliasForHint = NULL;
	if (rel != NULL &&
		rel->ybUniqueBaseId > 0 &&
		aliasMapping != NIL)
	{
		aliasForHint = list_nth(aliasMapping, rel->ybUniqueBaseId);
	}
	else
	{
		aliasForHint = rte->eref->aliasname;
	}

	return aliasForHint;
}

/*
 * Find scan method hint to be applied to the given relation
 *
 */
static ScanMethodHint *
find_scan_hint(PlannerInfo *root, Index relid)
{
	RelOptInfo	   *rel;
	RangeTblEntry  *rte;
	ScanMethodHint	*real_name_hint = NULL;
	ScanMethodHint	*alias_hint = NULL;
	int				i;

	/* This should not be a join rel */
	Assert(relid > 0);
	rel = root->simple_rel_array[relid];

	/*
	 * This function is called for any RelOptInfo or its inheritance parent if
	 * any. If we are called from inheritance planner, the RelOptInfo for the
	 * parent of target child relation is not set in the planner info.
	 *
	 * Otherwise we should check that the reloptinfo is base relation or
	 * inheritance children.
	 */
	if (rel &&
		rel->reloptkind != RELOPT_BASEREL &&
		rel->reloptkind != RELOPT_OTHER_MEMBER_REL)
		return NULL;

	/*
	 * This is baserel or appendrel children. We can refer to RangeTblEntry.
	 */
	rte = root->simple_rte_array[relid];
	Assert(rte);

	/* We don't hint on other than relation and foreign tables */
	if (rte->rtekind != RTE_RELATION ||
		rte->relkind == RELKIND_FOREIGN_TABLE)
		return NULL;

	char *aliasForHint = ybAliasForHinting(root->glob->ybPlanHintsAliasMapping, 
										   rel, rte);

	/* Find scan method hint, which matches given names, from the list. */
	for (i = 0; i < current_hint_state->num_hints[HINT_TYPE_SCAN_METHOD]; i++)
	{
		ScanMethodHint *hint = current_hint_state->scan_hints[i];

		/* We ignore disabled hints. */
		if (!hint_state_enabled(hint))
			continue;

		if (!alias_hint &&
			RelnameCmp(&aliasForHint, &hint->relname) == 0)
			alias_hint = hint;

		/* check the real name for appendrel children */
		if (!real_name_hint &&
			rel && rel->reloptkind == RELOPT_OTHER_MEMBER_REL)
		{
			char *realname = get_rel_name(rte->relid);

			if (realname && RelnameCmp(&realname, &hint->relname) == 0)
				real_name_hint = hint;
		}

		/* No more match expected, break  */
		if(alias_hint && real_name_hint)
			break;
	}

	/* real name match precedes alias match */
	if (real_name_hint)
		return real_name_hint;

	return alias_hint;
}

static ParallelHint *
find_parallel_hint(PlannerInfo *root, Index relid)
{
	RelOptInfo	   *rel;
	RangeTblEntry  *rte;
	ParallelHint	*real_name_hint = NULL;
	ParallelHint	*alias_hint = NULL;
	int				i;

	/* This should not be a join rel */
	Assert(relid > 0);
	rel = root->simple_rel_array[relid];

	/*
	 * Parallel planning is appliable only on base relation, which has
	 * RelOptInfo.
	 */
	if (!rel)
		return NULL;

	/*
	 * We have set root->glob->parallelModeOK if needed. What we should do here
	 * is just following the decision of planner.
	 */
	if (!rel->consider_parallel)
		return NULL;

	/*
	 * This is baserel or appendrel children. We can refer to RangeTblEntry.
	 */
	rte = root->simple_rte_array[relid];
	Assert(rte);

	char *ybAlias = ybAliasForHinting(root->glob->ybPlanHintsAliasMapping,
									  rel, rte);

	/* Find parallel method hint, which matches given names, from the list. */
	for (i = 0; i < current_hint_state->num_hints[HINT_TYPE_PARALLEL]; i++)
	{
		ParallelHint *hint = current_hint_state->parallel_hints[i];

		/* We ignore disabled hints. */
		if (!hint_state_enabled(hint))
			continue;

		if (!alias_hint &&
			RelnameCmp(&ybAlias, &hint->relname) == 0)
			alias_hint = hint;

		/* check the real name for appendrel children */
		if (!real_name_hint &&
			rel && rel->reloptkind == RELOPT_OTHER_MEMBER_REL)
		{
			char *realname = get_rel_name(rte->relid);

			if (realname && RelnameCmp(&realname, &hint->relname) == 0)
				real_name_hint = hint;
		}

		/* No more match expected, break  */
		if(alias_hint && real_name_hint)
			break;
	}

	/* real name match precedes alias match */
	if (real_name_hint)
		return real_name_hint;

	return alias_hint;
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


/*
 * Filter out indexes instructed in the hint as not to be used.
 *
 * This routine is used in relationship with the scan method enforcement, and
 * it returns true to allow the follow-up scan method to be enforced, and false
 * to prevent the scan enforcement.  Currently, this code will not enforce
 * the scan enforcement if *all* the indexes available to a relation have been
 * discarded.
 */
static bool
restrict_indexes(PlannerInfo *root, ScanMethodHint *hint, RelOptInfo *rel,
				 bool using_parent_hint)
{
	ListCell	   *cell;
	StringInfoData	buf;
	RangeTblEntry  *rte = root->simple_rte_array[rel->relid];
	Oid				relationObjectId = rte->relid;
	List		   *unused_indexes = NIL;
	bool			restrict_result;

	/*
	 * We delete all the IndexOptInfo list and prevent you from being usable by
	 * a scan.
	 */
	if (hint->enforce_mask == ENABLE_SEQSCAN ||
		hint->enforce_mask == ENABLE_TIDSCAN)
	{
		/*
		 * YB: Do not do a list_free_deep here since we need the IndexOptInfo
		 * objects for proving uniqueness.
		 */
		list_free(rel->indexlist);
		rel->indexlist = NIL;
		hint->base.state = HINT_STATE_USED;

		return true;
	}

	/*
	 * When a list of indexes is not specified, we just use all indexes.
	 */
	if (hint->indexnames == NIL)
		return true;

	/*
	 * Leaving only an specified index, we delete it from a IndexOptInfo list
	 * other than it.  However, if none of the specified indexes are available,
	 * then we keep all the indexes and skip enforcing the scan method. i.e.,
	 * we skip the scan hint altogether for the relation.
	 */
	if (debug_level > 0)
		initStringInfo(&buf);

	foreach (cell, rel->indexlist)
	{
		IndexOptInfo   *info = (IndexOptInfo *) lfirst(cell);
		char		   *indexname = get_rel_name(info->indexoid);
		ListCell	   *l;
		bool			use_index = false;

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
		 * Apply index restriction of parent hint to children. Since index
		 * inheritance is not explicitly described we should search for an
		 * children's index with the same definition to that of the parent.
		 */
		if (using_parent_hint && !use_index)
		{
			foreach(l, current_hint_state->parent_index_infos)
			{
				int					i;
				HeapTuple			ht_idx;
				ParentIndexInfo	   *p_info = (ParentIndexInfo *)lfirst(l);

				/*
				 * we check the 'same' index by comparing uniqueness, access
				 * method and index key columns.
				 */
				if (p_info->indisunique != info->unique ||
					p_info->method != info->relam ||
					list_length(p_info->column_names) != info->ncolumns)
					continue;

				/* Check if index key columns match */
				for (i = 0; i < info->ncolumns; i++)
				{
					char       *c_attname = NULL;
					char       *p_attname = NULL;

					p_attname = list_nth(p_info->column_names, i);

					/*
					 * if both of the key of the same position are expressions,
					 * ignore them for now and check later.
					 */
					if (info->indexkeys[i] == 0 && !p_attname)
						continue;

					/* deny if one is expression while another is not */
					if (info->indexkeys[i] == 0 || !p_attname)
						break;

					c_attname = get_attname(relationObjectId,
											info->indexkeys[i], false);

					/* deny if any of column attributes don't match */
					if (strcmp(p_attname, c_attname) != 0 ||
						p_info->indcollation[i] != info->indexcollations[i] ||
						p_info->opclass[i] != info->opcintype[i])
						break;

					/*
					 * Compare index ordering if this index is ordered.
					 *
					 * We already confirmed that this and the parent indexes
					 * share the same column set (actually only the length of
					 * the column set is compard, though.) and index access
					 * method. So if this index is unordered, the parent can be
					 * assumed to be be unodered. Thus no need to bother
					 * checking the parent's orderedness.
					 */
					if (info->sortopfamily != NULL &&
						(((p_info->indoption[i] & INDOPTION_DESC) != 0)
						 != info->reverse_sort[i] ||
						 ((p_info->indoption[i] & INDOPTION_NULLS_FIRST) != 0)
						 != info->nulls_first[i]))
						break;
				}

				/* deny this if any difference found */
				if (i != info->ncolumns)
					continue;

				/* check on key expressions  */
				if ((p_info->expression_str && (info->indexprs != NIL)) ||
					(p_info->indpred_str && (info->indpred != NIL)))
				{
					/* fetch the index of this child */
					ht_idx = SearchSysCache1(INDEXRELID,
											 ObjectIdGetDatum(info->indexoid));

					/* check expressions if both expressions are available */
					if (p_info->expression_str &&
						!heap_attisnull(ht_idx, Anum_pg_index_indexprs, NULL))
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

						/* deny if expressions don't match */
						if (strcmp(p_info->expression_str,
								   text_to_cstring(DatumGetTextP(result))) != 0)
						{
							/* Clean up */
							ReleaseSysCache(ht_idx);
							continue;
						}
					}

					/* compare index predicates  */
					if (p_info->indpred_str &&
						!heap_attisnull(ht_idx, Anum_pg_index_indpred, NULL))
					{
						Datum       predDatum;
						bool        isnull;
						Datum       result;

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
			unused_indexes = lappend_oid(unused_indexes, info->indexoid);

		pfree(indexname);
	}

	/*
	 * Update the list of indexes available to the IndexOptInfo based on what
	 * has been discarded previously.
	 *
	 * If the hint has no matching indexes, skip applying the hinted scan
	 * method.  For example if an IndexScan hint does not have any matching
	 * indexes, we should not enforce an enable_indexscan.
	 */
	if (list_length(unused_indexes) < list_length(rel->indexlist))
	{
		foreach (cell, unused_indexes)
		{
			Oid			final_oid = lfirst_oid(cell);
			ListCell   *l;

			foreach (l, rel->indexlist)
			{
				IndexOptInfo	   *info = (IndexOptInfo *) lfirst(l);

				if (info->indexoid == final_oid)
					rel->indexlist = foreach_delete_current(rel->indexlist, l);
			}
		}

		restrict_result = true;
	}
	else
		restrict_result = false;

	list_free(unused_indexes);

	if (debug_level > 0)
	{
		StringInfoData  rel_buf;
		char *disprelname = "";

		/*
		 * If this hint targetted the parent, use the real name of this
		 * child. Otherwise use hint specification.
		 */
		if (using_parent_hint)
			disprelname = get_rel_name(rte->relid);
		else
			disprelname = hint->relname;


		initStringInfo(&rel_buf);
		quote_value(&rel_buf, disprelname);

		ereport(pg_hint_plan_debug_message_level,
				(errmsg("available indexes for %s(%s):%s",
					 hint->base.keyword,
					 rel_buf.data,
					 buf.data)));
		pfree(buf.data);
		pfree(rel_buf.data);
	}

	return restrict_result;
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

	/*
	 * Collect relation attribute names of index columns for index
	 * identification, not index attribute names. NULL means expression index
	 * columns.
	 */
	for (i = 0; i < index->indnatts; i++)
	{
		attname = get_attname(relid, index->indkey.values[i], true);
		p_info->column_names = lappend(p_info->column_names, attname);

		p_info->indcollation[i] = indexRelation->rd_indcollation[i];
		p_info->opclass[i] = indexRelation->rd_opcintype[i];
		p_info->indoption[i] = indexRelation->rd_indoption[i];
	}

	/*
	 * to check to match the expression's parameter of index with child indexes
 	 */
	p_info->expression_str = NULL;
	if(!heap_attisnull(indexRelation->rd_indextuple, Anum_pg_index_indexprs,
					   NULL))
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
	if(!heap_attisnull(indexRelation->rd_indextuple, Anum_pg_index_indpred,
					   NULL))
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
 * cancel hint enforcement
 */
static void
reset_hint_enforcement()
{
	setup_scan_method_enforcement(NULL, current_hint_state);
	setup_parallel_plan_enforcement(NULL, current_hint_state);
}

/*
 * Set planner guc parameters according to corresponding scan hints.  Returns
 * bitmap of HintTypeBitmap. If shint or phint is not NULL, set used hint
 * there respectively.
 */
static int
setup_hint_enforcement(PlannerInfo *root, RelOptInfo *rel,
					   ScanMethodHint **rshint, ParallelHint **rphint)
{
	Index	new_parent_relid = 0;
	ListCell *l;
	ScanMethodHint *shint = NULL;
	ParallelHint   *phint = NULL;
	bool			inhparent = root->simple_rte_array[rel->relid]->inh;
	Oid		relationObjectId = root->simple_rte_array[rel->relid]->relid;
	int				ret = 0;

	/* reset returns if requested  */
	if (rshint != NULL) *rshint = NULL;
	if (rphint != NULL) *rphint = NULL;

	/*
	 * We could register the parent relation of the following children here
	 * when inhparent == true but inheritnce planner doesn't call this function
	 * for parents. Since we cannot distinguish who called this function we
	 * cannot do other than always seeking the parent regardless of who called
	 * this function.
	 */
	if (inhparent)
	{
		/* set up only parallel hints for parent relation */
		phint = find_parallel_hint(root, rel->relid);
		if (phint)
		{
			setup_parallel_plan_enforcement(phint, current_hint_state);
			if (rphint) *rphint = phint;
			ret |= HINT_BM_PARALLEL;
			return ret;
		}

		if (debug_level > 1)
			ereport(pg_hint_plan_debug_message_level,
					(errhidestmt(true),
					 errmsg ("pg_hint_plan%s: setup_hint_enforcement"
							 " skipping inh parent: relation=%u(%s), inhparent=%d,"
							 " current_hint_state=%p, hint_inhibit_level=%d",
							 qnostr, relationObjectId,
							 get_rel_name(relationObjectId),
							 inhparent, current_hint_state, hint_inhibit_level)));
		return 0;
	}

	if (bms_num_members(rel->top_parent_relids) == 1)
	{
		new_parent_relid = bms_next_member(rel->top_parent_relids, -1);
		current_hint_state->current_root = root;
		Assert(new_parent_relid > 0);
	}
	else
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
		 * hint to other childrens of this parent so remember it to avoid
		 * redundant setup cost.
		 */
		current_hint_state->parent_relid = new_parent_relid;

		/* Find hints for the parent */
		current_hint_state->parent_scan_hint =
			find_scan_hint(root, current_hint_state->parent_relid);

		current_hint_state->parent_parallel_hint =
			find_parallel_hint(root, current_hint_state->parent_relid);

		/*
		 * If hint is found for the parent, apply it for this child instead
		 * of its own.
		 */
		if (current_hint_state->parent_scan_hint)
		{
			ScanMethodHint * pshint = current_hint_state->parent_scan_hint;

			/* Apply index mask in the same manner to the parent. */
			if (pshint->indexnames)
			{
				Oid			parentrel_oid;
				Relation	parent_rel;

				parentrel_oid =
					root->simple_rte_array[current_hint_state->parent_relid]->relid;
				parent_rel = table_open(parentrel_oid, NoLock);

				/* Search the parent relation for indexes match the hint spec */
				foreach(l, RelationGetIndexList(parent_rel))
				{
					Oid         indexoid = lfirst_oid(l);
					char       *indexname = get_rel_name(indexoid);
					ListCell   *lc;
					ParentIndexInfo *parent_index_info;

					foreach(lc, pshint->indexnames)
					{
						if (RelnameCmp(&indexname, &lfirst(lc)) == 0)
							break;
					}
					if (!lc)
						continue;

					parent_index_info =
						get_parent_index_info(indexoid, parentrel_oid);
					current_hint_state->parent_index_infos =
						lappend(current_hint_state->parent_index_infos,
								parent_index_info);
				}
				table_close(parent_rel, NoLock);
			}
		}
	}

	shint = find_scan_hint(root, rel->relid);
	if (!shint)
		shint = current_hint_state->parent_scan_hint;

	if (shint)
	{
		bool using_parent_hint =
			(shint == current_hint_state->parent_scan_hint);
		bool restrict_result;

		ret |= HINT_BM_SCAN_METHOD;

		/* restrict unwanted indexes */
		restrict_result = restrict_indexes(root, shint, rel, using_parent_hint);

		/*
		 * Setup scan enforcement environment
		 *
		 * This has to be called after restrict_indexes(), that may decide to
		 * skip the scan method enforcement depending on the index restrictions
		 * applied.
		 */
		if (restrict_result)
			setup_scan_method_enforcement(shint, current_hint_state);

		if (debug_level > 1)
		{
			char *additional_message = "";

			if (shint == current_hint_state->parent_scan_hint)
				additional_message = " by parent hint";

			ereport(pg_hint_plan_debug_message_level,
					(errhidestmt(true),
					 errmsg ("pg_hint_plan%s: setup_hint_enforcement"
							 " index deletion%s:"
							 " relation=%u(%s), inhparent=%d, "
							 "current_hint_state=%p,"
							 " hint_inhibit_level=%d, scanmask=0x%x",
							 qnostr, additional_message,
							 relationObjectId,
							 get_rel_name(relationObjectId),
							 inhparent, current_hint_state,
							 hint_inhibit_level,
							 shint->enforce_mask)));
		}
	}

	/* Do the same for parallel plan enforcement */
	phint = find_parallel_hint(root, rel->relid);
	if (!phint)
		phint = current_hint_state->parent_parallel_hint;

	setup_parallel_plan_enforcement(phint, current_hint_state);

	if (phint)
		ret |= HINT_BM_PARALLEL;

	/* Nothing to apply. Reset the scan mask to intial state */
	if (!shint && ! phint)
	{
		if (debug_level > 1)
			ereport(pg_hint_plan_debug_message_level,
					(errhidestmt (true),
					 errmsg ("pg_hint_plan%s: setup_hint_enforcement"
							 " no hint applied:"
							 " relation=%u(%s), inhparent=%d, current_hint=%p,"
							 " hint_inhibit_level=%d, scanmask=0x%x",
							 qnostr, relationObjectId,
							 get_rel_name(relationObjectId),
							 inhparent, current_hint_state, hint_inhibit_level,
							 current_hint_state->init_scan_mask)));

		setup_scan_method_enforcement(NULL,	current_hint_state);

		return ret;
	}

	if (rshint != NULL) *rshint = shint;
	if (rphint != NULL) *rphint = phint;

	return ret;
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

		char * aliasForHint = ybAliasForHinting(root->glob->ybPlanHintsAliasMapping,
												root->simple_rel_array[i],
												root->simple_rte_array[i]);

		if (RelnameCmp(&aliasname, &aliasForHint) != 0)
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


/*
 * Return memoize hint which matches given joinrelids.
 */
static JoinMethodHint *
find_memoize_hint(Relids joinrelids)
{
	List	   *join_hint;
	ListCell   *l;

	join_hint = current_hint_state->memoize_hint_level[bms_num_members(joinrelids)];

	foreach(l, join_hint)
	{
		JoinMethodHint *hint = (JoinMethodHint *) lfirst(l);

		if (bms_equal(joinrelids, hint->joinrelids))
			return hint;
	}

	return NULL;
}

static void
ybAddProhibitedJoin(PlannerInfo *root, NodeTag ybJoinTag, Relids join_relids)
{
	Assert(join_relids);
	root->ybProhibitedJoinTypes
		= lappend_int(root->ybProhibitedJoinTypes, ybJoinTag);
	root->ybProhibitedJoins = lappend(root->ybProhibitedJoins, join_relids);

	if (yb_enable_planner_trace)
	{
		StringInfoData buf;
		initStringInfo(&buf);
		appendStringInfoString(&buf, "ybAddProhibitedJoin");

		StringInfoData buf2;
		initStringInfo(&buf2);
		ybBuildRelidsString(root, join_relids, &buf2);
		appendStringInfoSpaces(&buf, 2);
		appendStringInfo(&buf, "join relids : %s\n", buf2.data);;
		ereport(DEBUG1,
				(errmsg("\n%s", buf.data)));
		pfree(buf.data);
		pfree(buf2.data);
	}
}

static void
ybAddHintedJoin(PlannerInfo *root, Relids outer_relids, Relids inner_relids)
{
	Assert(outer_relids);
	Assert(inner_relids);
	root->ybHintedJoinsOuter = lappend(root->ybHintedJoinsOuter, outer_relids);
	root->ybHintedJoinsInner = lappend(root->ybHintedJoinsInner, inner_relids);

	if (yb_enable_planner_trace)
	{
		Relids join_relids = bms_union(outer_relids, inner_relids);
		StringInfoData buf;
		initStringInfo(&buf);
		appendStringInfoString(&buf, "ybAddHintedJoin");

		StringInfoData buf2;
		initStringInfo(&buf2);
		ybBuildRelidsString(root, join_relids, &buf2);
		appendStringInfoSpaces(&buf, 2);
		appendStringInfo(&buf, "join relids : %s\n", buf2.data);
		resetStringInfo(&buf2);
		ybBuildRelidsString(root, outer_relids, &buf2);
		appendStringInfoSpaces(&buf, 4);
		appendStringInfo(&buf, "outer relids : %s\n", buf2.data);
		resetStringInfo(&buf2);
		ybBuildRelidsString(root, inner_relids, &buf2);
		appendStringInfoSpaces(&buf, 4);
		appendStringInfo(&buf, "inner relids : %s\n", buf2.data);
		ereport(DEBUG1,
				(errmsg("\n%s", buf.data)));
		bms_free(join_relids);
		pfree(buf.data);
		pfree(buf2.data);
	}
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

	outer_rels = linitial(outer_inner->outer_inner_pair);
	inner_rels = llast(outer_inner->outer_inner_pair);

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

	Relids ybOrigOuterRelids = NULL;
	Relids ybOrigInnerRelids = NULL;
	if (IsYugaByteEnabled())
	{
		/*
		 * Copy the input relids since at least the outer will be modified below
		 * by the bms_add_members() call to form join_relids.
		 */
		ybOrigOuterRelids = bms_copy(outer_relids);
		ybOrigInnerRelids = bms_copy(inner_relids);
		if (yb_enable_planner_trace)
		{
			ybTraceRelds(root, outer_relids,
							"OuterInnerJoinCreate : outer relids");
			ybTraceRelds(root, inner_relids,
							"OuterInnerJoinCreate : inner relids");
		}
	}

	join_relids = bms_add_members(outer_relids, inner_relids);

	if (bms_num_members(join_relids) > nbaserel)
		return join_relids;

	if (IsYugaByteEnabled())
	{
		ybAddHintedJoin(root, ybOrigOuterRelids, ybOrigInnerRelids);
	}

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

		if (IsYugaByteEnabled() && yb_enable_planner_trace)
		{
			StringInfoData buf;
			initStringInfo(&buf);
			JoinMethodHintDesc(hint, &buf, true);
			ereport(DEBUG1,
					(errmsg("no method hint found, "
							"created one at level %d: %s",
							hint->nrels, buf.data)));
			pfree(buf.data);
		}
	}
	else
	{
		hint->inner_nrels = bms_num_members(inner_relids);
		hint->inner_joinrelids = bms_copy(inner_relids);

		if (IsYugaByteEnabled() && yb_enable_planner_trace)
		{
			StringInfoData buf;
			initStringInfo(&buf);
			JoinMethodHintDesc(hint, &buf, true);
			ereport(DEBUG1,
					(errmsg("  found hint : %s", buf.data)));
			pfree(buf.data);
		}
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

		if (IsYugaByteEnabled())
		{
			NodeTag ybJoinTag = ybIsNegationJoinHint((Hint *) hint);
			if (ybJoinTag != 0)
			{
				ybAddProhibitedJoin(root, ybJoinTag, bms_copy(hint->joinrelids));
			}
		}

		hstate->join_hint_level[hint->nrels] =
			lappend(hstate->join_hint_level[hint->nrels], hint);

		if (IsYugaByteEnabled() && yb_enable_planner_trace)
		{
			StringInfoData buf;
			initStringInfo(&buf);
			JoinMethodHintDesc(hint, &buf, true);
			ereport(DEBUG1,
					(errmsg("transform_join_hints : added join method hint at "
							"join level %d : %s", hint->nrels, buf.data)));
		}
	}

	/* ditto for memoize hints */
	for (i = 0; i < hstate->num_hints[HINT_TYPE_MEMOIZE]; i++)
	{
		JoinMethodHint *hint = hstate->join_hints[i];

		if (!hint_state_enabled(hint) || hint->nrels > nbaserel)
			continue;

		hint->joinrelids = create_bms_of_relids(&(hint->base), root,
									 initial_rels, hint->nrels, hint->relnames);

		if (hint->joinrelids == NULL || hint->base.state == HINT_STATE_ERROR)
			continue;

		hstate->memoize_hint_level[hint->nrels] =
			lappend(hstate->memoize_hint_level[hint->nrels], hint);
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

	if (IsYugaByteEnabled() && yb_enable_planner_trace)
	{
		if (hstate->num_hints[HINT_TYPE_LEADING] == 0)
		{
			ereport(DEBUG1,
					(errmsg("no leading hints")));
		}
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

		if (IsYugaByteEnabled() && yb_enable_planner_trace)
		{
			StringInfoData buf;
			initStringInfo(&buf);
			LeadingHintDesc(leading_hint, &buf, true);
			StringInfoData buf2;
			initStringInfo(&buf2);
			appendStringInfo(&buf2,
				"transform_join_hints : next leading hint : %s (error? : %s)",
				buf.data,
				leading_hint->base.state == HINT_STATE_ERROR ? " true" : "false");
			ereport(DEBUG1,
					(errmsg("next leading hint : %s", buf2.data)));
			pfree(buf.data);
			pfree(buf2.data);
		}

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

			if (IsYugaByteEnabled() && yb_enable_planner_trace)
			{
				ereport(DEBUG1,
						(errmsg("  relname %s -> relid %d", relname, relid)));
			}

			if (relid <= 0)
				break;

			if (bms_is_member(relid, relids))
			{
				hint_ereport(leading_hint->base.hint_str,
							 ("Relation name \"%s\" is duplicated.", relname));
				leading_hint->base.state = HINT_STATE_ERROR;

				if (IsYugaByteEnabled())
				{
					hstate->ybAnyHintFailed = true;
				}

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

			if (IsYugaByteEnabled())
			{
				hstate->ybAnyHintFailed = true;
			}
		}
		leading_hint->base.state = HINT_STATE_USED;
		lhint = leading_hint;
	}

	/* check to exist Leading hint marked with 'used'. */
	if (lhint == NULL)
	{
		if (IsYugaByteEnabled() && yb_enable_planner_trace)
		{
			ereport(DEBUG1,
					(errmsg("transform_join_hints : leading hint is NULL, "
							"returning FALSE")));
		}

		return false;
	}

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
		if (IsYugaByteEnabled() && yb_enable_planner_trace)
		{
			ereport(DEBUG1,
					(errmsg("  no outer-inner")));
		}

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

			Relids yb_prev_relids = bms_copy(joinrelids);

			/* Create bitmap of relids for current join level. */
			joinrelids = bms_add_member(joinrelids, relid);
			njoinrels++;

			/* We never have join method hint for single relation. */
			if (njoinrels < 2)
				continue;

			if (IsYugaByteEnabled())
			{
				/* Make relid set with the current single relation. */
				Relids current_relids = bms_make_singleton(relid);

				if (yb_enable_planner_trace)
				{
					ereport(DEBUG1,
							(errmsg("transform_join_hints : adding hinted join")));
				}

				/*
				 * For a Leading hint with no nesting the inner and outer can 
				 * be swapped so allow join both ways.
				 */
				ybAddHintedJoin(root, yb_prev_relids, current_relids);
				ybAddHintedJoin(root, current_relids, yb_prev_relids);

				if (yb_enable_planner_trace)
				{
					ybTraceRelds(root, joinrelids,
								 "transform_join_hints : looking for join "
								 "method hint for relids");
				}
			}

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

				if (IsYugaByteEnabled() && yb_enable_planner_trace)
				{
					StringInfoData buf;
					initStringInfo(&buf);
					JoinMethodHintDesc(hint, &buf, true);
					ereport(DEBUG1,
							(errmsg("  no method hint found , created one : %s",
									buf.data)));
					pfree(buf.data);
				}
			}
			else
			{
				if (IsYugaByteEnabled() && yb_enable_planner_trace)
				{
					StringInfoData buf;
					initStringInfo(&buf);
					JoinMethodHintDesc(hint, &buf, true);
					ereport(DEBUG1,
							(errmsg("  found method hint found : %s", buf.data)));
					pfree(buf.data);
				}
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
		if (IsYugaByteEnabled() && yb_enable_planner_trace)
		{
			ereport(DEBUG1,
					(errmsg("  found outer-inner")));
		}

		joinrelids = OuterInnerJoinCreate(lhint->outer_inner,
										  lhint,
                                          root,
                                          initial_rels,
										  hstate,
										  nbaserel);

		if (IsYugaByteEnabled() && yb_enable_planner_trace)
		{
			ybTraceRelds(root, joinrelids, "created outer-inner relids");
		}

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
				foreach (l, hstate->join_hint_level[i])
				{

					JoinMethodHint *hint = (JoinMethodHint *)lfirst(l);

					if (hint->inner_nrels == 0 &&
						!(bms_intersect(hint->joinrelids, joinrelids) == NULL ||
						  bms_equal(bms_union(hint->joinrelids, joinrelids),
						  hint->joinrelids)))
					{
						hstate->join_hint_level[i] =
							foreach_delete_current(hstate->join_hint_level[i], l);
					}
				}
			}
		}

		bms_free(joinrelids);
	}

	if (hint_state_enabled(lhint))
	{
		set_join_config_options(DISABLE_ALL_JOIN, false,
								current_hint_state->context);
		return true;
	}
	return false;
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
	JoinMethodHint *join_hint;
	JoinMethodHint *memoize_hint;
	RelOptInfo	   *rel;
	int				save_nestlevel;

	joinrelids = bms_union(rel1->relids, rel2->relids);
	join_hint = find_join_hint(joinrelids);
	memoize_hint = find_memoize_hint(joinrelids);
	bms_free(joinrelids);

	int ybLevel = bms_num_members(rel1->relids) + bms_num_members(rel2->relids);
	if (IsYugaByteEnabled() && yb_enable_planner_trace)
	{
		StringInfoData buf;
		initStringInfo(&buf);
		char ybMsgBuf[30];
		sprintf(ybMsgBuf, "(UID %u) ", ybGetNextUid(root->glob));
		appendStringInfo(&buf, "%s Created logical join Level %d : ",
						 ybMsgBuf, ybLevel);
		StringInfoData buf2;
		initStringInfo(&buf2);
		ybBuildRelidsString(root, rel1->relids, &buf2);
		appendStringInfoString(&buf, buf2.data);
		appendStringInfoSpaces(&buf, 1);
		resetStringInfo(&buf2);
		ybBuildRelidsString(root, rel2->relids, &buf2);
		appendStringInfoString(&buf, buf2.data);
		ereport(DEBUG1,
				(errmsg("\n%s\n", buf.data)));

		if (join_hint != NULL)
		{
			resetStringInfo(&buf);
			join_hint->base.desc_func((Hint *) join_hint, &buf, false);
			ereport(DEBUG1,
					(errmsg("\nlevel %d join method hint: %s (mask 0x%x)\n",
							ybLevel, buf.data, join_hint->enforce_mask)));
		}

		pfree(buf.data);
		pfree(buf2.data);
	}

	bool ybSetJoinHints = false;

	/* reject non-matching hints */
	if (join_hint && join_hint->inner_nrels != 0)
		join_hint = NULL;

	if (memoize_hint && memoize_hint->inner_nrels != 0)
		memoize_hint = NULL;

	if (join_hint || memoize_hint)
	{
		save_nestlevel = NewGUCNestLevel();

		if (join_hint)
			set_join_config_options(join_hint->enforce_mask, false,
									current_hint_state->context);

		if (memoize_hint)
		{
			bool memoize =
				memoize_hint->base.hint_keyword == HINT_KEYWORD_MEMOIZE;
			set_config_option_noerror("enable_memoize",
									  memoize ? "true" : "false",
									  current_hint_state->context,
									  PGC_S_SESSION, GUC_ACTION_SAVE,
									  true, ERROR);
		}
	}

	if (IsYugaByteEnabled() && join_hint == NULL)
	{
		if (current_hint_state->join_hint_level[ybLevel] == NULL)
		{
			/*
			 * There is no join method hint for this join. Also, there
			 * no leading or join method hints for the level. In this case
			 * we enable all join methods for this join.
			 */
			save_nestlevel = NewGUCNestLevel();
			set_join_config_options(ENABLE_ALL_JOIN, false,
									current_hint_state->context);
			ybSetJoinHints = true;
		}
	}

	/* do the work */
	rel = pg_hint_plan_make_join_rel(root, rel1, rel2);

	/* Restore the GUC variables we set above. */
	if (join_hint || memoize_hint || ybSetJoinHints)
	{
		if (join_hint)
			join_hint->base.state = HINT_STATE_USED;

		if (memoize_hint)
			memoize_hint->base.state = HINT_STATE_USED;

		AtEOXact_GUC(true, save_nestlevel);
	}

	if (IsYugaByteEnabled() && yb_enable_planner_trace)
	{
		StringInfoData buf;
		initStringInfo(&buf);
		char ybMsgBuf[30];
		sprintf(ybMsgBuf, "(UID %u) ", ybGetNextUid(root->glob));
		if (rel != NULL)
		{
			appendStringInfo(&buf, "%s New join rel Level %d : ",
							ybMsgBuf, ybLevel);
			ybTraceRelOptInfo(root, rel, buf.data);
		}
		else
		{
			appendStringInfo(&buf, "%s No join rel formed Level %d",
							 ybMsgBuf, ybLevel);
			ereport(DEBUG1,
					(errmsg("\n%s\n", buf.data)));
		}

		pfree(buf.data);
	}

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
	Relids			joinrelids;
	JoinMethodHint *join_hint;
	JoinMethodHint *memoize_hint;
	int				save_nestlevel;

	if (IsYugaByteEnabled() && yb_enable_planner_trace)
	{
		int level = (bms_num_members(outerrel->relids) +
						bms_num_members(innerrel->relids));

		char ybMsgBuf[30];
		sprintf(ybMsgBuf, "(UID %u) ", ybGetNextUid(root->glob));
		ereport(DEBUG1,
				(errmsg("\n%s add_paths_to_joinrel_wrapper : (Level %d)",
						ybMsgBuf, level)));
		ybTraceRelOptInfo(root, joinrel, "join rel");
		ybTraceRelOptInfo(root, outerrel, "outer rel");
		ybTraceRelOptInfo(root, innerrel, "inner rel");
	}

	joinrelids = bms_union(outerrel->relids, innerrel->relids);
	join_hint = find_join_hint(joinrelids);
	memoize_hint = find_memoize_hint(joinrelids);
	bms_free(joinrelids);

	/* reject the found hints if they don't match this join */
	if (join_hint && join_hint->inner_nrels == 0)
		join_hint = NULL;

	if (memoize_hint && memoize_hint->inner_nrels == 0)
		memoize_hint = NULL;

	/* set up configuration if needed */
	if (join_hint || memoize_hint)
	{
		save_nestlevel = NewGUCNestLevel();

		if (join_hint)
		{
			if (IsYugaByteEnabled() && yb_enable_planner_trace)
			{
				int level = (bms_num_members(outerrel->relids) +
							bms_num_members(innerrel->relids));

				char ybMsgBuf[30];
				sprintf(ybMsgBuf, "(UID %u) ", ybGetNextUid(root->glob));
				ereport(DEBUG1,
						(errmsg("\n%s add_paths_to_joinrel_wrapper : "
								"found join hint (Level %d)",
								ybMsgBuf, level)));
				if (join_hint->base.hint_str != NULL)
				{
					ereport(DEBUG1,
							(errmsg("\n  join hint string : %s",
									join_hint->base.hint_str)));
				}

				ybTraceRelOptInfo(root, joinrel, "join rel");
				ybTraceRelOptInfo(root, outerrel, "outer rel");
				ybTraceRelOptInfo(root, innerrel, "inner rel");
				ybTraceRelds(root, join_hint->inner_joinrelids,
								"join hint inner rels");
				ybTraceRelds(root, innerrel->relids, "inner rels");

				sprintf(ybMsgBuf, "(UID %u) ", ybGetNextUid(root->glob));

				if (bms_equal(join_hint->inner_joinrelids, innerrel->relids))
				{
					ereport(DEBUG1,
							(errmsg("\n+++ %s Level %d ACCEPTED join for hint, "
									"enabling join options +++",
									ybMsgBuf, level)));
				}
				else
				{
					ereport(DEBUG1,
							(errmsg("\n--- %s Level %d REJECTED join for hint, "
									"disabling all join options ---",
									ybMsgBuf, level)));
				}
			}

			if (bms_equal(join_hint->inner_joinrelids, innerrel->relids))
				set_join_config_options(join_hint->enforce_mask, false,
										current_hint_state->context);
			else
				set_join_config_options(DISABLE_ALL_JOIN, false,
										current_hint_state->context);
		}

		if (memoize_hint)
		{
			bool memoize =
				memoize_hint->base.hint_keyword == HINT_KEYWORD_MEMOIZE;
			set_config_option_noerror("enable_memoize",
									  memoize ? "true" : "false",
									  current_hint_state->context,
									  PGC_S_SESSION, GUC_ACTION_SAVE,
									  true, ERROR);
		}
	}

	/* generate paths */
	add_paths_to_joinrel(root, joinrel, outerrel, innerrel, jointype,
						 sjinfo, restrictlist);

	if (IsYugaByteEnabled() && yb_enable_planner_trace)
	{
		ybTraceRelOptInfo(root, joinrel, "join rel after adding paths");
	}

	/* restore GUC variables */
	if (join_hint || memoize_hint)
	{
		if (join_hint)
			join_hint->base.state = HINT_STATE_USED;

		if (memoize_hint)
			memoize_hint->base.state = HINT_STATE_USED;

		AtEOXact_GUC(true, save_nestlevel);
	}
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

static HintKeyword
ybIsNegationHint(Hint *hint)
{
	HintKeyword hintKeyword;
	switch (hint->hint_keyword)
	{
		case HINT_KEYWORD_NONESTLOOP:
		case HINT_KEYWORD_NOBATCHEDNL:
		case HINT_KEYWORD_NOMERGEJOIN:
		case HINT_KEYWORD_NOHASHJOIN:
		case HINT_KEYWORD_NOINDEXSCAN:
		case HINT_KEYWORD_NOBITMAPSCAN:
		case HINT_KEYWORD_NOTIDSCAN:
		case HINT_KEYWORD_NOINDEXONLYSCAN:
		case HINT_KEYWORD_NOMEMOIZE:
			hintKeyword = hint->hint_keyword;
			break;
		default:
			hintKeyword = HINT_KEYWORD_UNRECOGNIZED;
			break;
	}

	return hintKeyword;
}

static NodeTag
ybIsNegationJoinHint(Hint *hint)
{
	NodeTag joinTag;
	switch (hint->hint_keyword)
	{
		case HINT_KEYWORD_NONESTLOOP:
			joinTag = T_NestLoop;
			break;
		case HINT_KEYWORD_NOBATCHEDNL:
			joinTag = T_YbBatchedNestLoop;
			break;
		case HINT_KEYWORD_NOMERGEJOIN:
			joinTag = T_MergeJoin;
			break;
		case HINT_KEYWORD_NOHASHJOIN:
			joinTag = T_HashJoin;
			break;
		default:
			joinTag = 0;
			break;
	}

	return joinTag;
}

static bool
ybIsRelationNameInScope(char *hintRelationName)
{
	Assert(current_hint_state != NULL);
	Assert(hintRelationName != NULL);

	bool relationNameInScope = false;

	ListCell *lc;
	foreach(lc, current_hint_state->ybAllAliasNamesInScope)
	{
		char *aliasNameInScope = (char *) lfirst(lc);
		if (strcmp(hintRelationName, aliasNameInScope) == 0)
		{
			relationNameInScope = true;
			break;
		}
	}

	return relationNameInScope;
}

static char *
ybCheckPlanForDisabledNodes(Plan *plan, PlannedStmt *plannedStmt)
{
	char *errorMsg = NULL;

	if (plan != NULL)
	{
		switch (nodeTag(plan))
		{
			case T_SeqScan:
			case T_YbSeqScan:
			case T_BitmapHeapScan:
			case T_YbBitmapTableScan:
			case T_TidScan:
			case T_TidRangeScan:
			case T_IndexScan:
			case T_IndexOnlyScan:
			case T_BitmapIndexScan:
			case T_YbBitmapIndexScan:
				{
					if (plan->total_cost >= disable_cost)
					{
						if (plan->ybHintAlias != NULL)
						{
							StringInfoData buf;
							initStringInfo(&buf);
							appendStringInfo(&buf,
											 "no valid access method found for "
											 "relation \"%s\"",
											 plan->ybHintAlias);
							errorMsg = buf.data;
						}
						else
						{
							errorMsg = "no valid access method found for "
										"some relation";
						}
					}
				}
				break;

			case T_NestLoop:
			case T_YbBatchedNestLoop:
			case T_MergeJoin:
			case T_HashJoin:
				{
					if (plan->total_cost >= disable_cost)
					{
						if (!(plan->ybIsHinted))
						{
							if (plan->ybUniqueId > 0)
							{
								StringInfoData buf;
								initStringInfo(&buf);
								appendStringInfo(&buf,
												 "no valid method found for join "
												 "with UID %d",
												 plan->ybUniqueId);
								errorMsg = buf.data;
							}
							else
							{
								errorMsg
									= "no valid method found for some join";
							}
						}
					}
				}
				break;
			default:
				break;
		}

		char *childErrorMsg = NULL;
		if (plan->lefttree != NULL)
		{
			childErrorMsg = ybCheckPlanForDisabledNodes(plan->lefttree, 
														plannedStmt);

			if (childErrorMsg == NULL && plan->righttree != NULL)
			{
				childErrorMsg = ybCheckPlanForDisabledNodes(plan->righttree, 
															plannedStmt);
			}
		}

		if (childErrorMsg == NULL)
		{
			ListCell *lc;
			foreach(lc, plan->initPlan)
			{
				SubPlan *initPlan = (SubPlan *) lfirst(lc);
				Plan *subPlan = (Plan *) list_nth(plannedStmt->subplans,
													initPlan->plan_id - 1);

				childErrorMsg = ybCheckPlanForDisabledNodes(subPlan, 
															plannedStmt);

				if (childErrorMsg != NULL)
				{
					break;
				}
			}
		}

		if (childErrorMsg != NULL)
		{
			errorMsg = childErrorMsg;
		}
	}

	return errorMsg;
}

static void
ybCheckBadOrUnusedHints(PlannedStmt *plannedStmt)
{
	if (current_hint_state != NULL && yb_bad_hint_mode >= BAD_HINT_WARN)
	{
		char *errorMsg = ybCheckPlanForDisabledNodes(plannedStmt->planTree,
														plannedStmt);
		if (errorMsg != NULL)
		{
			ereport(WARNING,
					(errmsg("%s", errorMsg)));

			current_hint_state->ybAnyHintFailed = true;
		}

		StringInfoData buf;
		initStringInfo(&buf);

		for (int i = 0; i < current_hint_state->nall_hints; ++i)
		{
			Hint *currentHint = current_hint_state->all_hints[i];

			if (currentHint->state == HINT_STATE_ERROR)
			{
				currentHint->desc_func(currentHint, &buf, true);

				ereport(WARNING,
						(errmsg("error in hint: %s", buf.data)));

				current_hint_state->ybAnyHintFailed = true;
			}
			else if (currentHint->state == HINT_STATE_NOTUSED &&
					 ybIsNegationHint(currentHint) == HINT_KEYWORD_UNRECOGNIZED)
			{
				currentHint->desc_func(currentHint, &buf, true);
				current_hint_state->ybAnyHintFailed = true;

				ereport(WARNING,
						(errmsg("unused hint: %s", buf.data)));

				if (currentHint->type == HINT_TYPE_SCAN_METHOD)
				{
					ScanMethodHint *scanHint = (ScanMethodHint *) currentHint;

					if (!ybIsRelationNameInScope(scanHint->relname))
					{
						ereport(WARNING,
								(errmsg("bad relation name \"%s\" in scan "
										"hint: %s "
										"(use alias name from EXPLAIN)",
										scanHint->relname, buf.data)));
					}
				}
				else if (currentHint->type == HINT_TYPE_JOIN_METHOD ||
							currentHint->type == HINT_TYPE_MEMOIZE)
				{
					JoinMethodHint *joinMethodHint = (JoinMethodHint *) currentHint;
					Assert(joinMethodHint->nrels > 0);
					for (int i = 0; i < joinMethodHint->nrels; ++i)
					{
						char *relNameInHint = joinMethodHint->relnames[i];
						if (!ybIsRelationNameInScope(relNameInHint))
						{
							ereport(WARNING,
									(errmsg("bad relation name \"%s\" in join "
											"hint: %s "
											"(use alias name from EXPLAIN)",
											relNameInHint, buf.data)));
						}
					}
				}
				else if (currentHint->type == HINT_TYPE_LEADING)
				{
					LeadingHint *leadingHint = (LeadingHint *) currentHint;
					ListCell *lc;
					foreach (lc, leadingHint->relations)
					{
						char *relNameInHint = (char *) lfirst(lc);
						if (!ybIsRelationNameInScope(relNameInHint))
						{
							ereport(WARNING,
									(errmsg("bad relation name \"%s\" "
											"in leading hint: %s "
											"(use alias name from EXPLAIN)",
											relNameInHint, buf.data)));
						}
					}
				}
				else if (currentHint->type == HINT_TYPE_ROWS)
				{
					RowsHint *rowsHint = (RowsHint *) currentHint;

					for (int i = 0; i < rowsHint->nrels; ++i)
					{
						char *relNameInHint = rowsHint->relnames[i];
						if (!ybIsRelationNameInScope(relNameInHint))
						{
							ereport(WARNING,
									(errmsg("bad relation name \"%s\" in "
											"rows hint: %s (use alias name from "
											"EXPLAIN)",
											relNameInHint, buf.data)));
						}
					}
				}
				else if (currentHint->type == HINT_TYPE_PARALLEL)
				{
					ParallelHint *parallelHint = (ParallelHint *) currentHint;

					if (!ybIsRelationNameInScope(parallelHint->relname))
					{
						ereport(WARNING,
								(errmsg("bad relation name \"%s\" in parallel "
										"hint: %s (use alias name from EXPLAIN)",
										parallelHint->relname, buf.data)));
					}
				}
				else
				{
					ereport(WARNING,
							(errmsg("unexpected hint type %d in unused hint: %s",
									currentHint->type, buf.data)));
				}
			}

			resetStringInfo(&buf);
		}

		pfree(buf.data);
	}

	return;
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
	current_hint_state->memoize_hint_level =
		palloc0(sizeof(List *) * (nbaserel + 1));

	leading_hint_enable = transform_join_hints(current_hint_state,
											   root, nbaserel,
											   initial_rels, join_method_hints);

	if (IsYugaByteEnabled())
	{
		if (leading_hint_enable && yb_enable_planner_trace &&
			current_hint_state->leading_hint != NULL)
		{
			ybTraceLeadingHint(*(current_hint_state->leading_hint),
							   "pg_hint_plan_join_search : leading hint after transformation");
		}
	}

	rel = pg_hint_plan_standard_join_search(root, levels_needed, initial_rels);

	/*
	 * Adjust number of parallel workers of the result rel to the largest
	 * number of the component paths.
	 */
	if (current_hint_state->num_hints[HINT_TYPE_PARALLEL] > 0)
	{
		ListCell   *lc;
		int 		nworkers = 0;

		foreach (lc, initial_rels)
		{
			ListCell *lcp;
			RelOptInfo *initrel = (RelOptInfo *) lfirst(lc);

			foreach (lcp, initrel->partial_pathlist)
			{
				Path *path = (Path *) lfirst(lcp);

				if (nworkers < path-> parallel_workers)
					nworkers = path-> parallel_workers;
			}
		}

		foreach (lc, rel->partial_pathlist)
		{
			Path *path = (Path *) lfirst(lc);

			if (path->parallel_safe && path->parallel_workers < nworkers)
				path->parallel_workers = nworkers;
		}
	}

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
		set_join_config_options(current_hint_state->init_join_mask, true,
								current_hint_state->context);

	return rel;
}

static char *
ybCheckBadIndexHintExists(PlannerInfo *root, RelOptInfo *rel)
{
	char *badIndexName = NULL;
	ScanMethodHint *shint = find_scan_hint(root, rel->relid);
	if (shint != NULL)
	{
		if (shint->indexnames != NULL)
		{
			ListCell *lc;
			foreach(lc, shint->indexnames)
			{
				char *hintIndexName = lfirst(lc);
				bool matchedHintIndex = false;

				ListCell *lc1;
				foreach(lc1, rel->indexlist)
				{
					IndexOptInfo *index = (IndexOptInfo *) lfirst(lc1);
					char *indexName = get_rel_name(index->indexoid);

					if (RelnameCmp(&indexName, &hintIndexName) == 0)
					{
						matchedHintIndex = true;
						break;
					}
				}

				if (!matchedHintIndex)
				{
					badIndexName = hintIndexName;
					break;
				}
			}
		}
	}

	return badIndexName;
}

static bool
ybCheckValidPathForRelExists(PlannerInfo * root, RelOptInfo *rel)
{
	bool 		relHintingFailedOnCost = false;
	bool 		enabledPathFound = false;
	bool 		disabledPathFound = false;
	ListCell   *lc;
	foreach (lc, rel->pathlist)
	{
		Path *path = (Path *) lfirst(lc);
		if (path->total_cost < disable_cost || path->ybIsHinted ||
			path->ybHasHintedUid)
		{
			enabledPathFound = true;
			break;
		}
		else
		{
			disabledPathFound = true;
		}
	}

	if (!enabledPathFound && disabledPathFound)
	{
		relHintingFailedOnCost = true;
	}

	if (!relHintingFailedOnCost)
	{
		enabledPathFound = false;
		disabledPathFound = false;
		foreach (lc, rel->partial_pathlist)
		{
			Path *path = (Path *) lfirst(lc);
			if (path->total_cost < disable_cost || path->ybIsHinted ||
				path->ybHasHintedUid)
			{
				enabledPathFound = true;
				break;
			}
			else
			{
				disabledPathFound = true;
			}
		}
	}

	if (!enabledPathFound && disabledPathFound)
	{
		relHintingFailedOnCost = true;
	}

	return !relHintingFailedOnCost;
}

/*
 * Force number of wokers if instructed by hint
 */
void
pg_hint_plan_set_rel_pathlist(PlannerInfo * root, RelOptInfo *rel,
							  Index rti, RangeTblEntry *rte)
{
	ParallelHint   *phint;
	ListCell	   *l;
	int				found_hints;

	if (IsYugaByteEnabled())
	{
		if (current_hint_state != NULL && rel->ybHintAlias != NULL &&
			!ybIsRelationNameInScope(rel->ybHintAlias))
		{
			current_hint_state->ybAllAliasNamesInScope = lappend(current_hint_state->ybAllAliasNamesInScope,
																 pstrdup(rel->ybHintAlias));
		}
	}

	/* call the previous hook */
	if (prev_set_rel_pathlist)
		prev_set_rel_pathlist(root, rel, rti, rte);

	/* Nothing to do if no hint available */
	if (current_hint_state == NULL)
		return;

	/* Don't touch dummy rels. */
	if (IS_DUMMY_REL(rel))
		return;

	/*
	 * We can accept only plain relations, foreign tables and table samples are
	 * also unacceptable. See set_rel_pathlist.
	 */
	if (rel->rtekind != RTE_RELATION ||
		rte->relkind == RELKIND_FOREIGN_TABLE ||
		rte->tablesample != NULL)
		return;

	if (!IsYugaByteEnabled())
	{
		/* We cannot handle if this requires an outer */
		if (rel->lateral_relids)
			return;
	}

	if (list_length(rel->ybHintsOrigIndexlist) < list_length(rel->indexlist))
	{
		rel->ybHintsOrigIndexlist = list_copy(rel->indexlist);
	}

	/* Return if this relation gets no enfocement */
	if ((found_hints = setup_hint_enforcement(root, rel, NULL, &phint)) == 0)
		return;

	/* Here, we regenerate paths with the current hint restriction */
	if (found_hints & HINT_BM_SCAN_METHOD || found_hints & HINT_BM_PARALLEL)
	{
		if (IsYugaByteEnabled())
		{
			char *badIndexName = ybCheckBadIndexHintExists(root, rel);
			if (badIndexName != NULL)
			{
				current_hint_state->ybAnyHintFailed = true;
				if (yb_bad_hint_mode >= BAD_HINT_WARN)
				{
					ereport(WARNING,
							(errmsg("bad index hint name \"%s\" for table %s",
									badIndexName, rel->ybHintAlias)));
				}
			}
		}

		/*
		 * When hint is specified on non-parent relations, discard existing
		 * paths and regenerate based on the hint considered. Otherwise we
		 * already have hinted childx paths then just adjust the number of
		 * planned number of workers.
		 */
		if (root->simple_rte_array[rel->relid]->inh)
		{
			/* enforce number of workers if requested */
			if (phint && phint->force_parallel)
			{
				if (phint->nworkers == 0)
				{
					list_free_deep(rel->partial_pathlist);
					rel->partial_pathlist = NIL;
				}
				else
				{
					/* prioritize partial paths */
					foreach (l, rel->partial_pathlist)
					{
						Path *ppath = (Path *) lfirst(l);

						if (ppath->parallel_safe)
						{
							ppath->parallel_workers	= phint->nworkers;
							ppath->ybIsHinted = true;
							ppath->startup_cost = 0;
							ppath->total_cost = 0;
						}
					}

					/* disable non-partial paths */
					foreach (l, rel->pathlist)
					{
						Path *ppath = (Path *) lfirst(l);

						if (ppath->startup_cost < disable_cost)
						{
							ppath->startup_cost += disable_cost;
							ppath->total_cost += disable_cost;
						}
					}
				}
			}
		}
		else
		{
			/* Just discard all the paths considered so far */
			list_free_deep(rel->pathlist);
			rel->pathlist = NIL;
			list_free_deep(rel->partial_pathlist);
			rel->partial_pathlist = NIL;

			/* Regenerate paths with the current enforcement */
			set_plain_rel_pathlist(root, rel, rte);

			/* Additional work to enforce parallel query execution */
			if (phint && phint->nworkers > 0)
			{
				/*
				 * For Parallel Append to be planned properly, we shouldn't set
				 * the costs of non-partial paths to disable-value.  Lower the
				 * priority of non-parallel paths by setting partial path costs
				 * to 0 instead.
				 */
				foreach (l, rel->partial_pathlist)
				{
					Path *path = (Path *) lfirst(l);
					path->ybIsHinted = true;
					path->startup_cost = 0;
					path->total_cost = 0;
				}

				/* enforce number of workers if requested */
				if (phint->force_parallel)
				{
					foreach (l, rel->partial_pathlist)
					{
						Path *ppath = (Path *) lfirst(l);

						if (ppath->parallel_safe)
							ppath->parallel_workers	= phint->nworkers;
					}
				}

				/*
				 * Generate gather paths.  However, if this is an inheritance
				 * child, skip it.
				 */
				if (rel->reloptkind == RELOPT_BASEREL &&
					!bms_equal(rel->relids, root->all_baserels))
					generate_useful_gather_paths(root, rel, false);
			}
		}

		if (IsYugaByteEnabled())
		{
			if (!ybCheckValidPathForRelExists(root, rel))
			{
				current_hint_state->ybAnyHintFailed = true;

				if (yb_bad_hint_mode >= BAD_HINT_WARN)
				{
					StringInfoData buf;
					initStringInfo(&buf);
					ybBuildRelidsString(root, rel->relids, &buf);
					ereport(WARNING,
							(errmsg("hinting led to no path being found "
									"for relation \"%s\"",
									buf.data)));
					pfree(buf.data);
				}
			}
		}
	}

	reset_hint_enforcement();
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

	/*
	 * If we come here, we should have gone through the statement begin
	 * callback at least once.
	 */
	if (plpgsql_recurse_level > 0)
		plpgsql_recurse_level--;
}

void plpgsql_query_erase_callback(ResourceReleasePhase phase,
								  bool isCommit,
								  bool isTopLevel,
								  void *arg)
{
	/* Cleanup is just applied once all the locks are released */
	if (phase != RESOURCE_RELEASE_AFTER_LOCKS)
		return;

	if (isTopLevel)
	{
		/* Cancel recurse level */
		plpgsql_recurse_level = 0;
	}
	else if (plpgsql_recurse_level > 0)
	{
		/*
		 * This applies when a transaction is aborted for a PL/pgSQL query,
		 * like when a transaction triggers an exception, or for an internal
		 * commit.
		 */
		plpgsql_recurse_level--;
	}
}

/*
 * Initialize the hint cache. We use the TopMemoryContext so that the cache
 * is persistent for the duration of the backend.
 */
static HTAB *
yb_init_hint_cache(void)
{
	elog(DEBUG1, "Initializing hint cache");

	HASHCTL ctl;
	MemSet(&ctl, 0, sizeof(ctl));

	ctl.keysize = sizeof(YbHintCacheKey);
	ctl.entrysize = sizeof(YbHintCacheEntry);
	ctl.hcxt = YbHintCacheCtx;
	ctl.hash = yb_hint_hash_fn;
	ctl.match = yb_hint_match_fn;

	return hash_create(
		"YbHintCache", 128, /* initial size estimate */
		&ctl, HASH_ELEM | HASH_FUNCTION | HASH_COMPARE | HASH_CONTEXT);
}

/* Hash function for YbHintCacheKey (composite of two strings) */
static uint32
yb_hint_hash_fn(const void *key, Size keysize)
{
	Assert(keysize == sizeof(YbHintCacheKey));
	const YbHintCacheKey *k = (const YbHintCacheKey *) key;
	const char *s1 = k->query_string;
	const char *s2 = k->application_name;
	Size len1 = strlen(s1);
	Size len2 = strlen(s2);

	/* Compute hash for each key component */
	uint32 h1 = hash_bytes((const unsigned char *) s1, len1);
	uint32 h2 = hash_bytes((const unsigned char *) s2, len2);

	/* Combine the two hash values into one */
	uint32 h_combined = hash_combine(h1, h2);
	return h_combined;
}

/* Comparison function for YbHintCacheKey */
static int
yb_hint_match_fn(const void *key1, const void *key2, Size keysize)
{
	Assert(keysize == sizeof(YbHintCacheKey));
	const YbHintCacheKey *k1 = (const YbHintCacheKey *) key1;
	const YbHintCacheKey *k2 = (const YbHintCacheKey *) key2;
	if (strcmp(k1->query_string, k2->query_string) == 0 &&
		strcmp(k1->application_name, k2->application_name) == 0)
	{
		return 0;
	}
	return 1;
}

/*
 * Look up a cached hints entry for a given query and application name.
 * Returns a pointer to the hints string, or NULL if no entry is found.
 * The hints string is copied to the caller's context.
 */
static const char *
yb_get_cached_hints(const char *client_query, const char *client_application,
					bool *found)
{
	YbHintCacheEntry *entry;
	YbHintCacheKey key;
	key.query_string = client_query;
	key.application_name = client_application;

	if (!YbHintCache)
		YbRefreshHintCache();

	entry =
		(YbHintCacheEntry *) hash_search(YbHintCache, &key, HASH_FIND, found);
	if (*found)
	{
		elog(DEBUG4, "Hint cache hit: query: %s, application: '%s', hints: %s",
			 client_query, client_application, entry->hints);
		YbIncrementHintCacheHits();
		return pstrdup(entry->hints);
	}
	return NULL;
}

/*
 * Stores one row from the hint_plan.hints table in the in-memory hint cache.
 */
static void
yb_set_cached_hints(const char *client_query, const char *client_application,
					const char *hints, HTAB *hint_cache)
{
	YbHintCacheEntry *entry;
	bool found;
	YbHintCacheKey key;
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(YbHintCacheCtx);

	key.query_string = client_query;
	key.application_name = client_application;

	entry =
		(YbHintCacheEntry *) hash_search(hint_cache, &key, HASH_ENTER, &found);
	/*
	 * found should never be true since (a) there's a unique constraint on the
	 * query and application name, and (b) we delete and re-create the hash
	 * table before calling this function.
	 */
	Assert(!found);
	/*
	 * Copy all strings into the hash table's memory context so they
	 * survive for the life of the cache.
	 */
	entry->key.query_string = pstrdup(client_query);
	entry->key.application_name = pstrdup(client_application);
	entry->hints = hints ? pstrdup(hints) : NULL;

	MemoryContextSwitchTo(oldcontext);
}

/*
 * Invalidates the in-memory hint cache when we receive a relcache invalidation
 * message for the hint_plan.hints table.
 */
static void
YbInvalidateHintCacheCallback(Datum argument, Oid relationId)
{
	/*
	 * If we don't know what the OID of the hint table is, then we always
	 * have to invalidate the hint cache to be safe.
	 */
	if (relationId == YbCachedHintRelationId ||
		YbCachedHintRelationId == InvalidOid)
	{
		elog(DEBUG3, "Invalidating this backend's hint cache");
		YbHintCache = NULL;
		YbCachedHintRelationId = InvalidOid;

		/*
		 * This invalidation message may have been triggered by a modification
		 * to a hint for a prepared statement that's currently in the plan cache.
		 * If that prepared statement is using a generic plan, it's never going
		 * to pick up the new hint. Clearing the plan cache will force the planner
		 * to re-plan the prepared statement with the new hint.
		 *
		 * We don't know which prepared statements were affected, so we have to reset
		 * the entire plan cache.
		 */
		ResetPlanCache();
	}
}

/*
 * Returns the oid of the hint_plan.hints table (cached in memory).
 * If the cached oid is invalid, the oid is retrieved from the pg_class
 * catalog cache.
 */
static Oid
YbGetHintRelationId(void)
{
	if (YbCachedHintRelationId == InvalidOid)
	{
		Oid hintSchemaId = get_namespace_oid("hint_plan", false);

		YbCachedHintRelationId = get_relname_relid("hints", hintSchemaId);
		elog(DEBUG3, "New hint relation id: %d", YbCachedHintRelationId);
	}

	return YbCachedHintRelationId;
}

/*
 * Converts a heap tuple from the hint table into a YbHintCacheEntry struct.
 * Allocates memory for the entry in caller's context.
 */
static YbHintCacheEntry *
YbTupleToHintCacheEntry(TupleDesc tupleDescriptor, HeapTuple heapTuple)
{
	YbHintCacheEntry *entry = palloc(sizeof(YbHintCacheEntry));

	/* All columns of the hint table are non-nullable */
	bool isNull;
	entry->key.query_string = TextDatumGetCString(heap_getattr(
		heapTuple, YB_HINT_ATTR_QUERY_STRING, tupleDescriptor, &isNull));
	entry->key.application_name = TextDatumGetCString(heap_getattr(
		heapTuple, YB_HINT_ATTR_APPLICATION_NAME, tupleDescriptor, &isNull));
	entry->hints = TextDatumGetCString(
		heap_getattr(heapTuple, YB_HINT_ATTR_HINTS, tupleDescriptor, &isNull));

	return entry;
}

/*
 * Deletes the entire hint cache and scans the hint table, inserting all
 * rows into the cache.
 */
void
YbRefreshHintCache(void)
{
	elog(DEBUG4, "Refreshing hint cache");
	YbIncrementHintCacheRefreshes();

	if (YbHintCacheCtx == NULL)
	{
		elog(DEBUG3, "Creating hint cache context");

		if (!CacheMemoryContext)
			CreateCacheMemoryContext();

		YbHintCacheCtx = AllocSetContextCreate(
			CacheMemoryContext, "YbHintCacheContext", ALLOCSET_DEFAULT_SIZES);
	}
	else
	{
		elog(DEBUG3, "Resetting hint cache context");
		MemoryContextReset(YbHintCacheCtx);
	}

	HTAB *hint_cache = yb_init_hint_cache();

	MemoryContext hintScanContext = AllocSetContextCreate(
		YbHintCacheCtx, "YbHintScanContext", ALLOCSET_DEFAULT_SIZES);
	MemoryContext oldContext = MemoryContextSwitchTo(hintScanContext);

	if (!RecoveryInProgress())
	{
		Relation hintTable = NULL;
		SysScanDesc scanDescriptor = NULL;
		ScanKeyData scanKey[1];
		int scanKeyCount = 0;
		HeapTuple heapTuple = NULL;
		TupleDesc tupleDescriptor = NULL;

		elog(DEBUG4, "Scanning hint table");

		hintTable = table_open(YbGetHintRelationId(), AccessShareLock);

		scanDescriptor = systable_beginscan(hintTable, InvalidOid, false, NULL,
											scanKeyCount, scanKey);

		tupleDescriptor = RelationGetDescr(hintTable);

		while (HeapTupleIsValid(heapTuple = systable_getnext(scanDescriptor)))
		{
			/*
			 * Entries must be allocated in YbHintCacheCtx so they survive
			 * the deletion of hintScanContext.
			 */
			MemoryContextSwitchTo(YbHintCacheCtx);
			YbHintCacheEntry *entry = YbTupleToHintCacheEntry(tupleDescriptor, heapTuple);
			if (entry != NULL)
				yb_set_cached_hints(entry->key.query_string,
									entry->key.application_name, entry->hints, hint_cache);
			MemoryContextSwitchTo(hintScanContext);
		}

		systable_endscan(scanDescriptor);
		table_close(hintTable, AccessShareLock);
	}

	MemoryContextSwitchTo(oldContext);
	MemoryContextDelete(hintScanContext);

	elog(DEBUG4, "Done refreshing hint cache");

	YbHintCache = hint_cache;
}

/*
 * yb_hint_plan_cache_invalidate invalidates the hint cache in response to
 * a trigger.
 */
PG_FUNCTION_INFO_V1(yb_hint_plan_cache_invalidate);
Datum
yb_hint_plan_cache_invalidate(PG_FUNCTION_ARGS)
{
	if (!yb_enable_hint_table_cache)
	{
		elog(DEBUG3, "Hint cache is disabled, skipping cache invalidation");
		PG_RETURN_DATUM(PointerGetDatum(NULL));
	}

	if (!CALLED_AS_TRIGGER(fcinfo))
	{
		ereport(ERROR, (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
						errmsg("must be called as trigger")));
	}

	if (IsYsqlUpgrade || YBCPgYsqlMajorVersionUpgradeInProgress() || yb_extension_upgrade)
	{
		/* DDL operations are not allowed during extension upgrade */
		elog(DEBUG3, "Skipping hint cache invalidation during extension upgrade");
		PG_RETURN_DATUM(PointerGetDatum(NULL));
	}

	/*
	 * Send an invalidation message marking the hint table relcache entry as
	 * invalid.
	 */
	YBIncrementDdlNestingLevel(YB_DDL_MODE_VERSION_INCREMENT);

	YbInvalidateHintCache();

	YbForceSendInvalMessages();
	YBDecrementDdlNestingLevel();

	PG_RETURN_DATUM(PointerGetDatum(NULL));
}

/*
 * Invalidates the hint cache in response to a trigger.
 */
static void
YbInvalidateHintCache(void)
{
	HeapTuple classTuple = NULL;

	classTuple = SearchSysCache1(RELOID, ObjectIdGetDatum(YbGetHintRelationId()));
	if (HeapTupleIsValid(classTuple))
	{
		elog(DEBUG3, "Marking hint table relcache entry as invalid");
		CacheInvalidateRelcacheByTuple(classTuple);
		ReleaseSysCache(classTuple);
	}
}

/* include core static functions */
static void populate_joinrel_with_paths(PlannerInfo *root, RelOptInfo *rel1,
										RelOptInfo *rel2, RelOptInfo *joinrel,
										SpecialJoinInfo *sjinfo, List *restrictlist);

#define standard_join_search pg_hint_plan_standard_join_search
#define join_search_one_level pg_hint_plan_join_search_one_level
#define make_join_rel make_join_rel_wrapper
#include "core.c"

#undef make_join_rel
#define make_join_rel pg_hint_plan_make_join_rel
#define add_paths_to_joinrel add_paths_to_joinrel_wrapper
#include "make_join_rel.c"

#include "pg_stat_statements.c"

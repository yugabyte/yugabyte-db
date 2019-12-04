#include "postgres.h"

#include "access/htup.h"
#include "access/htup_details.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "nodes/nodeFuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "parser/analyze.h"
#include "parser/parse_node.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "analyze.h"
#include "cypher_clause.h"
#include "cypher_parser.h"

typedef struct cypher_parse_error_callback_arg
{
    const char *source_str;
    int query_loc;
} cypher_parse_error_callback_arg;

static post_parse_analyze_hook_type prev_post_parse_analyze_hook;

static void post_parse_analyze(ParseState *pstate, Query *query);
static bool convert_cypher_walker(Node *node, ParseState *pstate);
static bool is_rte_cypher(RangeTblEntry *rte);
static bool is_func_cypher(FuncExpr *funcexpr);
static void convert_cypher_to_subquery(RangeTblEntry *rte, ParseState *pstate);
static const char *expr_get_const_cstring(Node *expr, const char *source_str);
static int get_query_location(const int location, const char *source_str);
static void cypher_parse_error_callback(void *arg);
static Query *parse_and_analyze_cypher(const char *query_str, Param *params);
static void check_result_type(Query *query, RangeTblFunction *rtfunc,
                              ParseState *pstate);

void post_parse_analyze_init(void)
{
    prev_post_parse_analyze_hook = post_parse_analyze_hook;
    post_parse_analyze_hook = post_parse_analyze;
}

void post_parse_analyze_fini(void)
{
    post_parse_analyze_hook = prev_post_parse_analyze_hook;
}

static void post_parse_analyze(ParseState *pstate, Query *query)
{
    if (prev_post_parse_analyze_hook)
        prev_post_parse_analyze_hook(pstate, query);

    convert_cypher_walker((Node *)query, pstate);
}

// find cypher() calls in FROM clauses and convert them to SELECT subqueries
static bool convert_cypher_walker(Node *node, ParseState *pstate)
{
    if (!node)
        return false;

    if (IsA(node, RangeTblEntry))
    {
        RangeTblEntry *rte = (RangeTblEntry *)node;

        switch (rte->rtekind)
        {
        case RTE_SUBQUERY:
            // traverse other RTE_SUBQUERYs
            return convert_cypher_walker((Node *)rte->subquery, pstate);
        case RTE_FUNCTION:
            if (is_rte_cypher(rte))
                convert_cypher_to_subquery(rte, pstate);
            return false;
        default:
            return false;
        }
    }

    /*
     * This handles a cypher() call with other function calls in a ROWS FROM
     * expression. We can let the FuncExpr case below handle it but do this
     * here to throw a better error message.
     */
    if (IsA(node, RangeTblFunction))
    {
        RangeTblFunction *rtfunc = (RangeTblFunction *)node;
        FuncExpr *funcexpr = (FuncExpr *)rtfunc->funcexpr;

        /*
         * It is better to throw a kind error message here instead of the
         * internal error message that cypher() throws later when it is called.
         */
        if (is_func_cypher(funcexpr))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("cypher(...) in ROWS FROM is not supported"),
                     parser_errposition(pstate, exprLocation((Node *)funcexpr))));
        }

        return expression_tree_walker((Node *)funcexpr->args,
                                      convert_cypher_walker, pstate);
    }

    /*
     * This handles cypher() calls in expressions. Those in RTE_FUNCTIONs are
     * handled by either convert_cypher_to_subquery() or the RangeTblFunction
     * case above.
     */
    if (IsA(node, FuncExpr))
    {
        FuncExpr *funcexpr = (FuncExpr *)node;

        if (is_func_cypher(funcexpr))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("cypher(...) in expressions is not supported"),
                     errhint("Use subquery instead if possible."),
                     parser_errposition(pstate, exprLocation(node))));
        }

        return expression_tree_walker((Node *)funcexpr->args,
                                      convert_cypher_walker, pstate);
    }

    if (IsA(node, Query))
    {
        int flags;

        /*
         * QTW_EXAMINE_RTES
         *     We convert RTE_FUNCTION (cypher()) to RTE_SUBQUERY (SELECT)
         *     in-place.
         *
         * QTW_IGNORE_RT_SUBQUERIES
         *     After the conversion, we don't need to traverse the resulting
         *     RTE_SUBQUERY. However, we need to traverse other RTE_SUBQUERYs.
         *     This is done manually by the RTE_SUBQUERY case above.
         *
         * QTW_IGNORE_JOINALIASES
         *     We are not interested in this.
         */
        flags = QTW_EXAMINE_RTES | QTW_IGNORE_RT_SUBQUERIES |
                QTW_IGNORE_JOINALIASES;

        return query_tree_walker((Query *)node, convert_cypher_walker, pstate,
                                 flags);
    }

    return expression_tree_walker(node, convert_cypher_walker, pstate);
}

static bool is_rte_cypher(RangeTblEntry *rte)
{
    RangeTblFunction *rtfunc;
    FuncExpr *funcexpr;

    /*
     * The planner expects RangeTblFunction nodes in rte->functions list.
     * We cannot replace one of them to a SELECT subquery.
     */
    if (list_length(rte->functions) != 1)
        return false;

    /*
     * A plain function call or a ROWS FROM expression with one function call
     * reaches here. At this point, it is impossible to distinguish between the
     * two. However, it doesn't matter because they are identical in terms of
     * their meaning.
     */

    rtfunc = linitial(rte->functions);
    funcexpr = (FuncExpr *)rtfunc->funcexpr;
    return is_func_cypher(funcexpr);
}

/*
 * Return true if the qualified name of the given function is
 * <"ag_catalog"."cypher">. Otherwise, return false.
 */
static bool is_func_cypher(FuncExpr *funcexpr)
{
    HeapTuple proctup;
    Form_pg_proc proc;
    Oid nspid;
    const char *nspname;

    proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcexpr->funcid));
    Assert(HeapTupleIsValid(proctup));
    proc = (Form_pg_proc)GETSTRUCT(proctup);
    if (strncmp(NameStr(proc->proname), "cypher", NAMEDATALEN) != 0)
    {
        ReleaseSysCache(proctup);
        return false;
    }
    nspid = proc->pronamespace;
    ReleaseSysCache(proctup);

    nspname = get_namespace_name_or_temp(nspid);
    Assert(nspname);
    return (strcmp(nspname, "ag_catalog") == 0);
}

// convert cypher() call to SELECT subquery in-place
static void convert_cypher_to_subquery(RangeTblEntry *rte, ParseState *pstate)
{
    RangeTblFunction *rtfunc = linitial(rte->functions);
    FuncExpr *funcexpr = (FuncExpr *)rtfunc->funcexpr;
    Node *arg;
    const char *query_str;
    Node *params;
    cypher_parse_error_callback_arg ecb_arg;
    ErrorContextCallback ecb;
    Query *query;

    /*
     * We cannot apply this feature directly to SELECT subquery because the
     * planner does not support it. Adding a "row_number() OVER ()" expression
     * to the subquery as a result target might be a workaround but we throw an
     * error for now.
     */
    if (rte->funcordinality)
    {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("WITH ORDINALITY is not supported"),
                 parser_errposition(pstate, exprLocation((Node *)funcexpr))));
    }

    // NOTE: Remove asserts once the prototype of cypher() function is fixed.
    arg = linitial(funcexpr->args);
    Assert(exprType(arg) == CSTRINGOID);

    /*
     * Since cypher() function is nothing but an interface to get a Cypher
     * query, it must take a string constant as an argument so that the query
     * can be parsed and analyzed at this point to create a Query tree of it.
     *
     * Also, only dollar-quoted string constants are allowed because of the
     * following reasons.
     *
     * * If other kinds of string constants are used, the actual values of them
     *   may differ from what they are shown. This will confuse users.
     * * In the case above, the error position may not be accurate.
     */
    query_str = expr_get_const_cstring(arg, pstate->p_sourcetext);
    if (!query_str)
    {
        ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                        errmsg("a dollar-quoted string constant is expected"),
                        parser_errposition(pstate, exprLocation(arg))));
    }

    /*
     * Check to see if the cypher function had any parameters passed to it,
     * if so make sure Postgres parsed the second argument to a Param node.
     */
    if (list_length(funcexpr->args) == 2)
    {
        params = lsecond(funcexpr->args);
        if (!IsA(params, Param))
        {
            ereport(
                ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg(
                     "second argument of cypher function must be a parameter"),
                 parser_errposition(pstate, exprLocation(params))));
        }
    }
    else
    {
        params = NULL;
    }

    // install error context callback to adjust the error position
    ecb_arg.source_str = pstate->p_sourcetext;
    ecb_arg.query_loc = get_query_location(((Const *)arg)->location,
                                           pstate->p_sourcetext);
    ecb.previous = error_context_stack;
    ecb.callback = cypher_parse_error_callback;
    ecb.arg = &ecb_arg;
    error_context_stack = &ecb;

    query = parse_and_analyze_cypher(query_str, (Param *)params);

    // uninstall error context callback
    error_context_stack = ecb.previous;

    check_result_type(query, rtfunc, pstate);

    // rte->functions and rte->funcordinality are kept for debugging.
    // rte->alias, rte->eref, and rte->lateral need to be the same.
    // rte->inh is always false for both RTE_FUNCTION and RTE_SUBQUERY.
    // rte->inFromCl is always true for RTE_FUNCTION.
    rte->rtekind = RTE_SUBQUERY;
    rte->subquery = query;
}

static const char *expr_get_const_cstring(Node *expr, const char *source_str)
{
    Const *con;
    const char *p;

    if (!IsA(expr, Const))
        return NULL;

    con = (Const *)expr;
    if (con->constisnull)
        return NULL;

    Assert(con->location > -1);
    p = source_str + con->location;
    if (*p != '$')
        return NULL;

    return DatumGetCString(con->constvalue);
}

static int get_query_location(const int location, const char *source_str)
{
    const char *p;

    Assert(location > -1);

    p = source_str + location;
    Assert(*p == '$');

    return strchr(p + 1, '$') - source_str + 1;
}

static void cypher_parse_error_callback(void *arg)
{
    cypher_parse_error_callback_arg *ecb_arg = arg;
    int pos;

    if (geterrcode() == ERRCODE_QUERY_CANCELED)
        return;

    Assert(ecb_arg->query_loc > -1);
    pos = pg_mbstrlen_with_len(ecb_arg->source_str, ecb_arg->query_loc);
    errposition(pos + geterrposition());
}

static Query *parse_and_analyze_cypher(const char *query_str, Param *params)
{
    List *stmt;
    ParseState *pstate;
    Query *query;

    stmt = parse_cypher(query_str);

    pstate = make_parsestate(NULL);
    pstate->p_sourcetext = query_str;

    /*
     * In order to avoid using a global variable in the cypher_expr.c file or
     * tightly coupling the grammar logic with the transform logic, we are
     * using the p_ref_hook_state variable in the ParseState to hold then
     * information we need to handle cypher parameters. The side effect of
     * this is we can no longer support SQL subqueries in a Cypher query.
     */
    pstate->p_ref_hook_state = params;

    query = transform_cypher_stmt(pstate, stmt);

    free_parsestate(pstate);

    return query;
}

static void check_result_type(Query *query, RangeTblFunction *rtfunc,
                              ParseState *pstate)
{
    ListCell *lc;
    ListCell *lc1;
    ListCell *lc2;
    ListCell *lc3;

    if (list_length(query->targetList) != rtfunc->funccolcount)
    {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("return row and column definition list do not match"),
                 parser_errposition(pstate, exprLocation(rtfunc->funcexpr))));
    }

    // NOTE: Implement automatic type coercion instead of this.
    lc1 = list_head(rtfunc->funccoltypes);
    lc2 = list_head(rtfunc->funccoltypmods);
    lc3 = list_head(rtfunc->funccolcollations);
    foreach (lc, query->targetList)
    {
        TargetEntry *te = lfirst(lc);
        Node *expr = (Node *)te->expr;

        Assert(!te->resjunk);

        if (exprType(expr) != lfirst_oid(lc1) ||
            exprTypmod(expr) != lfirst_int(lc2) ||
            exprCollation(expr) != lfirst_oid(lc3))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_DATATYPE_MISMATCH),
                     errmsg("return row and column definition list do not match"),
                     parser_errposition(pstate, exprLocation(rtfunc->funcexpr))));
        }

        lc1 = lnext(lc1);
        lc2 = lnext(lc2);
        lc3 = lnext(lc3);
    }
}

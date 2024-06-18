/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "postgres.h"

#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "parser/analyze.h"
#include "parser/parse_coerce.h"
#include "parser/parse_collate.h"
#include "parser/parse_relation.h"
#include "parser/parse_target.h"
#include "utils/builtins.h"

#include "catalog/ag_graph.h"
#include "parser/cypher_analyze.h"
#include "parser/cypher_clause.h"
#include "parser/cypher_parser.h"
#include "utils/ag_func.h"
#include "utils/age_session_info.h"

typedef bool (*cypher_expression_condition)(Node *expr);

/*
 * extra_node is a global variable to this source to store, at the moment, the
 * explain stmt node passed up by the parser. The return value from the parser
 * contains an 'extra' value, hence the name.
 */
static Node *extra_node = NULL;
/*
 * Takes a query node and builds an explain stmt query node. It then replaces
 * the passed query node with the new explain stmt query node.
 */
static void build_explain_query(Query *query, Node *explain_node);

static post_parse_analyze_hook_type prev_post_parse_analyze_hook;

static void post_parse_analyze(ParseState *pstate, Query *query, JumbleState *jstate);
static bool convert_cypher_walker(Node *node, ParseState *pstate);
static bool is_rte_cypher(RangeTblEntry *rte);
static bool is_func_cypher(FuncExpr *funcexpr);
static void convert_cypher_to_subquery(RangeTblEntry *rte, ParseState *pstate);
static Name expr_get_const_name(Node *expr);
static const char *expr_get_const_cstring(Node *expr, const char *source_str);
static int get_query_location(const int location, const char *source_str);
static Query *analyze_cypher(List *stmt, ParseState *parent_pstate,
                             const char *query_str, int query_loc,
                             char *graph_name, uint32 graph_oid, Param *params);
static Query *analyze_cypher_and_coerce(List *stmt, RangeTblFunction *rtfunc,
                                        ParseState *parent_pstate,
                                        const char *query_str, int query_loc,
                                        char *graph_name, uint32 graph_oid,
                                        Param *params);
cypher_clause *build_subquery_node(cypher_clause *next);

/* expr tree walker */
bool expr_contains_node(cypher_expression_condition is_expr, Node *expr);
bool expr_has_subquery(Node * expr);


void post_parse_analyze_init(void)
{
    prev_post_parse_analyze_hook = post_parse_analyze_hook;
    post_parse_analyze_hook = post_parse_analyze;
}

void post_parse_analyze_fini(void)
{
    post_parse_analyze_hook = prev_post_parse_analyze_hook;
}

static void post_parse_analyze(ParseState *pstate, Query *query, JumbleState *jstate)
{
    if (prev_post_parse_analyze_hook)
    {
        prev_post_parse_analyze_hook(pstate, query, jstate);
    }

    /*
     * extra_node is set in the parsing stage to keep track of EXPLAIN.
     * So it needs to be set to NULL prior to any cypher parsing.
     */
    extra_node = NULL;

    convert_cypher_walker((Node *)query, pstate);

    /*
     * If there is an extra_node returned, we need to check to see if
     * it is an EXPLAIN.
     */
    if (extra_node != NULL)
    {
        /* process the EXPLAIN node */
        if (nodeTag(extra_node) == T_ExplainStmt)
        {
            build_explain_query(query, extra_node);
        }

        /* reset extra_node */
        pfree(extra_node);
        extra_node = NULL;
    }
}

/* find cypher() calls in FROM clauses and convert them to SELECT subqueries */
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
            /* traverse other RTE_SUBQUERYs */
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

        /*
         * From PG -
         * SQLValueFunction - parameterless functions with special grammar
         *                    productions.
         * CoerceViaIO - represents a type coercion between two types whose textual
         *               representations are compatible
         * Var - expression node representing a variable (ie, a table column)
         * OpExpr - expression node for an operator invocation
         * Const - constant value or expression node
         * BoolExpr - expression node for the basic Boolean operators AND, OR, NOT
         *
         * These are a special case that needs to be ignored.
         *
         */
        if (IsA(funcexpr, SQLValueFunction)
                || IsA(funcexpr, CoerceViaIO)
                || IsA(funcexpr, Var)   || IsA(funcexpr, OpExpr)
                || IsA(funcexpr, Const) || IsA(funcexpr, BoolExpr))
        {
            return false;
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
        bool result = false;
        Query *query = (Query *)node;

        /*
         * If this is a utility command, we need to unwrap the internal query
         * and pass it as the query.
         *
         * NOTE: This code only "knows" about the following utility commands -
         *
         * CREATE TABLE AS
         *
         * Others need to be added on a case by case basis.
         */
        if (query->utilityStmt != NULL &&
            IsA(query->utilityStmt, CreateTableAsStmt))
        {
            CreateTableAsStmt *ctas = (CreateTableAsStmt *)query->utilityStmt;

            if (IsA(ctas->query, Query))
            {
                query = (Query *)ctas->query;
            }
        }

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
        flags = QTW_EXAMINE_RTES_BEFORE | QTW_IGNORE_RT_SUBQUERIES |
                QTW_IGNORE_JOINALIASES;

        /* recurse on query */
        result = query_tree_walker(query, convert_cypher_walker, pstate, flags);

        return result;
    }

    return expression_tree_walker(node, convert_cypher_walker, pstate);
}

/*
 * Takes a query node and builds an explain stmt query node. It then replaces
 * the passed query node with the new explain stmt query node.
 */
static void build_explain_query(Query *query, Node *explain_node)
{
    ExplainStmt *estmt = NULL;
    Query *query_copy = NULL;
    Query *query_node = NULL;

    /*
     * Create a copy of the query node. This is purposely a shallow copy
     * because we are only moving the contents to another pointer.
     */
    query_copy = (Query *) palloc(sizeof(Query));
    memcpy(query_copy, query, sizeof(Query));

    /* build our Explain node and store the query node copy in it */
    estmt = makeNode(ExplainStmt);
    estmt->query = (Node *)query_copy;
    estmt->options = ((ExplainStmt *)explain_node)->options;

    /* build our replacement query node */
    query_node = makeNode(Query);
    query_node->commandType = CMD_UTILITY;
    query_node->utilityStmt = (Node *)estmt;
    query_node->canSetTag = true;

    /* now replace the top query node with our replacement query node */
    memcpy(query, query_node, sizeof(Query));

    /*
     * We need to free and clear the global variable when done. But, not
     * the ExplainStmt options. Those will get freed by PG when the
     * query is deleted.
     */
    ((ExplainStmt *)explain_node)->options = NULL;

    /* we need to free query_node as it is no longer needed */
    pfree(query_node);
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
    /*
     * From PG -
     * SQLValueFunction - parameterless functions with special grammar
     *                    productions.
     * CoerceViaIO - represents a type coercion between two types whose textual
     *               representations are compatible
     * Var - expression node representing a variable (ie, a table column)
     * OpExpr - expression node for an operator invocation
     * Const - constant value or expression node
     * BoolExpr - expression node for the basic Boolean operators AND, OR, NOT
     *
     * These are a special case that needs to be ignored.
     *
     */
    if (IsA(funcexpr, SQLValueFunction)
            || IsA(funcexpr, CoerceViaIO)
            || IsA(funcexpr, Var)   || IsA(funcexpr, OpExpr)
            || IsA(funcexpr, Const) || IsA(funcexpr, BoolExpr))
    {
        return false;
    }

    return is_oid_ag_func(funcexpr->funcid, "cypher");
}

/* convert cypher() call to SELECT subquery in-place */
static void convert_cypher_to_subquery(RangeTblEntry *rte, ParseState *pstate)
{
    RangeTblFunction *rtfunc = linitial(rte->functions);
    FuncExpr *funcexpr = (FuncExpr *)rtfunc->funcexpr;
    Node *arg1 = NULL;
    Node *arg2 = NULL;
    Node *arg3 = NULL;
    Name graph_name = NULL;
    char *graph_name_str = NULL;
    Oid graph_oid = InvalidOid;
    const char *query_str = NULL;
    int query_loc = -1;
    Param *params = NULL;
    errpos_ecb_state ecb_state = {{0}};
    List *stmt = NULL;
    Query *query = NULL;

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

    /* verify that we have 2 input parameters as it is possible to get 1 or 0 */
    if (funcexpr->args == NULL || list_length(funcexpr->args) < 2)
    {
        ereport(ERROR,
                (errcode(ERRCODE_SYNTAX_ERROR),
                 errmsg("cypher function requires a minimum of 2 arguments"),
                 parser_errposition(pstate, -1)));
    }

    /* get our first 2 arguments */
    arg1 = linitial(funcexpr->args);
    arg2 = lsecond(funcexpr->args);

    Assert(exprType(arg1) == NAMEOID);
    Assert(exprType(arg2) == CSTRINGOID);

    graph_name = expr_get_const_name(arg1);

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
    query_str = expr_get_const_cstring(arg2, pstate->p_sourcetext);

    /*
     * Validate appropriate cypher function usage -
     *
     * Session info OVERRIDES ANY INPUT PASSED and if any is passed, it will
     * cause the cypher function to error out.
     *
     * If this is using session info, both of the first 2 input parameters need
     * to be NULL, in addition to the session info being set up. Furthermore,
     * the input parameters passed in by session info need to both be non-NULL.
     *
     * If this is not using session info, both input parameters need to be
     * non-NULL.
     *
     */
    if (is_session_info_prepared())
    {
        /* check to see if either input parameter is non-NULL*/
        if (graph_name != NULL || query_str != NULL)
        {
            Node *arg = (graph_name == NULL) ? arg1 : arg2;

            /*
             * Make sure to clean up session info because the ereport will
             * cause the function to exit.
             */
            reset_session_info();

            ereport(ERROR,
                    (errcode(ERRCODE_SYNTAX_ERROR),
                     errmsg("session info requires cypher(NULL, NULL) to be passed"),
                     parser_errposition(pstate, exprLocation(arg))));
        }
        /* get our input parameters from session info */
        else
        {
            graph_name_str = get_session_info_graph_name();
            query_str = get_session_info_cypher_statement();

            /* check to see if either are NULL */
            if (graph_name_str == NULL || query_str == NULL)
            {
                /*
                 * Make sure to clean up session info because the ereport will
                 * cause the function to exit.
                 */
                reset_session_info();

                ereport(ERROR,
                    (errcode(ERRCODE_SYNTAX_ERROR),
                     errmsg("both session info parameters need to be non-NULL"),
                     parser_errposition(pstate, -1)));
            }
        }
    }
    /* otherwise, we get the parameters from the passed function input */
    else
    {
        /* get the graph name string from the passed parameters */
        if (!graph_name)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_SYNTAX_ERROR),
                     errmsg("a name constant is expected"),
                     parser_errposition(pstate, exprLocation(arg1))));
        }
        else
        {
            graph_name_str = NameStr(*graph_name);
        }
        /* get the query string from the passed parameters */
        if (!query_str)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_SYNTAX_ERROR),
                     errmsg("a dollar-quoted string constant is expected"),
                     parser_errposition(pstate, exprLocation(arg2))));
        }
    }

    /*
     * The session info is only valid for one cypher call. Now that we are done
     * with it, if it was used, we need to reset it to free the memory used.
     * Additionally, the query location is dependent on how we got the query
     * string, so set the location accordingly.
     */
    if (is_session_info_prepared())
    {
        reset_session_info();
        query_loc = 0;
    }
    else
    {
        /* this call will crash if we use session info */
        query_loc = get_query_location(((Const *)arg2)->location,
                                       pstate->p_sourcetext);
    }

    /* validate the graph exists */
    graph_oid = get_graph_oid(graph_name_str);
    if (!OidIsValid(graph_oid))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_SCHEMA),
                 errmsg("graph \"%s\" does not exist", graph_name_str),
                 parser_errposition(pstate, exprLocation(arg1))));
    }

    /*
     * Check to see if the cypher function had a third parameter passed to it,
     * if so make sure Postgres parsed the second argument to a Param node.
     */
    if (list_length(funcexpr->args) == 3)
    {
        arg3 = lthird(funcexpr->args);
        if (!IsA(arg3, Param))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("third argument of cypher function must be a parameter"),
                     parser_errposition(pstate, exprLocation(arg3))));
        }

        params = (Param *)arg3;
    }
    else
    {
        params = NULL;
    }

    /*
     * install error context callback to adjust an error position for
     * parse_cypher() since locations that parse_cypher() stores are 0 based
     */
    setup_errpos_ecb(&ecb_state, pstate, query_loc);

    stmt = parse_cypher(query_str);

    /*
     * Extract any extra node passed up and assign it to the global variable
     * 'extra_node' - if it wasn't already set. It will be at the end of the
     * stmt list and needs to be removed for normal processing, regardless.
     * It is done this way to allow utility commands to be processed against the
     * AGE query tree. Currently, only EXPLAIN is passed here. But, it need not
     * just be EXPLAIN - so long as it is carefully documented and carefully
     * done.
     */
    if (extra_node == NULL)
    {
        extra_node = llast(stmt);
        stmt = list_delete_ptr(stmt, extra_node);
    }
    else
    {
        Node *temp = llast(stmt);

        ereport(WARNING,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("too many extra_nodes passed from parser")));

        stmt = list_delete_ptr(stmt, temp);
    }

    cancel_errpos_ecb(&ecb_state);

    Assert(pstate->p_expr_kind == EXPR_KIND_NONE);
    pstate->p_expr_kind = EXPR_KIND_FROM_SUBSELECT;
    /* transformRangeFunction() always sets p_lateral_active to true. */
    /* FYI, rte is RTE_FUNCTION and is being converted to RTE_SUBQUERY here. */
    pstate->p_lateral_active = true;

    /*
     * Cypher queries that end with CREATE clause do not need to have the
     * coercion logic applied to them because we are forcing the column
     * definition list to be a particular way in this case.
     */
    if (is_ag_node(llast(stmt), cypher_create) || is_ag_node(llast(stmt), cypher_set) ||
        is_ag_node(llast(stmt), cypher_delete) || is_ag_node(llast(stmt), cypher_merge))
    {
        /* column definition list must be ... AS relname(colname agtype) ... */
        if (!(rtfunc->funccolcount == 1 &&
              linitial_oid(rtfunc->funccoltypes) == AGTYPEOID))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_DATATYPE_MISMATCH),
                     errmsg("column definition list for CREATE clause must contain a single agtype attribute"),
                     errhint("... cypher($$ ... CREATE ... $$) AS t(c agtype) ..."),
                     parser_errposition(pstate, exprLocation(rtfunc->funcexpr))));
        }

        query = analyze_cypher(stmt, pstate, query_str, query_loc,
                               graph_name_str, graph_oid, params);
    }
    else
    {
        query = analyze_cypher_and_coerce(stmt, rtfunc, pstate, query_str,
                                          query_loc, graph_name_str, graph_oid,
                                          params);
    }

    pstate->p_lateral_active = false;
    pstate->p_expr_kind = EXPR_KIND_NONE;

    /* rte->functions and rte->funcordinality are kept for debugging. */
    /* rte->alias, rte->eref, and rte->lateral need to be the same. */
    /* rte->inh is always false for both RTE_FUNCTION and RTE_SUBQUERY. */
    /* rte->inFromCl is always true for RTE_FUNCTION. */
    rte->rtekind = RTE_SUBQUERY;
    rte->subquery = query;
}

static Name expr_get_const_name(Node *expr)
{
    Const *con;

    if (!IsA(expr, Const))
        return NULL;

    con = (Const *)expr;
    if (con->constisnull)
        return NULL;

    return DatumGetName(con->constvalue);
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

/*
 * This is a specialized expression tree walker for finding exprs of a specified
 * type. Create a function that checks for the type you want, and this function
 * will iterate through the tree.
 */

bool expr_contains_node(cypher_expression_condition is_expr, Node *expr)
{
    if (!expr)
    {
        return false;
    }

    switch (nodeTag(expr))
    {
    case T_A_Const:
    case T_ColumnRef:
    case T_A_Indirection:
        {
            break;
        }
    case T_A_Expr:
    {
        A_Expr *a = (A_Expr *)expr;

        switch (a->kind)
        {
        case AEXPR_OP:
        case AEXPR_IN:
            {
                if (expr_contains_node(is_expr, a->lexpr) ||
                    expr_contains_node(is_expr, a->rexpr))
                {
                    return true;
                }
                break;
            }
        default:
            ereport(ERROR, (errmsg_internal("unrecognized A_Expr kind: %d",
                                            a->kind)));
        }
        break;
    }
    case T_BoolExpr:
    {
        BoolExpr *b = (BoolExpr *)expr;
        ListCell *la;

        foreach(la, b->args)
        {
            Node *arg = lfirst(la);

            if (expr_contains_node(is_expr, arg))
            {
                return true;
            }
        }
        break;
    }
    case T_NullTest:
    {
        NullTest *n = (NullTest *)expr;

        if (expr_contains_node(is_expr, (Node *)n->arg))
        {
            return true;
        }
        break;
    }
    case T_CaseExpr:
    {
        CaseExpr *cexpr = (CaseExpr *)expr;
        ListCell   *l;

        if (cexpr->arg && expr_contains_node(is_expr, (Node *) cexpr->arg))
        {
            return true;
        }

        foreach(l, cexpr->args)
        {
            CaseWhen *w = lfirst_node(CaseWhen, l);
            Node *warg;

            warg = (Node *) w->expr;

            if (expr_contains_node(is_expr, warg))
            {
                return true;
            }
            warg = (Node *)w->result;

            if (expr_contains_node(is_expr, warg))
            {
                return true;
            }
        }

        if (expr_contains_node(is_expr , (Node *)cexpr->defresult))
        {
            return true;
        }

        break;
    }
    case T_CaseTestExpr:
    {
        break;
    }
    case T_CoalesceExpr:
    {
        CoalesceExpr *cexpr = (CoalesceExpr *) expr;
        ListCell *args;

        foreach(args, cexpr->args)
        {
            Node *e = (Node *)lfirst(args);

            if (expr_contains_node(is_expr, e))
            {
                return true;
            }
        }
        break;
    }
    case T_ExtensibleNode:
    {
        if (is_ag_node(expr, cypher_bool_const))
        {
            return is_expr(expr);
        }
        if (is_ag_node(expr, cypher_integer_const))
        {
            return is_expr(expr);
        }
        if (is_ag_node(expr, cypher_param))
        {
            return is_expr(expr);
        }
        if (is_ag_node(expr, cypher_map))
        {
            cypher_map *cm = (cypher_map *)expr;
            ListCell *le;

            Assert(list_length(cm->keyvals) % 2 == 0);

            le = list_head(cm->keyvals);

            while(le != NULL)
            {
                Node *val;

                le = lnext(cm->keyvals, le);

                val = lfirst(le);

                if (expr_contains_node(is_expr, val))
                {
                    return true;
                }

                le = lnext(cm->keyvals, le);

            }
            break;
        }
        if (is_ag_node(expr, cypher_map_projection))
        {
            cypher_map_projection *cmp = (cypher_map_projection *)expr;
            ListCell *lc;

            foreach(lc, cmp->map_elements)
            {
                cypher_map_projection_element *elem;

                elem = lfirst(lc);

                if (expr_contains_node(is_expr, elem->value))
                {
                    return true;
                }
            }

            break;
        }
        if (is_ag_node(expr, cypher_list))
        {
            cypher_list *cl = (cypher_list *)expr;
            ListCell *le = NULL;

            foreach(le, cl->elems)
            {
                Node *texpr = lfirst(le);

                if (expr_contains_node(is_expr, texpr))
                {
                    return true;
                }
            }
            break;
        }
        if (is_ag_node(expr, cypher_string_match))
        {
            cypher_string_match *csm = (cypher_string_match *)expr;

            if (expr_contains_node(is_expr, csm->lhs) ||
                expr_contains_node(is_expr, csm->rhs))
            {
                return true;
            }
            break;
        }
        if (is_ag_node(expr, cypher_typecast))
        {
            cypher_typecast *t = (cypher_typecast *) expr;

            if (expr_contains_node(is_expr, t->expr))
            {
                return true;
            }
            break;
        }
        if (is_ag_node(expr, cypher_comparison_aexpr))
        {
            cypher_comparison_aexpr *a = (cypher_comparison_aexpr *) expr;

            if (expr_contains_node(is_expr, a->lexpr) ||
                expr_contains_node(is_expr, a->rexpr))
            {
                return true;
            }
            break;
        }
        if (is_ag_node(expr, cypher_comparison_boolexpr))
        {
            cypher_comparison_boolexpr *b = (cypher_comparison_boolexpr *) expr;
            ListCell *la;

            foreach(la, b->args)
            {
                Node *arg = lfirst(la);

                if (expr_contains_node(is_expr, arg))
                {
                    return true;
                }
            }
            break;
        }
        if (is_ag_node(expr, cypher_unwind))
        {
            cypher_unwind* lc = (cypher_unwind *)expr;

            if (expr_contains_node(is_expr, lc->where) ||
                expr_contains_node(is_expr, lc->collect))
            {
                return true;
            }
            break;
        }

        if (is_ag_node(expr, cypher_sub_pattern))
        {
            break;
        }

        if (is_ag_node(expr, cypher_sub_query))
        {
            break;
        }

        ereport(ERROR,
                (errmsg_internal("unrecognized ExtensibleNode: %s",
                                 ((ExtensibleNode *)expr)->extnodename)));

        break;
    }
    case T_FuncCall:
    {
        FuncCall *fn = (FuncCall *)expr;
        ListCell *arg;

        foreach(arg, fn->args)
        {
            Node *farg = NULL;

            farg = (Node *)lfirst(arg);

            if (expr_contains_node(is_expr, farg))
            {
                return true;
            }
        }
        break;
    }
    case T_SubLink:
    {
       SubLink *s = (SubLink *)expr;

        if (expr_contains_node(is_expr, s->subselect))
        {
            return true;
        }
      break;
    }
    default:
        ereport(ERROR, (errmsg_internal("unrecognized node type: %d",
                                        nodeTag(expr))));
    }

    return (is_expr(expr));
}

/*
 * Function that checks if an expr is a cypher_sub_query. Used in tandem with
 * expr_contains_node. Can write more similar to this to find similar nodes.
 */

bool expr_has_subquery(Node * expr)
{
    if (expr == NULL)
    {
        return false;
    }

    if (IsA(expr, ExtensibleNode))
    {
        if (is_ag_node(expr, cypher_sub_query))
        {
            return true;
        }
    }
    return false;
}

/*
 * This function constructs an intermediate WITH node for processing subqueries
 */
cypher_clause *build_subquery_node(cypher_clause *next)
{
    cypher_match *match = (cypher_match *)next->self;
    cypher_clause *where_container_clause;
    cypher_with *with_clause = make_ag_node(cypher_with);
    ColumnRef *cr;
    ResTarget *rt;

    /* construct the column ref star */
    cr = makeNode(ColumnRef);
    cr->fields = list_make1(makeNode(A_Star));
    cr->location = exprLocation((Node *)next);

    /*construct the restarget */
    rt = makeNode(ResTarget);
    rt->name = NULL;
    rt->indirection = NIL;
    rt->val = (Node *)cr;
    rt->location = exprLocation((Node *)next);


    /* construct the with_clause */
    with_clause->items = list_make1(rt);
    with_clause->where = match->where;
    with_clause->subquery_intermediate = true;

    /*
     * create the where container, and set the match (next) as the
     * prev of the where container
     */
    where_container_clause = palloc(sizeof(*where_container_clause));
    where_container_clause->self = (Node *)with_clause;
    where_container_clause->next = NULL;
    where_container_clause->prev = next;

    return where_container_clause;
}

static Query *analyze_cypher(List *stmt, ParseState *parent_pstate,
                             const char *query_str, int query_loc,
                             char *graph_name, uint32 graph_oid, Param *params)
{
    cypher_clause *clause;
    ListCell *lc;
    cypher_parsestate parent_cpstate;
    cypher_parsestate *cpstate;
    ParseState *pstate;
    errpos_ecb_state ecb_state;
    Query *query;

    /*
     * Since the first clause in stmt is the innermost subquery, the order of
     * the clauses is inverted.
     */
    clause = NULL;
    foreach (lc, stmt)
    {
        cypher_clause *next;

        next = palloc(sizeof(*next));
        next->next = NULL;
        next->self = lfirst(lc);
        next->prev = clause;

        /* check for subqueries in match */
        if (is_ag_node(next->self, cypher_match))
        {
            cypher_match *match = (cypher_match *)next->self;

            if (match->where != NULL && expr_contains_node(expr_has_subquery, match->where))
            {
               /* advance the clause iterator to the intermediate clause position */
               clause = build_subquery_node(next);

               /* set the next of the match to the where_container_clause */
               match->where = NULL;
               next->next = clause;
               continue;
            }
        }

        if (clause != NULL)
        {
            clause->next = next;
        }
        clause = next;
    }

    /*
     * convert ParseState into cypher_parsestate temporarily to pass it to
     * make_cypher_parsestate()
     */
    parent_cpstate.pstate = *parent_pstate;
    parent_cpstate.graph_name = NULL;
    parent_cpstate.params = NULL;

    cpstate = make_cypher_parsestate(&parent_cpstate);

    pstate = (ParseState *)cpstate;

    /* we don't want functions that go up the pstate parent chain to access the
     * original SQL query pstate.
     */
    pstate->parentParseState = NULL;
    /*
     * override p_sourcetext with query_str to make parser_errposition() work
     * correctly with errpos_ecb()
     */
    pstate->p_sourcetext = query_str;

    cpstate->graph_name = graph_name;
    cpstate->graph_oid = graph_oid;
    cpstate->params = params;
    cpstate->default_alias_num = 0;
    cpstate->entities = NIL;
    cpstate->subquery_where_flag = false;
    /*
     * install error context callback to adjust an error position since
     * locations in stmt are 0 based
     */
    setup_errpos_ecb(&ecb_state, parent_pstate, query_loc);

    query = transform_cypher_clause(cpstate, clause);

    cancel_errpos_ecb(&ecb_state);

    free_cypher_parsestate(cpstate);

    return query;
}

/*
 * Since some target entries of subquery may be referenced for sorting (ORDER
 * BY), we cannot apply the coercion directly to the expressions of the target
 * entries. Therefore, we do the coercion by doing SELECT over subquery.
 */
static Query *analyze_cypher_and_coerce(List *stmt, RangeTblFunction *rtfunc,
                                        ParseState *parent_pstate,
                                        const char *query_str, int query_loc,
                                        char *graph_name, uint32 graph_oid,
                                        Param *params)
{
    ParseState *pstate;
    Query *query;
    const bool lateral = false;
    Query *subquery;
    ParseNamespaceItem *pnsi;
    int rtindex;
    ListCell *lt;
    ListCell *lc1;
    ListCell *lc2;
    ListCell *lc3;

    int attr_count = 0;

    pstate = make_parsestate(parent_pstate);

    query = makeNode(Query);
    query->commandType = CMD_SELECT;

    /*
     * Below is similar to transform_prev_cypher_clause().
     */

    Assert(pstate->p_expr_kind == EXPR_KIND_NONE);
    pstate->p_expr_kind = EXPR_KIND_FROM_SUBSELECT;
    pstate->p_lateral_active = lateral;

    subquery = analyze_cypher(stmt, pstate, query_str, query_loc, graph_name,
                              graph_oid, (Param *)params);

    pstate->p_lateral_active = false;
    pstate->p_expr_kind = EXPR_KIND_NONE;

    /* ALIAS Syntax makes `RESJUNK`. So, It must be skipping. */
    foreach(lt, subquery->targetList)
    {
        TargetEntry *te = lfirst(lt);
        if (!te->resjunk)
        {
            attr_count++;
        }
    }

    /* check the number of attributes first */
    if (attr_count != rtfunc->funccolcount)
    {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("return row and column definition list do not match"),
                 parser_errposition(pstate, exprLocation(rtfunc->funcexpr))));
    }

    pnsi = addRangeTableEntryForSubquery(pstate, subquery, makeAlias("_", NIL),
                                        lateral, true);

    rtindex = list_length(pstate->p_rtable);
    /* rte is the only RangeTblEntry in pstate */
    if (rtindex !=1 )
    {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("invalid value for rtindex")));
    }


    addNSItemToQuery(pstate, pnsi, true, true, true);
    query->targetList = expandNSItemAttrs(pstate, pnsi, 0, true, -1);

    markTargetListOrigins(pstate, query->targetList);

    /* do the type coercion for each target entry */
    lc1 = list_head(rtfunc->funccolnames);
    lc2 = list_head(rtfunc->funccoltypes);
    lc3 = list_head(rtfunc->funccoltypmods);
    foreach (lt, query->targetList)
    {
        TargetEntry *te = lfirst(lt);
        Node *expr = (Node *)te->expr;
        Oid current_type;
        Oid target_type;

        Assert(!te->resjunk);

        current_type = exprType(expr);
        target_type = lfirst_oid(lc2);
        if (current_type != target_type)
        {
            int32 target_typmod = lfirst_int(lc3);
            Node *new_expr;

            /*
             * The coercion context of this coercion is COERCION_EXPLICIT
             * because the target type is explicitly mentioned in the column
             * definition list and we need to do this by looking up all
             * possible coercion.
             */
            new_expr = coerce_to_target_type(pstate, expr, current_type,
                                             target_type, target_typmod,
                                             COERCION_EXPLICIT,
                                             COERCE_EXPLICIT_CAST, -1);
            if (!new_expr)
            {
                char *colname = strVal(lfirst(lc1));

                ereport(ERROR,
                        (errcode(ERRCODE_CANNOT_COERCE),
                         errmsg("cannot cast type %s to %s for column \"%s\"",
                                format_type_be(current_type),
                                format_type_be(target_type), colname),
                         parser_errposition(pstate,
                                            exprLocation(rtfunc->funcexpr))));
            }

            te->expr = (Expr *)new_expr;
        }

        lc1 = lnext(rtfunc->funccolnames, lc1);
        lc2 = lnext(rtfunc->funccoltypes, lc2);
        lc3 = lnext(rtfunc->funccoltypmods, lc3);
    }

    query->rtable = pstate->p_rtable;
    query->jointree = makeFromExpr(pstate->p_joinlist, NULL);

    assign_query_collations(pstate, query);

    free_parsestate(pstate);

    return query;
}

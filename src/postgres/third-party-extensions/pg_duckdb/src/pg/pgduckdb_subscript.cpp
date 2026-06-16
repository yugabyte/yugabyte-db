#include "pgduckdb/pgduckdb_metadata_cache.hpp"
#include "pgduckdb/utility/cpp_wrapper.hpp"

extern "C" {
#include "postgres.h"
#include "executor/execExpr.h"
#include "parser/parse_coerce.h"
#include "parser/parse_node.h"
#include "parser/parse_expr.h"
#include "nodes/subscripting.h"
#include "nodes/nodeFuncs.h"
#include "pgduckdb/vendor/pg_list.hpp"
}

namespace pgduckdb {

namespace pg {

Node *
CoerceSubscriptToText(struct ParseState *pstate, A_Indices *subscript, const char *type_name) {
	if (!subscript->uidx) {
		elog(ERROR, "Creating a slice out of %s is not supported", type_name);
	}

	Node *subscript_expr = transformExpr(pstate, subscript->uidx, pstate->p_expr_kind);
	int expr_location = exprLocation(subscript->uidx);
	Oid subscript_expr_type = exprType(subscript_expr);

	if (subscript->lidx) {
		elog(ERROR, "Creating a slice out of %s is not supported", type_name);
	}

	Node *coerced_expr = coerce_to_target_type(pstate, subscript_expr, subscript_expr_type, TEXTOID, -1,
	                                           COERCION_IMPLICIT, COERCE_IMPLICIT_CAST, expr_location);
	if (!coerced_expr) {
		ereport(ERROR, (errcode(ERRCODE_DATATYPE_MISMATCH), errmsg("%s subscript must have text type", type_name),
		                parser_errposition(pstate, expr_location)));
	}

	if (!IsA(subscript_expr, Const)) {
		ereport(ERROR, (errcode(ERRCODE_DATATYPE_MISMATCH), errmsg("%s subscript must be a constant", type_name),
		                parser_errposition(pstate, expr_location)));
	}

	Const *subscript_const = castNode(Const, subscript_expr);
	if (subscript_const->constisnull) {
		ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED), errmsg("%s subscript cannot be NULL", type_name),
		                parser_errposition(pstate, expr_location)));
	}

	return coerced_expr;
}

/*
 * In Postgres all index operations in a row are all slices or all plain
 * index operations. If you mix them, all are converted to slices.
 * There's no difference in representation possible between
 * "col[1:2][1]" and "col[1:2][1:]". If you want this separation you
 * need to use parenthesis to separate: "(col[1:2])[1]"
 * This might seem like fairly strange behaviour, but Postgres uses
 * this to be able to slice in multi-dimensional arrays and this
 * behaviour is documented here:
 * https://www.postgresql.org/docs/current/arrays.html#ARRAYS-ACCESSING
 *
 * This is different from DuckDB, but there's not much we can do about
 * that. So we'll have this same behaviour by, which means we need to always
 * add the lower subscript to the slice. The lower subscript will be NULL in
 * that case.
 *
 * See also comments on SubscriptingRef in nodes/subscripting.h
 */
void
AddSubscriptExpressions(SubscriptingRef *sbsref, struct ParseState *pstate, A_Indices *subscript, bool is_slice) {
	Assert(is_slice || subscript->uidx);

	Node *upper_subscript_expr = NULL;
	if (subscript->uidx) {
		upper_subscript_expr = transformExpr(pstate, subscript->uidx, pstate->p_expr_kind);
	}

	sbsref->refupperindexpr = lappend(sbsref->refupperindexpr, upper_subscript_expr);

	if (is_slice) {
		Node *lower_subscript_expr = NULL;
		if (subscript->uidx) {
			lower_subscript_expr = transformExpr(pstate, subscript->lidx, pstate->p_expr_kind);
		}
		sbsref->reflowerindexpr = lappend(sbsref->reflowerindexpr, lower_subscript_expr);
	}
}

/*
 * DuckdbSubscriptTransform is called by the parser when a subscripting
 * operation is performed on a duckdb type that can be indexed by arbitrary
 * expressions. All this does is parse those expressions and make sure the
 * subscript returns an an duckdb.unresolved_type again.
 */
void
DuckdbSubscriptTransform(SubscriptingRef *sbsref, List *indirection, struct ParseState *pstate, bool is_slice,
                         bool is_assignment, const char *type_name) {
	/*
	 * We need to populate our cache for some of the code below. Normally this
	 * cache is populated at the start of our planner hook, but this function
	 * is being called from the parser.
	 */
	if (!pgduckdb::IsExtensionRegistered()) {
		elog(ERROR, "BUG: Using %s but the pg_duckdb extension is not installed", type_name);
	}

	if (is_assignment) {
		elog(ERROR, "Assignment to %s is not supported", type_name);
	}

	if (indirection == NIL) {
		elog(ERROR, "Subscripting %s with an empty subscript is not supported", type_name);
	}

	// Transform each subscript expression
	foreach_node(A_Indices, subscript, indirection) {
		AddSubscriptExpressions(sbsref, pstate, subscript, is_slice);
	}

	// Set the result type of the subscripting operation
	sbsref->refrestype = pgduckdb::DuckdbUnresolvedTypeOid();
	sbsref->reftypmod = -1;
}

/*
 * DuckdbTextSubscriptTransform is called by the parser when a subscripting
 * operation is performed on type that can only be indexed by string literals.
 * It has two main puprposes:
 * 1. Ensure that the row is being indexed using a string literal
 * 2. Ensure that the return type of this index operation is
 *    duckdb.unresolved_type
 *
 * Currently this is used for duckdb.row and duckdb.struct types.
 */
void
DuckdbTextSubscriptTransform(SubscriptingRef *sbsref, List *indirection, struct ParseState *pstate, bool is_slice,
                             bool is_assignment, const char *type_name) {
	/*
	 * We need to populate our cache for some of the code below. Normally this
	 * cache is populated at the start of our planner hook, but this function
	 * is being called from the parser.
	 */
	if (!pgduckdb::IsExtensionRegistered()) {
		elog(ERROR, "BUG: Using %s but the pg_duckdb extension is not installed", type_name);
	}

	if (is_assignment) {
		elog(ERROR, "Assignment to %s is not supported", type_name);
	}

	if (indirection == NIL) {
		elog(ERROR, "Subscripting %s with an empty subscript is not supported", type_name);
	}

	bool first = true;

	// Transform each subscript expression
	foreach_node(A_Indices, subscript, indirection) {
		/* The first subscript needs to be a TEXT constant, since it should be
		 * a column reference. But the subscripts after that can be anything,
		 * DuckDB should interpret those. */
		if (first) {
			sbsref->refupperindexpr =
			    lappend(sbsref->refupperindexpr, CoerceSubscriptToText(pstate, subscript, type_name));
			if (is_slice) {
				sbsref->reflowerindexpr = lappend(sbsref->reflowerindexpr, NULL);
			}
			first = false;
			continue;
		}

		AddSubscriptExpressions(sbsref, pstate, subscript, is_slice);
	}

	// Set the result type of the subscripting operation
	sbsref->refrestype = pgduckdb::DuckdbUnresolvedTypeOid();
	sbsref->reftypmod = -1;
}

static bool
DuckdbSubscriptCheckSubscripts(ExprState * /*state*/, ExprEvalStep *op, ExprContext * /*econtext*/) {
	SubscriptingRefState *sbsrefstate = op->d.sbsref_subscript.state;
	char *type_name = strVal(sbsrefstate->workspace);
	elog(ERROR, "Subscripting %s is not supported in the Postgres Executor", type_name);
}

static void
DuckdbSubscriptFetch(ExprState * /*state*/, ExprEvalStep *op, ExprContext * /*econtext*/) {
	SubscriptingRefState *sbsrefstate = op->d.sbsref_subscript.state;
	char *type_name = strVal(sbsrefstate->workspace);
	elog(ERROR, "Subscripting %s is not supported in the Postgres Executor", type_name);
}

static void
DuckdbSubscriptAssign(ExprState * /*state*/, ExprEvalStep *op, ExprContext * /*econtext*/) {
	SubscriptingRefState *sbsrefstate = op->d.sbsref_subscript.state;
	char *type_name = strVal(sbsrefstate->workspace);
	elog(ERROR, "Subscripting %s is not supported in the Postgres Executor", type_name);
}

static void
DuckdbSubscriptFetchOld(ExprState * /*state*/, ExprEvalStep *op, ExprContext * /*econtext*/) {
	SubscriptingRefState *sbsrefstate = op->d.sbsref_subscript.state;
	char *type_name = strVal(sbsrefstate->workspace);
	elog(ERROR, "Subscripting %s is not supported in the Postgres Executor", type_name);
}

/*
 * DuckdbSubscriptExecSetup stores a bunch of functions in the methods
 * structure. These functions are called by the Postgres executor when a
 * subscripting is executed. We need to implement this function, because it is
 * called for materialized CTEs. Even in that case the actual functions that
 * are stored in methods are never supposed to be called, because pg_duckdb
 * shouldn't force usage of DuckDB execution when duckdb types are present in
 * the query. So these methods are just stubs that throw an error when called.
 */
void
DuckdbSubscriptExecSetup(const SubscriptingRef * /*sbsref*/, SubscriptingRefState *sbsrefstate,
                         SubscriptExecSteps *methods, const char *type_name) {

	sbsrefstate->workspace = makeString(pstrdup(type_name));
	methods->sbs_check_subscripts = DuckdbSubscriptCheckSubscripts;
	methods->sbs_fetch = DuckdbSubscriptFetch;
	methods->sbs_assign = DuckdbSubscriptAssign;
	methods->sbs_fetch_old = DuckdbSubscriptFetchOld;
}

void
DuckdbRowSubscriptTransform(SubscriptingRef *sbsref, List *indirection, struct ParseState *pstate, bool is_slice,
                            bool is_assignment) {
	DuckdbTextSubscriptTransform(sbsref, indirection, pstate, is_slice, is_assignment, "duckdb.row");
}

void
DuckdbRowSubscriptExecSetup(const SubscriptingRef *sbsref, SubscriptingRefState *sbsrefstate,
                            SubscriptExecSteps *methods) {
	DuckdbSubscriptExecSetup(sbsref, sbsrefstate, methods, "duckdb.row");
}

static SubscriptRoutines duckdb_row_subscript_routines = {
    .transform = DuckdbRowSubscriptTransform,
    .exec_setup = DuckdbRowSubscriptExecSetup,
    .fetch_strict = false,
    .fetch_leakproof = true,
    .store_leakproof = true,
};

void
DuckdbUnresolvedTypeSubscriptTransform(SubscriptingRef *sbsref, List *indirection, struct ParseState *pstate,
                                       bool is_slice, bool is_assignment) {
	DuckdbSubscriptTransform(sbsref, indirection, pstate, is_slice, is_assignment, "duckdb.unresolved_type");
}

void
DuckdbUnresolvedTypeSubscriptExecSetup(const SubscriptingRef *sbsref, SubscriptingRefState *sbsrefstate,
                                       SubscriptExecSteps *methods) {
	DuckdbSubscriptExecSetup(sbsref, sbsrefstate, methods, "duckdb.unresolved_type");
}

static SubscriptRoutines duckdb_unresolved_type_subscript_routines = {
    .transform = DuckdbUnresolvedTypeSubscriptTransform,
    .exec_setup = DuckdbUnresolvedTypeSubscriptExecSetup,
    .fetch_strict = false,
    .fetch_leakproof = true,
    .store_leakproof = true,
};

void
DuckdbStructSubscriptTransform(SubscriptingRef *sbsref, List *indirection, struct ParseState *pstate, bool is_slice,
                               bool is_assignment) {
	DuckdbTextSubscriptTransform(sbsref, indirection, pstate, is_slice, is_assignment, "duckdb.struct");
}

void
DuckdbStructSubscriptExecSetup(const SubscriptingRef *sbsref, SubscriptingRefState *sbsrefstate,
                               SubscriptExecSteps *methods) {
	DuckdbSubscriptExecSetup(sbsref, sbsrefstate, methods, "duckdb.struct");
}

static SubscriptRoutines duckdb_struct_subscript_routines = {
    .transform = DuckdbStructSubscriptTransform,
    .exec_setup = DuckdbStructSubscriptExecSetup,
    .fetch_strict = false,
    .fetch_leakproof = true,
    .store_leakproof = true,
};

void
DuckdbMapSubscriptTransform(SubscriptingRef *sbsref, List *indirection, struct ParseState *pstate, bool is_slice,
                            bool is_assignment) {
	DuckdbSubscriptTransform(sbsref, indirection, pstate, is_slice, is_assignment, "duckdb.map");
}

void
DuckdbMapSubscriptExecSetup(const SubscriptingRef *sbsref, SubscriptingRefState *sbsrefstate,
                            SubscriptExecSteps *methods) {
	DuckdbSubscriptExecSetup(sbsref, sbsrefstate, methods, "duckdb.map");
}

static SubscriptRoutines duckdb_map_subscript_routines = {
    .transform = DuckdbMapSubscriptTransform,
    .exec_setup = DuckdbMapSubscriptExecSetup,
    .fetch_strict = false,
    .fetch_leakproof = true,
    .store_leakproof = true,
};

} // namespace pg

} // namespace pgduckdb

extern "C" {

DECLARE_PG_FUNCTION(duckdb_row_subscript) {
	PG_RETURN_POINTER(&pgduckdb::pg::duckdb_row_subscript_routines);
}

DECLARE_PG_FUNCTION(duckdb_unresolved_type_subscript) {
	PG_RETURN_POINTER(&pgduckdb::pg::duckdb_unresolved_type_subscript_routines);
}

DECLARE_PG_FUNCTION(duckdb_struct_subscript) {
	PG_RETURN_POINTER(&pgduckdb::pg::duckdb_struct_subscript_routines);
}

DECLARE_PG_FUNCTION(duckdb_map_subscript) {
	PG_RETURN_POINTER(&pgduckdb::pg::duckdb_map_subscript_routines);
}
}

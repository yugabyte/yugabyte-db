/*-------------------------------------------------------------------------
 *
 * pg_list.hpp
 *	  PG17 list macros from pg_list.h backported to lower versions, with a
 *	  small modification to make foreach_ptr work in C++ (which has stricter
 *	  casting rules from void *)
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "c.h"

#include "nodes/pg_list.h"
/*
 * Remove the original definition of foreach_delete_current so we can redefine
 * it below in a way that works for the easier to use foreach_* macros.
 */
#undef foreach_delete_current

#if PG_VERSION_NUM >= 170000
#undef foreach_ptr
#undef foreach_int
#undef foreach_oid
#undef foreach_xid
#undef foreach_internal
#undef foreach_node
#endif

// clang-format off
/*
 * foreach_delete_current -
 *	  delete the current list element from the List associated with a
 *	  surrounding foreach() or foreach_*() loop, returning the new List
 *	  pointer; pass the name of the iterator variable.
 *
 * This is similar to list_delete_cell(), but it also adjusts the loop's state
 * so that no list elements will be missed.  Do not delete elements from an
 * active foreach or foreach_* loop's list in any other way!
 */
#define foreach_delete_current(lst, var_or_cell)	\
	((List *) (var_or_cell##__state.l = list_delete_nth_cell(lst, var_or_cell##__state.i--)))

/*
 * Convenience macros that loop through a list without needing a separate
 * "ListCell *" variable.  Instead, the macros declare a locally-scoped loop
 * variable with the provided name and the appropriate type.
 *
 * Since the variable is scoped to the loop, it's not possible to detect an
 * early break by checking its value after the loop completes, as is common
 * practice.  If you need to do this, you can either use foreach() instead or
 * manually track early breaks with a separate variable declared outside of the
 * loop.
 *
 * Note that the caveats described in the comment above the foreach() macro
 * also apply to these convenience macros.
 */
#define foreach_ptr(type, var, lst) foreach_internal(type, *, var, lst, lfirst)
#define foreach_int(var, lst)	foreach_internal(int, , var, lst, lfirst_int)
#define foreach_oid(var, lst)	foreach_internal(Oid, , var, lst, lfirst_oid)
#define foreach_xid(var, lst)	foreach_internal(TransactionId, , var, lst, lfirst_xid)

/*
 * The internal implementation of the above macros.  Do not use directly.
 *
 * This macro actually generates two loops in order to declare two variables of
 * different types.  The outer loop only iterates once, so we expect optimizing
 * compilers will unroll it, thereby optimizing it away.
 */
#define foreach_internal(type, pointer, var, lst, func) \
	for (type pointer var = 0, pointer var##__outerloop = (type pointer) 1; \
		 var##__outerloop; \
		 var##__outerloop = 0) \
		for (ForEachState var##__state = {(lst), 0}; \
			 (var##__state.l != NIL && \
			  var##__state.i < var##__state.l->length && \
			 (var = (type pointer) func(&var##__state.l->elements[var##__state.i]), true)); \
			 var##__state.i++)

/*
 * foreach_node -
 *	  The same as foreach_ptr, but asserts that the element is of the specified
 *	  node type.
 */
#define foreach_node(type, var, lst) \
	for (type * var = 0, *var##__outerloop = (type *) 1; \
		 var##__outerloop; \
		 var##__outerloop = 0) \
		for (ForEachState var##__state = {(lst), 0}; \
			 (var##__state.l != NIL && \
			  var##__state.i < var##__state.l->length && \
			 (var = lfirst_node(type, &var##__state.l->elements[var##__state.i]), true)); \
			 var##__state.i++)
// clang-format on

/*-------------------------------------------------------------------------
 *
 * disable_core_macro.h
 *	  Support including tuplesort.c from postgresql core code.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Copyright (c) 2022, Postgres Professional
 *
 *-------------------------------------------------------------------------
 */

#ifndef __DISABLE_CORE_MACRO_H__
#define __DISABLE_CORE_MACRO_H__

#undef TRACE_SORT
#undef DEBUG_BOUNDED_SORT
#undef TRACE_POSTGRESQL_SORT_START
#undef TRACE_POSTGRESQL_SORT_DONE

#if PG_VERSION_NUM >= 110000
#define TRACE_POSTGRESQL_SORT_START(arg1, arg2, arg3, arg4, arg5, arg6) \
	do { } while (0)
#else
#define TRACE_POSTGRESQL_SORT_START(arg1, arg2, arg3, arg4, arg5) \
	do { } while (0)
#endif


#define TRACE_POSTGRESQL_SORT_DONE(arg1, arg2) \
	do { } while (0)


#endif /* __DISABLE_CORE_MACRO_H__ */

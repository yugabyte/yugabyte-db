#ifndef __PG_SENTINEL_H__
#define __PG_SENTINEL_H__

#include <postgres.h>
#include "parser/analyze.h"

/* Check PostgreSQL version */
#if PG_VERSION_NUM < 90600
        #error "You are trying to build pg_sentinel with PostgreSQL version < 9.6"
#endif

/* Saved hook values in case of unload */
extern post_parse_analyze_hook_type prev_post_parse_analyze_hook;

/* Our hooks */
#if PG_VERSION_NUM < 140000
extern void getparsedinfo_post_parse_analyze(ParseState *pstate, Query *query);
#else
extern void getparsedinfo_post_parse_analyze(ParseState *pstate, Query *query, JumbleState *jstate);
#endif
/* Estimate amount of shared memory needed */
extern Size proc_entry_memsize(void);
extern int get_max_procs_count(void);

typedef struct procEntry
{
        uint64 queryid;
        char *query;
        char *cmdtype;
} procEntry;

extern procEntry *ProcEntryArray;

#endif

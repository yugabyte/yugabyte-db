#include "postgres.h"

#include "fmgr.h"

#include "nodes/ag_nodes.h"
#include "optimizer/cypher_paths.h"
#include "parser/cypher_analyze.h"

PG_MODULE_MAGIC;

void _PG_init(void);

void _PG_init(void)
{
    register_ag_nodes();
    set_rel_pathlist_init();
    post_parse_analyze_init();
}

void _PG_fini(void);

void _PG_fini(void)
{
    post_parse_analyze_fini();
    set_rel_pathlist_fini();
}

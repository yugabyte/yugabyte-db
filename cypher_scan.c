#include "postgres.h"

#include "catalog/namespace.h"
#include "optimizer/tlist.h"
#include "utils/builtins.h"
#include "utils/syscache.h"

#include "cypher_scan.h"
#include "cypher_scan_state.h"

Node *create_cypher_custom_scan_state(CustomScan *cscan);

const CustomScanMethods cypher_create_scan_methods = {
    "Cypher Create Scan Custom Methods", create_cypher_custom_scan_state};

Node *create_cypher_custom_scan_state(CustomScan *cscan)
{
    cypher_create_scan_state *cypher_css =
        palloc(sizeof(cypher_create_scan_state));

    cypher_css->css.ss.ps.type = T_CustomScanState;
    cypher_css->css.ss.ps.plan = (Plan *)cscan;

    // Initialized by ExecInitCustomScan
    cypher_css->css.ss.ps.state = NULL;
    cypher_css->css.ss.ps.ExecProcNode = NULL;
    cypher_css->css.ss.ps.ExecProcNodeReal = NULL;
    cypher_css->css.ss.ps.qual = NULL;
    cypher_css->css.ss.ps.ps_ResultTupleSlot = NULL;
    cypher_css->css.ss.ps.ps_ExprContext = NULL;
    cypher_css->css.ss.ps.scandesc = NULL;
    cypher_css->css.ss.ss_currentRelation = NULL;
    cypher_css->css.ss.ss_currentScanDesc = NULL;
    cypher_css->css.ss.ss_ScanTupleSlot = NULL;
    cypher_css->css.flags = 0;

    // Initialized in ExecInitNode
    cypher_css->css.ss.ps.instrument = NULL;

    // Needed for parallelism only and
    // Initialized in executor/execParallel.c
    cypher_css->css.ss.ps.worker_instrument = NULL;
    cypher_css->css.ss.ps.worker_jit_instrument = NULL;
    cypher_css->css.pscan_len = 0;

    // Not Needed for a basic CREATE
    cypher_css->css.ss.ps.lefttree = NULL;
    cypher_css->css.ss.ps.righttree = NULL;
    cypher_css->css.ss.ps.initPlan = NIL;
    cypher_css->css.ss.ps.subPlan = NIL;
    cypher_css->css.ss.ps.chgParam = NULL;
    cypher_css->css.ss.ps.ps_ProjInfo = NULL;
    cypher_css->css.custom_ps = NIL;

    cypher_css->css.methods = &cypher_create_custom_exec_methods;

    cypher_css->pattern = linitial(cscan->custom_private);

    return (Node *)cypher_css;
}

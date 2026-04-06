/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/explain/model.rs
 *
 *-------------------------------------------------------------------------
 */

use serde::Deserialize;

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "PascalCase")]
pub struct DistributedTask {
    #[serde(rename = "Node")]
    pub _node: String,

    #[serde(rename = "Remote Plan")]
    pub worker_plans: Vec<Vec<PostgresExplain>>,

    #[serde(rename = "Query")]
    pub _query: String,

    pub error: Option<String>,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "PascalCase")]
pub struct DistributedJob {
    #[serde(rename = "Task Count")]
    pub task_count: i32,

    #[serde(rename = "Tasks Shown")]
    pub tasks_shown: String,

    #[serde(rename = "Tuple data received from nodes")]
    pub total_response_size: Option<String>,

    pub tasks: Vec<DistributedTask>,
}

#[derive(Deserialize, Debug, Clone)]
pub struct DistributedSubPlan {
    #[serde(rename = "PlannedStmt")]
    pub statements: Vec<PostgresExplain>,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "PascalCase")]
pub struct DistributedQueryPlan {
    pub job: DistributedJob,
    pub subplans: Option<Vec<DistributedSubPlan>>,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "PascalCase")]
pub struct PostgresExplain {
    pub plan: ExplainPlan,
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(rename_all = "PascalCase")]
pub struct ExplainPlan {
    #[serde(rename = "Actual Rows")]
    pub actual_rows: Option<i64>,

    #[serde(rename = "Actual Total Time")]
    pub actual_total_time: Option<f64>,

    #[serde(rename = "Alias")]
    pub alias: Option<String>,

    #[serde(rename = "Custom Plan Provider")]
    pub custom_plan_provider: Option<String>,

    #[serde(rename = "Distributed Query")]
    pub distributed_plan: Option<DistributedQueryPlan>,

    #[serde(rename = "Execution Time")]
    pub execution_time: Option<f64>,

    pub filter: Option<String>,

    #[serde(rename = "Group Key")]
    pub group_key: Option<Vec<String>>,

    #[serde(rename = "Heap Fetches")]
    pub heap_fetches: Option<i64>,

    #[serde(rename = "Index Cond")]
    pub index_condition: Option<String>,

    #[serde(rename = "Index Name")]
    pub index_name: Option<String>,

    #[serde(rename = "CosmosSearch Custom Params")]
    pub vector_search_custom_params: Option<String>,

    #[serde(rename = "Join Type")]
    pub join_type: Option<String>,

    #[serde(rename = "Node Type")]
    pub node_type: String,

    #[serde(rename = "Order By")]
    pub order_by: Option<String>,

    pub output: Option<Vec<String>>,

    #[serde(rename = "Relation Name")]
    pub relation_name: Option<String>,

    #[serde(rename = "Page Size")]
    pub page_size: Option<i64>,

    #[serde(rename = "Plan Rows")]
    pub plan_rows: Option<serde_json::value::Number>,

    #[serde(rename = "Planning Time")]
    pub planning_time: Option<f64>,

    #[serde(rename = "Plans")]
    pub inner_plans: Option<Vec<ExplainPlan>>,

    #[serde(rename = "Parent Relationship")]
    pub parent_relationship: Option<String>,

    #[serde(rename = "Rows Removed by Filter")]
    pub rows_removed_by_filter: Option<i64>,

    #[serde(rename = "Rows Removed by Index Recheck")]
    pub rows_removed_by_index: Option<i64>,

    #[serde(rename = "Scan Direction")]
    pub scan_direction: Option<String>,

    #[serde(rename = "Sort Key")]
    pub sort_keys: Option<Vec<String>>,

    #[serde(rename = "Presorted Key")]
    pub presorted_key: Option<Vec<String>>,

    #[serde(rename = "Sort Method")]
    pub sort_method: Option<String>,

    #[serde(rename = "Sort Space Type")]
    pub sort_space_type: Option<String>,

    #[serde(rename = "Sort Space Used")]
    pub sort_space_used: Option<i64>,

    #[serde(rename = "Startup Cost")]
    pub startup_cost: Option<f64>,

    #[serde(rename = "Total Cost")]
    pub total_cost: Option<f64>,

    #[serde(rename = "Function Name")]
    pub function_name: Option<String>,

    #[serde(rename = "Exact Heap Blocks")]
    pub exact_heap_blocks: Option<i64>,

    #[serde(rename = "Lossy Heap Blocks")]
    pub lossy_heap_blocks: Option<i64>,

    #[serde(rename = "Shared Hit Blocks")]
    pub shared_hit_blocks: Option<i64>,

    #[serde(rename = "Shared Read Blocks")]
    pub shared_read_blocks: Option<i64>,

    #[serde(rename = "I/O Read Time")]
    pub io_read_time: Option<i64>,

    #[serde(rename = "Workers Launched")]
    pub workers_launched: Option<i64>,

    #[serde(rename = "IndexDetails")]
    pub index_details: Option<Vec<IndexDetails>>,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct VectorSearchParams {
    pub n_probes: Option<f64>,
    pub ef_search: Option<f64>,
    pub l_search: Option<f64>,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct IndexDetails {
    pub index_name: Option<String>,
    pub is_multi_key: Option<bool>,
    pub index_bounds: Option<Vec<String>>,
    pub inner_scan_loops: Option<i64>,
    pub scan_key_details: Option<Vec<String>>,
    pub scan_type: Option<String>,
    pub num_duplicates: Option<i64>,
}

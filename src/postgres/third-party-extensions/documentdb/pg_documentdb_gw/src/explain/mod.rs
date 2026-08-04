/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/explain/mod.rs
 *
 *-------------------------------------------------------------------------
 */

use core::f64;
use std::{cmp::Ordering, collections::HashMap, str::FromStr};

use async_recursion::async_recursion;
use bson::{rawdoc, Document, RawArrayBuf, RawBson, RawDocument, RawDocumentBuf};
use model::*;
use once_cell::sync::Lazy;
use serde_json::Value;

use crate::{
    context::{ConnectionContext, RequestContext},
    error::{DocumentDBError, Result},
    postgres::PgDataClient,
    protocol::OK_SUCCEEDED,
    requests::{Request, RequestInfo, RequestType},
    responses::{RawResponse, Response},
    QueryCatalog,
};

mod model;
mod query_diagnostics;

static MAX_EXPLAIN_BSON_COMMAND_LENGTH: usize = 100 * 1024;

type AggregationStage = (
    &'static str,
    Option<fn(&ExplainPlan, &mut RawDocumentBuf, &QueryCatalog) -> ()>,
);

static AGGREGATION_STAGE_NAME_MAP: Lazy<HashMap<&'static str, AggregationStage>> =
    Lazy::new(|| {
        let project_function: fn(&ExplainPlan, &mut RawDocumentBuf, &QueryCatalog) -> () =
            |p, writer, query_catalog| {
                write_output_stage(
                    |output, query_catalog| {
                        query_diagnostics::get_projection_type_output(
                            output,
                            "project",
                            query_catalog,
                        )
                    },
                    query_catalog,
                    p,
                    writer,
                )
            };
        let add_fields_function: fn(&ExplainPlan, &mut RawDocumentBuf, &QueryCatalog) -> () =
            |p, writer, query_catalog| {
                write_output_stage(
                    |output, query_catalog| {
                        query_diagnostics::get_projection_type_output(
                            output,
                            "add_fields",
                            query_catalog,
                        )
                    },
                    query_catalog,
                    p,
                    writer,
                )
            };
        HashMap::from([
            ("UNWIND", ("$unwind", None)),
            ("MATCH", ("$match", None)),
            ("LOOKUP_JOIN", ("$lookup", None)),
            ("LOOKUP", ("$lookup", None)),
            ("FACET", ("$facet", None)),
            ("GROUP", ("$group", None)),
            ("COUNT_SCAN", ("$count", None)),
            ("LIMIT", ("$limit", None)),
            ("SAMPLESORT", ("$sample", None)),
            ("SORT", ("$sort", None)),
            ("PROJECT", ("$project", Some(project_function))), //Some(WriteProjectionStage)),
            ("ADDFIELDS", ("$addFields", Some(add_fields_function))), //Some(WriteAddFieldsStage)),
            ("SHARD_MERGE", ("$mergeCursors", None)),
            ("MERGE_CURSORS", ("$group", None)),
            ("WORKER_PARTIAL_AGG", ("$group", None)),
            ("UNIONWITH", ("$unionWith", None)),
            ("DOCUMENTS_AGG", ("$documents", None)),
            ("COLLSTATS_AGG", ("$collStats", None)),
        ])
    });

fn write_output_stage(
    f: fn(&str, &QueryCatalog) -> RawDocumentBuf,
    query_catalog: &QueryCatalog,
    plan: &ExplainPlan,
    writer: &mut RawDocumentBuf,
) {
    if let Some(output) = plan.output.as_ref() {
        for o in output {
            let doc = f(o, query_catalog);
            for (key, val) in doc.into_iter().flatten() {
                writer.append(key, val.to_raw_bson())
            }
        }
    }
}

/// Processing explain is a bit complicated because the payload can come in two forms:
/// A command with explain:true, or an explain command wrapping a sub command
#[async_recursion]
pub async fn process_explain(
    request_context: &mut RequestContext<'_>,
    verbosity: Option<Verbosity>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    // Extract the first command from the request document
    let first_command = {
        let request = request_context.payload;
        request.document().into_iter().next()
    };

    if let Some(result) = first_command {
        let result = result?;

        // Default to QueryPlanner here, as Default tends to be too brief
        let verbosity = verbosity.unwrap_or_else(|| {
            let request = request_context.payload;
            request
                .document()
                .get_str("verbosity")
                .map_or(Verbosity::QueryPlanner, Verbosity::from_str)
        });

        match result.0 {
            "explain" => {
                if let Some(explain_doc) = result.1.as_document() {
                    let new_request = Request::Raw(
                        RequestType::Explain,
                        explain_doc,
                        request_context.payload.extra(),
                    );

                    let mut new_request_context = RequestContext {
                        activity_id: request_context.activity_id,
                        payload: &new_request,
                        info: request_context.info,
                        tracker: request_context.tracker,
                    };

                    // Recursive call with the unwrapped command
                    Box::pin(process_explain(
                        &mut new_request_context,
                        Some(verbosity),
                        connection_context,
                        pg_data_client,
                    ))
                    .await
                } else {
                    Err(DocumentDBError::bad_value(
                        "Explain command was not a document.".to_string(),
                    ))
                }
            }
            "aggregate" => {
                run_explain(
                    request_context,
                    "pipeline",
                    verbosity,
                    connection_context,
                    pg_data_client,
                )
                .await
            }
            "find" => {
                run_explain(
                    request_context,
                    "find",
                    verbosity,
                    connection_context,
                    pg_data_client,
                )
                .await
            }
            "count" => {
                run_explain(
                    request_context,
                    "count",
                    verbosity,
                    connection_context,
                    pg_data_client,
                )
                .await
            }
            "distinct" => {
                run_explain(
                    request_context,
                    "distinct",
                    verbosity,
                    connection_context,
                    pg_data_client,
                )
                .await
            }
            _ => Err(DocumentDBError::bad_value(
                "Unrecognized explain command.".to_string(),
            )),
        }
    } else {
        Err(DocumentDBError::bad_value(
            "No command was provided to explain".to_string(),
        ))
    }
}

#[derive(PartialEq, Clone, Copy, Debug)]
pub enum Verbosity {
    Default,
    QueryPlanner,
    ExecutionStats,
    AllPlansExecution,
    AllShardsQueryPlan,
    AllShardsExecution,
}

impl Verbosity {
    fn from_str(value: &str) -> Self {
        match value {
            "queryPlanner" => Verbosity::QueryPlanner,
            "executionStats" => Verbosity::ExecutionStats,
            "allPlansExecution" => Verbosity::AllPlansExecution,
            "allShardsQueryPlan" => Verbosity::AllShardsQueryPlan,
            "allShardsExecution" => Verbosity::AllShardsExecution,
            _ => Verbosity::Default,
        }
    }
}

async fn run_explain(
    request_context: &mut RequestContext<'_>,
    query_base: &str,
    verbosity: Verbosity,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let request = request_context.payload;
    let request_info = request_context.info;

    let (explain_response, query) = pg_data_client
        .execute_explain(request_context, query_base, verbosity, connection_context)
        .await?;

    let dynamic_config = connection_context.dynamic_configuration();

    match explain_response {
        Some(content) => {
            let explain_content = if dynamic_config.enable_developer_explain().await {
                Some(convert_to_bson(content.clone()))
            } else {
                None
            };

            let (collection_name, subtype) = get_subtype_and_collection_name(request)?;
            let mut explain = transform_explain(
                content,
                request_info.db()?,
                collection_name,
                subtype,
                query_base,
                verbosity,
                connection_context.service_context.query_catalog(),
            )
            .await?;

            explain.append("explainVersion", 2.0);
            explain.append(
                "command",
                format!(
                    "db.runCommand({{explain:{}}})",
                    Document::try_from(request.document())?
                        .to_string()
                        .replace('\"', "'")
                ),
            );
            explain.append("ok", OK_SUCCEEDED);

            if dynamic_config.enable_developer_explain().await {
                explain.append(
                    "internal",
                    developer_explain(
                        &query,
                        explain_content.expect("Set during developer explain"),
                        request.document(),
                        request_info,
                    ),
                )
            }

            Ok(Response::Raw(RawResponse(explain)))
        }
        None => Err(DocumentDBError::internal_error(
            "PG returned no rows in response".to_string(),
        )),
    }
}

fn developer_explain(
    query: &str,
    explain_content: RawBson,
    request: &RawDocument,
    request_info: &RequestInfo,
) -> RawDocumentBuf {
    rawdoc! {
        "sql": {
            "query": query
        },
        "query_parameters":[request_info.db().unwrap_or_default(), request.to_raw_document_buf()],
        "explain": explain_content
    }
}

fn get_subtype_and_collection_name<'a>(request: &'a Request<'_>) -> Result<(&'a str, RequestType)> {
    let (key, first_field) =
        request
            .document()
            .into_iter()
            .next()
            .ok_or(DocumentDBError::bad_value(
                "Explain request was empty".to_string(),
            ))??;
    Ok((
        first_field.as_str().ok_or(DocumentDBError::bad_value(
            "First field of explain document needs to be a string".to_string(),
        ))?,
        RequestType::from_str(key)?,
    ))
}

async fn transform_explain(
    explain_content: serde_json::Value,
    db: &str,
    collection: &str,
    subtype: RequestType,
    query_base: &str,
    verbosity: Verbosity,
    query_catalog: &QueryCatalog,
) -> Result<RawDocumentBuf> {
    let mut plans: Vec<PostgresExplain> = serde_json::from_value(explain_content).map_err(|e| {
        DocumentDBError::internal_error(format!("Failed to parse backend explain plan: {e}"))
    })?;

    let plan = plans.remove(0);

    // Store top level data
    let planning_time = plan.plan.planning_time;
    let execution_time = plan.plan.execution_time;
    let data_size = plan
        .plan
        .distributed_plan
        .as_ref()
        .and_then(|dp| dp.job.total_response_size.clone());

    let plan = decompose_distributed_plan(plan.plan);

    let is_unsharded = is_unsharded(&plan);

    let plan = try_simplify_plan(plan, is_unsharded, query_base, query_catalog);

    let collection_path = format!("{db}.{collection}");
    let mut base_result = if subtype == RequestType::Aggregate {
        aggregate_explain(plan, &collection_path, verbosity, query_catalog)
    } else {
        cursor_explain(plan, &collection_path, false, verbosity, query_catalog)
    };

    if let Some(planning_time) = planning_time {
        base_result.append(
            "explainCommandPlanningTimeMillis",
            truncate_latency(planning_time),
        );
    }
    if let Some(execution_time) = execution_time {
        base_result.append(
            "explainCommandExecTimeMillis",
            truncate_latency(execution_time),
        );
    }
    if let Some(data_size) = data_size {
        base_result.append("dataSize", data_size);
    }

    Ok(base_result)
}

fn decompose_distributed_plan(mut explain_plan: ExplainPlan) -> ExplainPlan {
    if explain_plan.distributed_plan.is_some()
        && explain_plan
            .distributed_plan
            .as_ref()
            .is_some_and(|dplan| dplan.subplans.is_some())
    {
        let mut subplans = explain_plan
            .distributed_plan
            .as_mut()
            .expect("Checked")
            .subplans
            .take()
            .expect("Checked");
        return decompose_plan_with_subplans(explain_plan, &mut subplans);
    }

    if let Some(plans) = explain_plan.inner_plans {
        explain_plan.inner_plans =
            Some(plans.into_iter().map(decompose_distributed_plan).collect());
    }
    explain_plan
}

fn walk_plan<T, F, G>(
    explain_plan: &ExplainPlan,
    plan_func: &F,
    job_func: &G,
    state: T,
    walk_inner: bool,
) -> T
where
    F: Fn(&ExplainPlan, T) -> T,
    G: Fn(&DistributedJob, T) -> T,
{
    let mut state = plan_func(explain_plan, state);
    if let Some(tasks) = explain_plan
        .distributed_plan
        .as_ref()
        .map(|dp| &dp.job.tasks)
    {
        state = job_func(
            &explain_plan
                .distributed_plan
                .as_ref()
                .expect("Just acquired")
                .job,
            state,
        );
        for task in tasks {
            for plans in &task.worker_plans {
                for plan in plans {
                    state = walk_plan(&plan.plan, plan_func, job_func, state, walk_inner)
                }
            }
        }
    }
    if walk_inner {
        if let Some(ref plans) = explain_plan.inner_plans {
            for plan in plans {
                state = walk_plan(plan, plan_func, job_func, state, walk_inner)
            }
        }
    }
    state
}

fn decompose_plan_with_subplans(
    mut explain_plan: ExplainPlan,
    subplans: &mut Vec<DistributedSubPlan>,
) -> ExplainPlan {
    if subplans.is_empty() {
        return explain_plan;
    }

    let (intermediate_read, _, other_tasks) = walk_plan(
        &explain_plan,
        &|plan, (intermediate_read, custom_tasks, other_tasks)| match (
            plan.node_type.as_str(),
            plan.function_name.as_deref(),
        ) {
            ("Custom Scan", _) => (intermediate_read, custom_tasks + 1, other_tasks),
            ("Function Scan", Some("read_intermediate_result")) => {
                (intermediate_read + 1, custom_tasks, other_tasks)
            }
            _ => (intermediate_read, custom_tasks, other_tasks + 1),
        },
        &|_, s| s,
        (0, 0, 0),
        true,
    );

    if intermediate_read == 0 {
        if let Some(dp) = explain_plan.distributed_plan.as_mut() {
            dp.subplans = Some(subplans.clone());
        }
        return explain_plan;
    }

    if other_tasks == 0 && intermediate_read == 1 {
        if subplans[subplans.len() - 1].statements.len() == 1 {
            let mut last_sub_plan = subplans.remove(subplans.len() - 1);
            return decompose_plan_with_subplans(last_sub_plan.statements.remove(0).plan, subplans);
        } else {
            let mut new_plans = Vec::new();
            for plans in subplans {
                for plan in &plans.statements {
                    new_plans.push(plan.plan.clone());
                }
            }
            if new_plans.len() == 1 {
                return decompose_distributed_plan(new_plans.remove(0));
            }

            explain_plan.inner_plans = Some(new_plans);
            explain_plan.distributed_plan = None;
        }
    } else if intermediate_read > 0 {
        if let Some(tasks) = explain_plan
            .distributed_plan
            .as_ref()
            .map(|dp| &dp.job.tasks)
        {
            let inner_plans = explain_plan.inner_plans.get_or_insert(Vec::new());
            for task in tasks {
                for list in &task.worker_plans {
                    for plan in list {
                        inner_plans.push(plan.plan.clone())
                    }
                }
            }
            explain_plan.distributed_plan = None;
        }

        if let Some(inner_plans) = explain_plan.inner_plans {
            explain_plan.inner_plans = Some(
                inner_plans
                    .into_iter()
                    .map(|p| walk_plan_and_replace_intermediate_reads(p, subplans))
                    .collect(),
            )
        }

        if explain_plan
            .custom_plan_provider
            .as_ref()
            .is_some_and(|s| s == "Citus Adaptive")
            && explain_plan
                .inner_plans
                .as_ref()
                .is_some_and(|ip| ip.len() == 1)
        {
            return explain_plan.inner_plans.take().expect("Checked").remove(0);
        }
    }
    explain_plan
}

fn walk_plan_and_replace_intermediate_reads(
    mut plan: ExplainPlan,
    subplans: &mut Vec<DistributedSubPlan>,
) -> ExplainPlan {
    if subplans.is_empty() {
        return plan;
    }

    if plan.node_type == "Function Scan"
        && plan.function_name.as_deref() == Some("read_intermediate_result")
    {
        if subplans[subplans.len() - 1].statements.len() == 1 {
            let mut last_sub_plan = subplans.remove(subplans.len() - 1);
            return decompose_plan_with_subplans(last_sub_plan.statements.remove(0).plan, subplans);
        }
    } else if let Some(inner_plans) = plan.inner_plans {
        plan.inner_plans = Some(
            inner_plans
                .into_iter()
                .map(|p| decompose_plan_with_subplans(p, subplans))
                .collect(),
        )
    }

    plan
}

fn is_unsharded(plan: &ExplainPlan) -> bool {
    let (has_distributed_job, has_shard_filter) = walk_plan(
        plan,
        &|plan, mut state: (bool, bool)| {
            if let Some(relation) = plan.relation_name.as_deref() {
                if plan.filter.as_deref().is_some_and(|f| {
                    query_diagnostics::has_shard_filter_query(f)
                        && !query_diagnostics::is_unsharded_query(relation, f)
                }) || plan.index_condition.as_deref().is_some_and(|ic| {
                    query_diagnostics::has_shard_filter_query(ic)
                        && !query_diagnostics::is_unsharded_query(relation, ic)
                }) {
                    state.1 = true;
                }
            }
            state
        },
        &|job, mut state: (bool, bool)| {
            if job.task_count > 1 {
                state.0 = true;
            }
            state
        },
        (false, false),
        true,
    );
    !has_distributed_job && !has_shard_filter
}

fn remove_nested_add_fields(
    mut plan: ExplainPlan,
    parent_stage_name: &str,
    query_catalog: &QueryCatalog,
) -> ExplainPlan {
    plan.inner_plans = plan.inner_plans.map(|inner_plans| {
        inner_plans
            .into_iter()
            .flat_map(|plan| {
                let (stage_name, _) =
                    get_stage_from_plan(&plan, Some(parent_stage_name), query_catalog);
                if stage_name == "ADDFIELDS" {
                    plan.inner_plans
                        .map_or(vec![].into_iter(), |ps| ps.into_iter())
                } else {
                    vec![plan].into_iter()
                }
            })
            .collect()
    });
    plan
}

fn try_simplify_plan(
    mut plan: ExplainPlan,
    is_unsharded: bool,
    query_base: &str,
    query_catalog: &QueryCatalog,
) -> ExplainPlan {
    if is_unsharded {
        if let Some(job) = plan.distributed_plan.as_mut().map(|dp| &mut dp.job) {
            if job.task_count == 1
                && job.tasks_shown == "All"
                && job.tasks.len() == 1
                && job.tasks[0].worker_plans.len() == 1
                && job.tasks[0].worker_plans[0].len() == 1
            {
                plan = job.tasks.remove(0).worker_plans.remove(0).remove(0).plan;
            }
        }
    }
    if query_base == "count" {
        if is_unsharded && plan.distributed_plan.is_none() {
            return ExplainPlan {
                node_type: "Explain_Count_Scan".to_string(),
                inner_plans: Some(vec![remove_nested_add_fields(plan, "COUNT", query_catalog)]),
                ..Default::default()
            };
        } else if let Some(dp) = plan.distributed_plan.as_mut() {
            if dp.job.task_count == 1
                && dp.job.tasks_shown == "All"
                && dp.job.tasks.len() == 1
                && dp.job.tasks[0].worker_plans.len() == 1
                && dp.job.tasks[0].worker_plans[0].len() == 1
            {
                let sub_plan = dp.job.tasks[0].worker_plans[0].remove(0).plan;
                let new_plan = remove_nested_add_fields(sub_plan, "COUNT", query_catalog);
                let new_plan = ExplainPlan {
                    node_type: "Explain_Count_Scan".to_string(),
                    inner_plans: Some(vec![new_plan]),
                    ..Default::default()
                };
                dp.job.tasks[0].worker_plans[0].insert(0, PostgresExplain { plan: new_plan })
            }
        }
    }
    plan
}

#[derive(Clone, Copy)]
enum AggregationType {
    /// <summary>
    /// A simple aggregation that can be reduced to a "Count"/"Distinct"/"Find" style explain.
    /// </summary>
    SimpleAggregation,

    /// <summary>
    /// An aggregation that requires multiple stages to explain.
    /// </summary>
    StageBasedAggregation,
}

const SIMPLE_AGGREGATION_STAGES: [&str; 9] = [
    "COLLSCAN",
    "IXSCAN",
    "FETCH",
    "OR",
    "PROJECTION",
    "AND",
    "PROJECTION_DEFAULT",
    "EOF",
    "SHARD_MERGE",
];

fn get_projection_plan_from_output(output: &[String]) -> Option<&'static str> {
    for output in output {
        if output.contains("COALESCE(array_agg(") {
            return Some("LOOKUP");
        }
        if output.contains("bson_dollar_lookup_extract_filter_expression") {
            return Some("LOOKUP_EXTRACT");
        }
        if output.contains("bson_dollar_unwind") {
            return Some("UNWIND");
        }
        if output.contains("bson_dollar_project") {
            return Some("PROJECT");
        }
        if output.contains("bson_dollar_add_fields") {
            return Some("ADDFIELDS");
        }
        if output.contains("bson_distinct_unwind") {
            return Some("DISTINCT_UNWIND");
        }
    }
    None
}

fn get_aggregate_plan_from_output<'a>(
    output: &'a str,
    parent_stage: Option<&str>,
    query_catalog: &QueryCatalog,
) -> Option<&'a str> {
    if query_diagnostics::is_output_count(output, query_catalog) {
        Some("COUNT_SCAN")
    } else if output.contains("bson_build_distinct_response")
        || output.contains("bson_distinct_agg")
    {
        Some("DISTINCT_SCAN")
    } else if output.contains("COALESCE(array_agg(") {
        Some("LOOKUP_JOIN")
    } else if output.contains("bson_object_agg") {
        Some("FACET")
    } else if output.contains(query_catalog.find_coalesce()) {
        if parent_stage.is_some_and(|p| p == "LOOKUP") {
            Some("LOOKUP_JOIN")
        } else {
            let facet_names: Vec<&str> = output.split('\'').collect();
            if facet_names.len() > 4 {
                Some(facet_names[facet_names.len() - 4])
            } else {
                None
            }
        }
    } else if output.contains("coord_combine_agg") {
        Some("MERGE_CURSORS")
    } else if output.contains("worker_partial_agg") {
        Some("WORKER_PARTIAL_AGG")
    } else {
        None
    }
}

fn get_stage_from_plan(
    plan: &ExplainPlan,
    parent_stage: Option<&str>,
    query_catalog: &QueryCatalog,
) -> (String, Option<&'static str>) {
    if plan
        .index_name
        .as_ref()
        .is_some_and(|name| name.starts_with("collection_pk") || name == "_id_")
        && plan.index_condition.as_ref().is_some_and(|cond| {
            !cond.as_str().contains("object_id")
                && query_diagnostics::is_unsharded_query(
                    plan.relation_name.as_deref().unwrap_or_default(),
                    cond.as_str(),
                )
        })
    {
        return ("COLLSCAN".to_owned(), None);
    }

    match plan.node_type.as_str() {
        "Index Only Scan" => ("IXSCAN".to_owned(), None),
        "Bitmap Index Scan" | "Index Scan" => {
            if plan
                .index_condition
                .as_ref()
                .is_some_and(|name| name.contains(query_catalog.find_operator()))
            {
                ("TEXT_MATCH".to_owned(), Some("IXSCAN"))
            } else if plan
                .order_by
                .as_deref()
                .is_some_and(|o| o.contains("<|-|>"))
            {
                (
                    if plan
                        .order_by
                        .as_deref()
                        .expect("Checked")
                        .contains("bson_validate_geometry")
                    {
                        "GEO_NEAR_2D"
                    } else {
                        "GEO_NEAR_2DSPHERE"
                    }
                    .to_owned(),
                    Some("IXSCAN"),
                )
            } else if parent_stage.is_some_and(|s| s == "FETCH")
                || plan.node_type.as_str() == "Bitmap Index Scan"
            {
                ("IXSCAN".to_owned(), None)
            } else {
                ("FETCH".to_owned(), Some("IXSCAN"))
            }
        }
        "Bitmap Heap Scan" | "Parallel Bitmap Heap Scan" => {
            if plan
                .filter
                .as_ref()
                .is_some_and(|f| f.contains(query_catalog.find_bson_text_meta_qual()))
            {
                ("PROJECTION_DEFAULT".to_owned(), None)
            } else {
                ("FETCH".to_owned(), None)
            }
        }
        "BitmapOr" => ("OR".to_owned(), None),
        "Result" | "ProjectSet" => {
            if let Some(o) = plan.output.as_ref() {
                if let Some(p) = get_projection_plan_from_output(o) {
                    return (p.to_owned(), None);
                }
            }
            ("PROJECTION_DEFAULT".to_owned(), None)
        }
        "BitmapAnd" => ("AND".to_owned(), None),
        "Sort" => {
            if plan
                .sort_keys
                .as_ref()
                .is_some_and(|keys| keys.len() == 1 && keys[0].contains("(random())"))
            {
                return ("SAMPLESORT".to_owned(), None);
            }
            if let Some(sort_keys) = plan.sort_keys.as_ref() {
                if sort_keys.len() == 1 {
                    if sort_keys[0].contains("bson_expression_get") {
                        return ("GROUPSORT".to_owned(), None);
                    } else if sort_keys[0].contains("bson_validate_geometry") {
                        return ("GEO_NEAR_2D".to_owned(), None);
                    } else if sort_keys[0].contains("bson_validate_geography") {
                        return ("GEO_NEAR_2DSPHERE".to_owned(), None);
                    }
                }
            }
            if plan
                .sort_keys
                .as_ref()
                .is_some_and(|keys| keys.len() == 1 && keys[0].contains("bson_expression_get"))
            {
                return ("GROUPSORT".to_owned(), None);
            }
            ("SORT".to_owned(), None)
        }
        "Sample Scan" => ("SAMPLESCAN".to_owned(), None),
        "Seq Scan" => ("COLLSCAN".to_owned(), None),
        "Unique" => ("UNIQUE".to_owned(), None),
        "Nested Loop" => {
            if let Some(join) = plan.join_type.as_ref() {
                if join == "Left"
                    || (join == "Inner"
                        && plan
                            .output
                            .as_ref()
                            .map(|o| o.len() == 1 && o[0].contains("bson_dollar_merge_documents"))
                            .unwrap_or_default())
                {
                    ("LOOKUP".to_owned(), None)
                } else {
                    (join.to_uppercase() + "_JOIN", None)
                }
            } else {
                ("JOIN".to_owned(), None)
            }
        }
        "CTE Scan" | "Subquery Scan" => {
            if let Some(outputs) = plan.output.as_ref() {
                let project_stage = if !outputs.is_empty() {
                    get_projection_plan_from_output(outputs)
                } else {
                    None
                };

                if plan.filter.is_some() {
                    return ("MATCH".to_owned(), project_stage);
                } else if let Some(stage) = project_stage {
                    return (stage.to_owned(), None);
                }
            }
            ("PROJECTION_DEFAULT".to_owned(), None)
        }
        "Limit" => {
            let p = plan
                .output
                .as_ref()
                .and_then(|o| get_projection_plan_from_output(o));
            ("LIMIT".to_owned(), p)
        }
        "Aggregate" => {
            if let (Some(g), Some(o)) = (plan.group_key.as_ref(), plan.output.as_ref()) {
                let plan_outputs: Vec<&String> = o.iter().filter(|o| !g.contains(o)).collect();
                if plan_outputs.len() == 1 {
                    if let Some(p) =
                        get_aggregate_plan_from_output(plan_outputs[0], parent_stage, query_catalog)
                    {
                        return (p.to_owned(), None);
                    }
                } else {
                    return ("GROUP".to_owned(), None);
                }
            } else if plan.group_key.is_some() {
                return ("GROUP".to_owned(), None);
            } else if let Some(o) = plan.output.as_ref() {
                if o.len() == 1 {
                    if let Some(p) =
                        get_aggregate_plan_from_output(&o[0], parent_stage, query_catalog)
                    {
                        return (p.to_owned(), None);
                    }
                }
            }
            tracing::warn!("Unknown stage aggregate found");
            ("GENERIC_AGGREGATE".to_owned(), None)
        }
        "Gather" => ("Parallel Merge".to_owned(), None),
        "Custom Scan" => {
            if let Some(cpp) = plan.custom_plan_provider.as_ref() {
                match cpp.as_str() {
                    "Citus Adaptive" => {
                        if plan
                            .distributed_plan
                            .as_ref()
                            .is_some_and(|p| p.job.task_count == 1)
                        {
                            ("SINGLE_SHARD".to_owned(), None)
                        } else {
                            ("SHARD_MERGE".to_owned(), None)
                        }
                    }
                    "DocumentDBApiExplainQueryScan" => ("ExplainWrapper".to_owned(), None),
                    scan_type if query_catalog.scan_types().contains(&scan_type.to_string()) => {
                        ("FETCH".to_owned(), None)
                    }
                    _ => {
                        tracing::warn!("Unknown scan: {cpp}");
                        ("".to_owned(), None)
                    }
                }
            } else {
                tracing::warn!("Custom scan without provider.");
                ("".to_owned(), None)
            }
        }
        "Function Scan" => {
            if let Some(function_name) = plan.function_name.as_deref() {
                match function_name {
                    "empty_data_table" => return ("EOF".to_owned(), None),
                    "bson_lookup_unwind" => {
                        // A lookup unwind as the base RTE
                        if (plan.inner_plans.is_none()
                            || plan.inner_plans.as_ref().is_some_and(|p| p.is_empty()))
                            && (plan.parent_relationship.is_none()
                                || plan
                                    .parent_relationship
                                    .as_ref()
                                    .is_some_and(|pr| pr != "Outer"))
                        {
                            return ("DOCUMENTS_AGG".to_owned(), None);
                        }
                    }
                    "coll_stats_aggregation" => {
                        if parent_stage.is_some_and(|x| x == "COUNT") {
                            return ("RECORD_STORE_FAST_COUNT".to_owned(), None);
                        } else {
                            return ("COLLSTATS_AGG".to_owned(), None);
                        }
                    }
                    _ => {}
                }
            };
            tracing::warn!(
                "Unknown function found: {}",
                plan.function_name.as_deref().unwrap_or("None")
            );
            ("FUNCSCAN".to_owned(), None)
        }
        "Explain_Count_Scan" => ("COUNT".to_owned(), None),
        "Append" => {
            // This is an estimate of the behavior - could be incorrect, but
            // as a starting point "good enough".
            if plan.parent_relationship.is_none()
                && plan.inner_plans.as_ref().is_some_and(|ip| {
                    ip.len() == 2
                        && ip.iter().all(|p| {
                            p.parent_relationship
                                .as_deref()
                                .is_some_and(|pr| pr == "Member")
                        })
                })
            {
                ("UNIONWITH".to_owned(), None)
            } else {
                ("UNION".to_owned(), None)
            }
        }
        _ => {
            tracing::warn!("Unknown stage found: {}", plan.node_type);
            ("COLLSCAN".to_owned(), None)
        }
    }
}

fn aggregate_explain(
    mut plan: ExplainPlan,
    collection_path: &str,
    verbosity: Verbosity,
    query_catalog: &QueryCatalog,
) -> RawDocumentBuf {
    let (agg_type, shard_count) = walk_plan(
        &plan,
        &|plan, mut state: (AggregationType, i32)| {
            let (stage, _) = get_stage_from_plan(plan, None, query_catalog);
            if !SIMPLE_AGGREGATION_STAGES.contains(&stage.as_str()) {
                state.0 = AggregationType::StageBasedAggregation;
            }
            state
        },
        &|job, mut state| {
            state.1 = std::cmp::max(state.1, job.task_count);
            state
        },
        (AggregationType::SimpleAggregation, 0),
        true,
    );

    if shard_count == 0 {
        return aggregate_explain_core(plan, agg_type, collection_path, verbosity, query_catalog);
    }

    let result = determine_pipeline_split(&mut plan);
    if let Some((errors, shard_parts, dplan)) = result {
        if !shard_parts.is_empty() {
            let mut shards_explain = rawdoc! {
                "shardCount": dplan.job.task_count,
                "shardInformation": dplan.job.tasks_shown,
                "retrievedDocumentSizeBytes": dplan.job.total_response_size.unwrap_or("".to_string())
            };

            let mut i = 0;
            for shard_plan in shard_parts.into_iter() {
                i += 1;
                shards_explain.append(
                    format!("shard_{i}"),
                    aggregate_explain_core(
                        shard_plan.clone(),
                        agg_type,
                        collection_path,
                        verbosity,
                        query_catalog,
                    ),
                );
            }
            for error in errors {
                i += 1;

                shards_explain.append(format!("shard_{i}"), rawdoc! {"error": error});
            }

            return rawdoc! {
                "splitPipeline": {
                    "mergerPart": aggregate_explain_core(plan, AggregationType::StageBasedAggregation, collection_path, verbosity, query_catalog)
                },
                "shards":  shards_explain
            };
        }
    }
    aggregate_explain_core(plan, agg_type, collection_path, verbosity, query_catalog)
}

fn determine_pipeline_split(
    plan: &mut ExplainPlan,
) -> Option<(Vec<String>, Vec<ExplainPlan>, DistributedQueryPlan)> {
    let mut errors = Vec::new();
    let mut shard_parts = Vec::new();
    if let Some(dplan) = plan.distributed_plan.take() {
        if dplan.job.task_count >= 1 {
            for job in dplan.job.tasks.as_slice() {
                if let Some(error) = job.error.as_deref() {
                    errors.push(error.to_string())
                }

                for plan in job.worker_plans.as_slice() {
                    if !plan.is_empty() {
                        shard_parts.push(plan[0].plan.clone())
                    }
                }
            }
        }
        return Some((errors, shard_parts, dplan));
    } else if let Some(inner_plans) = plan.inner_plans.as_mut() {
        for inner_plan in inner_plans {
            let result = determine_pipeline_split(inner_plan);
            if result.as_ref().is_some_and(|r| !r.1.is_empty()) {
                return result;
            }
        }
    }
    None
}

fn aggregate_explain_core(
    plan: ExplainPlan,
    agg_type: AggregationType,
    collection_path: &str,
    verbosity: Verbosity,
    query_catalog: &QueryCatalog,
) -> RawDocumentBuf {
    match agg_type {
        AggregationType::SimpleAggregation => {
            rawdoc! {
                "stages": [
                    { "$cursor": cursor_explain(plan, collection_path, false, verbosity, query_catalog) }
                ]
            }
        }
        AggregationType::StageBasedAggregation => {
            let mut processed_stages = Vec::new();
            classify_stages(plan, None, &mut processed_stages, query_catalog);
            let bufs: Vec<RawDocumentBuf> = processed_stages
                .into_iter()
                .map(|(documentdb_name, plan, _, f)| {
                    let mut doc = cursor_explain(
                        plan.clone(),
                        collection_path,
                        true,
                        verbosity,
                        query_catalog,
                    );
                    if let Some(f) = f {
                        f(&plan, &mut doc, query_catalog)
                    }
                    rawdoc! { documentdb_name: doc }
                })
                .collect();

            let mut array = RawArrayBuf::new();
            for buf in bufs {
                array.push(buf)
            }
            rawdoc! {"stages": array}
        }
    }
}

type Stage = (
    String,
    ExplainPlan,
    String,
    Option<fn(&ExplainPlan, &mut RawDocumentBuf, &QueryCatalog) -> ()>,
);

fn classify_stages(
    mut plan: ExplainPlan,
    prior_stage: Option<&str>,
    processed_stages: &mut Vec<Stage>,
    query_catalog: &QueryCatalog,
) -> Option<ExplainPlan> {
    let (stage_name, _) = get_stage_from_plan(&plan, prior_stage, query_catalog);

    // base case - if we see a table it becomes terminal - everything below gets put in here.
    if plan.relation_name.is_some() || stage_name == "ExplainWrapper" {
        processed_stages.push(("$cursor".to_owned(), plan, stage_name, None));
        return None;
    }

    // if this is a Lookup JOIN then we automatically treat it as terminal.
    // all nested stages get placed under this one.
    // also let it parent under the root $lookup projection.
    if stage_name == "LOOKUP_JOIN" {
        return Some(plan);
    }

    if stage_name != "facet1"
        && stage_name.len() > "Facet".len()
        && stage_name.to_lowercase().starts_with("facet")
    {
        return Some(plan);
    }

    // Do a DFS of the stages.
    let inner_plans = plan.inner_plans.take();
    plan.inner_plans = inner_plans.map(|inner| {
        inner
            .into_iter()
            .filter_map(|plan| {
                classify_stages(plan, Some(&stage_name), processed_stages, query_catalog)
            })
            .collect()
    });

    if is_aggregation_stage_skippable(&plan, &stage_name, prior_stage) {
        return Some(plan);
    }

    if let Some((stage, func)) = AGGREGATION_STAGE_NAME_MAP.get(stage_name.as_str()) {
        processed_stages.push((stage.to_string(), plan, stage_name, *func));
        return None;
    }

    if prior_stage.is_none() {
        tracing::warn!("Found residual unparented aggregation stage {stage_name}");
        processed_stages.push(("$root".to_owned(), plan, stage_name, None));
        return None;
    }

    tracing::warn!("Unknown aggregation stage {stage_name}");
    Some(plan)
}

fn is_aggregation_stage_skippable(
    plan: &ExplainPlan,
    stage_name: &str,
    prior_stage: Option<&str>,
) -> bool {
    // let 2 merge cursors merge.
    if prior_stage.is_some_and(|p| p == stage_name) && stage_name == "MERGE_CURSORS" {
        return true;
    }

    if stage_name == "PROJECTION_DEFAULT" || stage_name == "FETCH" {
        // if there's valid filters, inner plans, or outputs - it's useful.
        if plan.filter.is_some()
            || plan.inner_plans.as_ref().is_some_and(|ip| !ip.is_empty())
            || plan.output.as_ref().is_some_and(|o| o.len() > 1)
        {
            return false;
        }

        // no filters, no output, no inner plan - ignorable.
        if plan.output.as_ref().is_none_or(|o| o.is_empty()) {
            return true;
        }

        if let Some(o) = plan.output.as_ref() {
            if o.len() == 1
                && plan
                    .alias
                    .as_ref()
                    .is_some_and(|a| o[0] == format!("{a}.document"))
            {
                return true;
            }
        }
    }

    false
}

fn get_total_examined(plan: &ExplainPlan) -> (i64, i64) {
    let (mut total_rows_examined, mut total_keys_examined, _) = walk_plan(
        plan,
        &|p, (total_rows_examined, total_keys_examined, root)| {
            if !std::ptr::eq(p, root) {
                (
                    total_rows_examined
                        + p.actual_rows.unwrap_or(0)
                        + p.rows_removed_by_filter.unwrap_or(0),
                    total_keys_examined
                        + p.actual_rows.unwrap_or(0)
                        + p.rows_removed_by_index.unwrap_or(0),
                    root,
                )
            } else {
                (total_rows_examined, total_keys_examined, root)
            }
        },
        &|_, state| state,
        (0, 0, plan),
        false,
    );

    if total_rows_examined == 0 {
        total_rows_examined =
            plan.actual_rows.unwrap_or(0) + plan.rows_removed_by_filter.unwrap_or(0);
    }
    if total_keys_examined == 0 {
        total_keys_examined =
            plan.actual_rows.unwrap_or(0) + plan.rows_removed_by_index.unwrap_or(0);
    }
    (total_rows_examined, total_keys_examined)
}

fn query_planner(
    plan: ExplainPlan,
    collection_path: &str,
    is_aggregation_stage: bool,
    query_catalog: &QueryCatalog,
) -> RawDocumentBuf {
    let mut writer = RawDocumentBuf::new();
    if plan.distributed_plan.is_none() && !is_aggregation_stage {
        writer.append("namespace", collection_path)
    }
    let result = walk_plan_stage(
        plan,
        None,
        query_catalog,
        |plan, stage_name, query_catalog| {
            let mut doc = rawdoc! {
                "stage": stage_name,
            };

            if stage_name != "FETCH" {
                if let Some(index_name) = plan.index_name.as_deref() {
                    doc.append("indexName", index_name);
                }

                if let Some(direction) = plan.scan_direction.as_deref() {
                    doc.append("direction", direction);
                }

                if plan.node_type.contains("bitmap") {
                    doc.append("isBitmap", true);
                }

                if plan.node_type == "Index Only Scan" {
                    doc.append("isIndexOnlyScan", true);
                }

                if plan.index_details.is_some() {
                    let mut arr = RawArrayBuf::new();
                    for detail in plan.index_details.as_ref().unwrap() {
                        if detail.index_name.as_deref() != plan.index_name.as_deref() {
                            continue;
                        }

                        let mut index_doc = rawdoc! {};
                        if let Some(index_name) = detail.index_name.as_deref() {
                            index_doc.append("indexName", index_name);
                        }

                        if let Some(multi_key_val) = detail.is_multi_key {
                            index_doc.append("isMultiKey", multi_key_val);
                        }

                        if let Some(index_bounds) = detail.index_bounds.as_ref() {
                            let mut bounds_arr = RawArrayBuf::new();
                            index_bounds.iter().for_each(|key| {
                                bounds_arr.push(key.as_str());
                            });
                            index_doc.append("bounds", bounds_arr);
                        }

                        arr.push(index_doc);
                    }

                    doc.append("indexUsage", arr);
                }
            }

            if let Some(page_size) = plan.page_size {
                doc.append("page_size", smallest_from_i64(page_size));
            }

            if let Some(startup_cost) = plan.startup_cost {
                doc.append("startupCost", startup_cost);
            }

            if let Some(total_cost) = plan.total_cost {
                doc.append("totalCost", total_cost);
            }

            if let Some(sort_keys) = plan.sort_keys.as_ref() {
                let mut sort_keys_arr = RawArrayBuf::new();
                for order_string in sort_keys {
                    if let Some(order_value) =
                        query_diagnostics::get_sort_conditions(order_string, query_catalog)
                    {
                        sort_keys_arr.push(order_value);
                    }
                }
                if !sort_keys_arr.is_empty() {
                    doc.append("sortKey", sort_keys_arr);
                }

                doc.append("sortKeysCount", sort_keys.len() as i32);
            }
            if let Some(presorted_keys) = plan.presorted_key.as_ref() {
                doc.append("presortedKeysCount", presorted_keys.len() as i32);
            }

            if stage_name != "FETCH" && plan.node_type == "Index Scan" && plan.order_by.is_some() {
                doc.append("hasOrderBy", true);
            }

            if let Some(vector_search_params) = plan.vector_search_custom_params.as_deref() {
                let params: std::result::Result<VectorSearchParams, serde_json::Error> =
                    serde_json::from_str(vector_search_params);
                if let Ok(params) = params {
                    let mut vector_search = RawDocumentBuf::new();
                    if let Some(nprobes) = params.n_probes {
                        vector_search.append("nProbes", smallest_from_f64(nprobes))
                    }
                    if let Some(ef_search) = params.ef_search {
                        vector_search.append("efSearch", smallest_from_f64(ef_search))
                    }
                    if let Some(l_search) = params.l_search {
                        vector_search.append("lSearch", smallest_from_f64(l_search))
                    }
                    doc.append("cosmosSearchCustomParams", vector_search)
                } else {
                    tracing::error!("Failed to parse vector search params: {vector_search_params}")
                }
            }

            if let Some(filter) = plan.filter.as_deref() {
                let values = query_diagnostics::get_runtime_conditions(filter, query_catalog);
                if !values.is_empty() {
                    doc.append("runtimeFilterSet", limited_array_from_contents(values));
                }
            }

            if plan.index_condition.is_some() && stage_name != "FETCH" {
                let conditions = query_diagnostics::get_index_conditions(
                    plan.index_condition.as_deref().expect("Checked"),
                    query_catalog,
                );
                if !conditions.is_empty() {
                    doc.append("indexFilterSet", limited_array_from_contents(conditions));
                }
            }

            if stage_name != "EOF" {
                let rows: i64 = plan
                    .plan_rows
                    .as_ref()
                    .map(|n| n.as_i64().unwrap_or(i64::MAX))
                    .unwrap_or(0);
                doc.append("estimatedTotalKeysExamined", smallest_from_i64(rows))
            }
            doc
        },
    );
    writer.append("winningPlan", result);
    writer
}

fn limited_array_from_contents(contents: Vec<(&'static str, RawDocumentBuf)>) -> RawArrayBuf {
    let mut accumulated_length = 0;
    let mut arr = RawArrayBuf::new();
    for (expr, value) in contents {
        let expr_value = if accumulated_length > MAX_EXPLAIN_BSON_COMMAND_LENGTH {
            RawBson::from("...")
        } else if value.as_bytes().len() > MAX_EXPLAIN_BSON_COMMAND_LENGTH {
            accumulated_length += MAX_EXPLAIN_BSON_COMMAND_LENGTH;
            RawBson::from(format!(
                "{}...",
                &value.to_document().unwrap_or_default().to_string()
                    [0..MAX_EXPLAIN_BSON_COMMAND_LENGTH]
            ))
        } else {
            accumulated_length += value.as_bytes().len();
            RawBson::from(value)
        };

        let doc = rawdoc! {
            expr: expr_value
        };
        arr.push(doc);
    }
    arr
}

fn execution_stats(plan: ExplainPlan, query_catalog: &QueryCatalog) -> RawDocumentBuf {
    let (total_rows_examined, total_keys_examined) = get_total_examined(&plan);
    let execution_time = plan.actual_total_time.unwrap_or(0.0) as i64;
    let returned = plan.actual_rows.unwrap_or(0);
    let stages = walk_plan_stage(plan, None, query_catalog, |plan, stage_name, _| {
        let mut doc = rawdoc! {
            "stage": stage_name,
            "nReturned": plan.actual_rows.unwrap_or(0),
            "executionTimeMillis": plan.actual_total_time.unwrap_or(0.0) as i64,
            "totalKeysExamined": plan.actual_rows.unwrap_or(0) + plan.rows_removed_by_index.unwrap_or(0),
        };
        if plan.index_name.is_none()
            || plan.filter.is_some()
            || plan.rows_removed_by_filter.unwrap_or(0) > 0
        {
            doc.append(
                "totalDocsExamined",
                smallest_from_i64(
                    plan.actual_rows.unwrap_or(0) + plan.rows_removed_by_filter.unwrap_or(0),
                ),
            )
        }

        if stage_name != "FETCH" {
            if let Some(index_name) = plan.index_name.as_deref() {
                doc.append("indexName", index_name)
            }

            if let Some(heap_fetches) = plan.heap_fetches {
                doc.append("totalDocsAnalyzed", smallest_from_i64(heap_fetches));
            }

            if plan.index_details.is_some() {
                let mut arr = RawArrayBuf::new();
                for detail in plan.index_details.as_ref().unwrap() {
                    let mut index_doc = rawdoc! {};
                    if let Some(index_name) = detail.index_name.as_deref() {
                        index_doc.append("indexName", index_name);
                    }

                    if let Some(inner_scan_loops) = detail.inner_scan_loops {
                        index_doc.append("scanLoops", smallest_from_i64(inner_scan_loops));
                    }

                    if let Some(scan_type) = detail.scan_type.as_deref() {
                        index_doc.append("scanType", scan_type);
                    }

                    if let Some(num_duplicates) = detail.num_duplicates {
                        if num_duplicates > 0 {
                            index_doc.append("numDuplicates", smallest_from_i64(num_duplicates));
                        }
                    }

                    if let Some(scan_key_details) = detail.scan_key_details.as_ref() {
                        let mut scan_key_arr = RawArrayBuf::new();
                        scan_key_details.iter().for_each(|key| {
                            scan_key_arr.push(key.as_str());
                        });
                        index_doc.append("scanKeys", scan_key_arr);
                    }

                    arr.push(index_doc);
                }

                doc.append("indexUsage", arr);
            }
        }

        if stage_name == "TEXT_MATCH" {
            doc.append("textIndexVersion", 3)
        }
        if plan.sort_space_type.as_deref().is_some_and(|s| s == "Disk") {
            doc.append("usedDisk", true)
        }
        if let Some(method) = plan.sort_method.as_deref() {
            doc.append("sortMethod", method)
        }
        if let Some(blocks) = plan.exact_heap_blocks {
            doc.append("exactBlocksRead", smallest_from_i64(blocks))
        }
        if let Some(blocks) = plan.lossy_heap_blocks {
            doc.append("lossyBlocksRead", smallest_from_i64(blocks))
        }
        if let Some(space_used) = plan.sort_space_used {
            doc.append(
                "totalDataSizeSortedBytesEstimate",
                smallest_from_i64(space_used),
            )
        }
        if let Some(v) = plan.rows_removed_by_filter {
            doc.append("totalDocsRemovedByRuntimeFilter", smallest_from_i64(v))
        }
        if let Some(v) = plan.rows_removed_by_index {
            doc.append("totalDocsRemovedByIndexRechecks", smallest_from_i64(v))
        }
        if let Some(v) = plan.shared_hit_blocks {
            doc.append("numBlocksFromCache", smallest_from_i64(v))
        }
        if let Some(v) = plan.shared_read_blocks {
            doc.append("numBlocksFromDisk", smallest_from_i64(v))
        }
        if let Some(v) = plan.io_read_time {
            doc.append("ioReadTimeMillis", smallest_from_i64(v))
        }
        if let Some(v) = plan.workers_launched {
            doc.append("parallelWorkers", smallest_from_i64(v))
        }

        doc
    });
    rawdoc! {
        "nReturned": returned,
        "executionTimeMillis": execution_time,
        "totalDocsExamined": total_rows_examined,
        "totalKeysExamined": total_keys_examined,
        "executionStages": stages
    }
}

fn distribute_index_details(plan: &mut ExplainPlan, index_details: Option<Vec<IndexDetails>>) {
    plan.index_details = index_details;

    if let Some(inner_plans) = plan.inner_plans.as_mut() {
        for inner_plan in inner_plans {
            distribute_index_details(inner_plan, plan.index_details.clone());
        }
    }
}

fn skip_stage(plan: ExplainPlan, query_catalog: &QueryCatalog) -> ExplainPlan {
    if plan.node_type == "Subquery Scan"
        && plan.output.as_ref().is_some_and(|o| {
            o.len() == 1
                && (o[0].starts_with("bson_repath_and_build")
                    || o[0].starts_with(query_catalog.find_bson_repath_and_build()))
        })
        && plan.inner_plans.as_ref().is_some_and(|ip| ip.len() == 1)
    {
        let mut p = plan.inner_plans.expect("Checked").remove(0);
        p.alias = plan.alias;
        p
    } else if plan.node_type == "Custom Scan"
        && plan.custom_plan_provider.as_deref() == Some("DocumentDBApiExplainQueryScan")
        && plan.inner_plans.as_ref().is_some_and(|ip| ip.len() == 1)
    {
        let mut new_plan = plan.inner_plans.expect("Checked").remove(0);
        new_plan.output = plan.output;
        distribute_index_details(&mut new_plan, plan.index_details.clone());
        new_plan
    } else {
        plan
    }
}

fn walk_plan_stage(
    plan: ExplainPlan,
    parent_stage: Option<&str>,
    query_catalog: &QueryCatalog,
    f: fn(&ExplainPlan, &str, &QueryCatalog) -> RawDocumentBuf,
) -> RawDocumentBuf {
    let plan = skip_stage(plan, query_catalog);

    let (stage, inner_stage) = get_stage_from_plan(&plan, parent_stage, query_catalog);
    let mut res = f(&plan, &stage, query_catalog);

    if let Some(inner) = inner_stage {
        let mut inner_res = f(&plan, inner, query_catalog);
        walk_plan_stage_core(plan.clone(), Some(&stage), query_catalog, &mut inner_res, f);
        res.append("inputStage", inner_res);
        res
    } else {
        walk_plan_stage_core(plan.clone(), Some(&stage), query_catalog, &mut res, f);
        res
    }
}

fn walk_plan_stage_core(
    plan: ExplainPlan,
    parent_stage: Option<&str>,
    query_catalog: &QueryCatalog,
    writer: &mut RawDocumentBuf,
    f: fn(&ExplainPlan, &str, &QueryCatalog) -> RawDocumentBuf,
) {
    if let Some(job) = plan.distributed_plan.map(|dp| dp.job) {
        let plan_bufs = job.tasks.into_iter().enumerate().flat_map(|(i, task)| {
            let iter: Box<dyn Iterator<Item = RawDocumentBuf>> = if let Some(err) = task.error {
                Box::new(std::iter::once(rawdoc! {
                    "error": err
                }))
            } else {
                let iter = task.worker_plans.into_iter().filter_map(|mut plans| {
                    if plans.is_empty() {
                        None
                    } else {
                        Some(rawdoc! {
                            "shard": format!("shard_{}", i),
                            "winningPlan": walk_plan_stage(plans.remove(0).plan, parent_stage, query_catalog, f)
                        })
                    }
                }).collect::<Vec<RawDocumentBuf>>().into_iter();
                Box::new(iter)
            };
            iter
        });
        writer.append("shardCount", job.task_count);
        writer.append("shardInformation", job.tasks_shown);
        writer.append("shards", RawArrayBuf::from_iter(plan_bufs));
        if let Some(bytes) = job.total_response_size {
            writer.append("retrievedDocumentSizeBytes", bytes);
        }
    }

    if let Some(mut inner_plans) = plan.inner_plans {
        match inner_plans.len().cmp(&1) {
            Ordering::Equal => {
                let recurse =
                    walk_plan_stage(inner_plans.remove(0), parent_stage, query_catalog, f);
                writer.append("inputStage", recurse);
            }
            Ordering::Greater => {
                let docs = RawArrayBuf::from_iter(
                    inner_plans
                        .into_iter()
                        .map(|p| walk_plan_stage(p, parent_stage, query_catalog, f)),
                );
                writer.append("inputStages", docs);
            }
            _ => {}
        }
    }
}

fn cursor_explain(
    plan: ExplainPlan,
    collection_path: &str,
    is_aggregation_stage: bool,
    verbosity: Verbosity,
    query_catalog: &QueryCatalog,
) -> RawDocumentBuf {
    let mut doc = rawdoc! {
        "queryPlanner": query_planner(plan.clone(), collection_path, is_aggregation_stage, query_catalog),
    };
    if matches!(
        verbosity,
        Verbosity::ExecutionStats | Verbosity::AllPlansExecution | Verbosity::AllShardsExecution
    ) {
        doc.append("executionStats", execution_stats(plan, query_catalog))
    }
    doc
}

fn truncate_latency(latency: f64) -> f64 {
    (latency * 1000.0).round() / 1000.0
}

fn convert_to_bson(val: serde_json::Value) -> RawBson {
    match val {
        Value::Number(n) => {
            if let Some(n) = n.as_i64() {
                RawBson::Int64(n)
            } else if let Some(n) = n.as_f64() {
                RawBson::Double(n)
            } else {
                RawBson::Double(f64::NAN)
            }
        }
        Value::Object(map) => {
            let mut doc = RawDocumentBuf::new();
            for (k, v) in map {
                doc.append(k, convert_to_bson(v))
            }
            RawBson::Document(doc)
        }
        Value::Array(arr) => {
            let mut bson_array = RawArrayBuf::new();
            for v in arr {
                bson_array.push(convert_to_bson(v));
            }
            RawBson::Array(bson_array)
        }
        Value::Null => RawBson::Null,
        Value::Bool(b) => RawBson::Boolean(b),
        Value::String(s) => RawBson::String(s),
    }
}

fn smallest_from_f64(value: f64) -> RawBson {
    if value % 1.0 == 0.0 {
        smallest_from_i64(value as i64)
    } else {
        RawBson::Double(value)
    }
}

fn smallest_from_i64(value: i64) -> RawBson {
    if let Ok(v) = i32::try_from(value) {
        RawBson::Int32(v)
    } else {
        RawBson::Int64(value)
    }
}

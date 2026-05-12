/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/explain/query_diagnostics.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::RawDocumentBuf;
use once_cell::sync::Lazy;
use regex::{CaptureMatches, Regex};

use crate::QueryCatalog;

static SHARD_KEY_VALUE_EXTRACT: Lazy<Regex> = Lazy::new(|| {
    Regex::new("shard_key_value (OPERATOR\\(pg_catalog.=\\)|=) '(\\d+)'::bigint")
        .expect("Static input")
});
static BSON_SUM_OF_ONE_OUTPUT_REGEX: Lazy<Regex> = Lazy::new(|| {
    Regex::new("^bsonsum\\(bson_expression_get\\(.+, 'BSONHEX13000000012473756d00000000000000f03f00'::bson, true\\)\\)?$").expect("Static input")
});

pub fn get_projection_type_output(
    output: &str,
    function_name: &str,
    query_catalog: &QueryCatalog,
) -> RawDocumentBuf {
    if let Some(result) = Regex::new(query_catalog.bson_dollar_project_output_regex())
        .expect("static input")
        .captures(output)
    {
        if result.len() == 4 && result.get(2).is_some_and(|x| x.as_str() == function_name) {
            if let Some(m) = result.get(3) {
                if let Ok(bytes) = hex::decode(m.as_str()) {
                    if let Ok(doc) = RawDocumentBuf::from_bytes(bytes) {
                        return doc;
                    }
                }
                tracing::warn!("Failed to parse hex string from explain");
            }
        }
    }
    RawDocumentBuf::new()
}

fn is_single_sharded_query(query: &str) -> bool {
    query.contains("shard_key_value OPERATOR(pg_catalog.=)") || query.contains("shard_key_value =")
}

pub fn is_unsharded_query(relation_name: &str, filter: &str) -> bool {
    if !is_single_sharded_query(filter) {
        return false;
    }
    if let Some(result) = SHARD_KEY_VALUE_EXTRACT.captures(filter) {
        let shard_key_value = result.get(2);
        if let Some(shard_key_value) = shard_key_value {
            return relation_name
                .starts_with(&("documents_".to_string() + shard_key_value.as_str()));
        } else {
            return false;
        }
    }

    false
}

pub fn has_shard_filter_query(filter: &str) -> bool {
    filter.contains("shard_key_value OPERATOR(pg_catalog.=)")
        || filter.contains("shard_key_value =")
}

pub fn is_output_count(output: &str, query_catalog: &QueryCatalog) -> bool {
    let output = output.replace(query_catalog.api_catalog_name_regex(), "");
    if output.contains("bson_repath_and_build")
        && output.contains("bsonsum('BSONHEX0b00000010000100000000'::bson)")
    {
        return true;
    }

    if output.contains(query_catalog.output_bson_count_aggregate())
        || output.contains(query_catalog.output_bson_command_count_aggregate())
        || output.contains(query_catalog.output_count_regex())
    {
        return true;
    }

    BSON_SUM_OF_ONE_OUTPUT_REGEX.is_match(&output)
}

fn get_expressions(
    captures: CaptureMatches,
    query_catalog: &QueryCatalog,
) -> Vec<(&'static str, RawDocumentBuf)> {
    let mut expressions = vec![];
    for captures in captures {
        if let Some(capture) = captures.name("expr") {
            if let Some(index_condition) = Regex::new(query_catalog.single_index_condition_regex())
                .expect("static input")
                .captures(capture.as_str())
            {
                let operator =
                    get_operator(index_condition.name("operator").map_or("", |m| m.as_str()));
                let query_bson = index_condition.name("queryBson").map_or("", |m| m.as_str());
                let query = hex::decode(query_bson).unwrap_or(vec![]);
                let query_bson = RawDocumentBuf::from_bytes(query).unwrap_or_default();
                expressions.push((operator, query_bson))
            }
        }
    }
    expressions
}

pub fn get_runtime_conditions(
    input: &str,
    query_catalog: &QueryCatalog,
) -> Vec<(&'static str, RawDocumentBuf)> {
    get_expressions(
        Regex::new(query_catalog.runtime_condition_split_regex())
            .expect("static input")
            .captures_iter(input),
        query_catalog,
    )
}

pub fn get_index_conditions(
    input: &str,
    query_catalog: &QueryCatalog,
) -> Vec<(&'static str, RawDocumentBuf)> {
    get_expressions(
        Regex::new(query_catalog.index_condition_split_regex())
            .expect("static input")
            .captures_iter(input),
        query_catalog,
    )
}

pub fn get_sort_conditions(input: &str, query_catalog: &QueryCatalog) -> Option<RawDocumentBuf> {
    Regex::new(query_catalog.single_index_condition_regex())
        .expect("static input")
        .captures(input)
        .and_then(|capture| capture.get(3))
        .and_then(|opt| {
            hex::decode(opt.as_str())
                .ok()
                .and_then(|f| RawDocumentBuf::from_bytes(f).ok())
        })
}

fn get_operator(input: &str) -> &'static str {
    match input {
        "@=" => "$eq",
        "@>" => "$gt",
        "@>=" => "$gte",
        "@<" => "$lt",
        "@<=" => "$lte",
        "@*=" => "$in",
        "@!=" => "$ne",
        "@!*=" => "$nin",
        "@~" => "$regex",
        "@?" => "$exists",
        "@@#" => "$size",
        "@#" => "$type",
        "@&=" => "$all",
        "@!&" => "$bitsAllClear",
        "@!|" => "$bitsAnyClear",
        "@#?" => "$elemMatch",
        "@&" => "$bitsAllSet",
        "@|" => "$bitsAnySet",
        "@%" => "$mod",
        "@<>" => "$range",
        "@#%" => "$text",
        "@|-|" => "$geoWithin",
        "@|#|" => "$geoIntersects",
        "@|><|" => "$_minMaxDistance", // This is only for explain to show either $minDistance or $maxDistance was used
        _ => "$unknown",
    }
}

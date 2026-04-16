use std::{ffi::CStr, str::FromStr};

use pgrx::{
    ereport,
    ffi::c_char,
    is_a,
    pg_sys::{
        addRangeTableEntryForRelation, defGetInt32, defGetInt64, defGetString, get_namespace_name,
        get_rel_namespace, makeDefElem, makeString, make_parsestate, quote_qualified_identifier,
        AccessShareLock, AsPgCStr, CopyStmt, CreateTemplateTupleDesc, DefElem, List, NoLock, Node,
        NodeTag::T_CopyStmt, Oid, ParseNamespaceItem, ParseState, PlannedStmt, QueryEnvironment,
        RangeVar, RangeVarGetRelidExtended, RowExclusiveLock, TupleDescInitEntry,
    },
    PgBox, PgList, PgLogLevel, PgRelation, PgSqlErrorCode, PgTupleDesc,
};
use url::Url;

use crate::{
    arrow_parquet::{
        compression::{all_supported_compressions, PgParquetCompression},
        field_ids,
        match_by::MatchBy,
        parquet_writer::{DEFAULT_ROW_GROUP_SIZE, DEFAULT_ROW_GROUP_SIZE_BYTES},
        uri_utils::ParsedUriInfo,
    },
    pgrx_utils::extension_exists,
};

use self::field_ids::FieldIds;

use super::{
    copy_to_split_dest_receiver::INVALID_FILE_SIZE_BYTES, hook::ENABLE_PARQUET_COPY_HOOK,
    pg_compat::strVal,
};

pub(crate) fn validate_copy_to_options(p_stmt: &PgBox<PlannedStmt>, uri_info: &ParsedUriInfo) {
    validate_copy_option_names(
        p_stmt,
        &[
            "format",
            "file_size_bytes",
            "field_ids",
            "row_group_size",
            "row_group_size_bytes",
            "compression",
            "compression_level",
            "freeze",
        ],
    );

    let format_option = copy_stmt_get_option(p_stmt, "format");

    if !format_option.is_null() {
        let format = unsafe { defGetString(format_option.as_ptr()) };

        let format = unsafe {
            CStr::from_ptr(format)
                .to_str()
                .expect("format option is not a valid CString")
        };

        if format != "parquet" {
            panic!(
                "{} is not a valid format. Only parquet format is supported.",
                format
            );
        }
    }

    let file_size_bytes_option = copy_stmt_get_option(p_stmt, "file_size_bytes");

    if !file_size_bytes_option.is_null() {
        let file_size_bytes = unsafe { defGetString(file_size_bytes_option.as_ptr()) };

        let file_size_bytes = unsafe {
            CStr::from_ptr(file_size_bytes)
                .to_str()
                .expect("file_size_bytes option is not a valid CString")
        };

        parse_file_size(file_size_bytes)
            .unwrap_or_else(|e| panic!("file_size_bytes option is not valid: {}", e));
    }

    let field_ids_option = copy_stmt_get_option(p_stmt, "field_ids");

    if !field_ids_option.is_null() {
        let field_ids = unsafe { defGetString(field_ids_option.as_ptr()) };

        let field_ids = unsafe {
            CStr::from_ptr(field_ids)
                .to_str()
                .expect("field_ids option is not a valid CString")
        };

        if let Err(e) = FieldIds::from_str(field_ids) {
            ereport!(
                pgrx::PgLogLevel::ERROR,
                pgrx::PgSqlErrorCode::ERRCODE_INVALID_JSON_TEXT,
                e,
                "Allowed options are: none, auto, or a JSON object with field names as keys and field ids as values.",
            );
        }
    }

    let row_group_size_option = copy_stmt_get_option(p_stmt, "row_group_size");

    if !row_group_size_option.is_null() {
        let row_group_size = unsafe { defGetInt64(row_group_size_option.as_ptr()) };

        if row_group_size <= 0 {
            panic!("row_group_size must be greater than 0");
        }
    }

    let row_group_size_bytes_option = copy_stmt_get_option(p_stmt, "row_group_size_bytes");

    if !row_group_size_bytes_option.is_null() {
        let row_group_size_bytes = unsafe { defGetInt64(row_group_size_bytes_option.as_ptr()) };

        if row_group_size_bytes <= 0 {
            panic!("row_group_size_bytes must be greater than 0");
        }
    }

    let compression_option = copy_stmt_get_option(p_stmt, "compression");

    if !compression_option.is_null() {
        let compression = unsafe { defGetString(compression_option.as_ptr()) };

        let compression = unsafe {
            CStr::from_ptr(compression)
                .to_str()
                .expect("compression option is not a valid CString")
        };

        if PgParquetCompression::from_str(compression).is_err() {
            panic!(
                "{} is not a valid compression format. Supported compression formats are {}",
                compression,
                all_supported_compressions()
                    .into_iter()
                    .map(|c| c.to_string())
                    .collect::<Vec<_>>()
                    .join(", ")
            );
        }
    }

    let compression_level_option = copy_stmt_get_option(p_stmt, "compression_level");

    if !compression_level_option.is_null() {
        let compression_level = unsafe { defGetInt32(compression_level_option.as_ptr()) };

        let compression = copy_to_stmt_compression(p_stmt, uri_info);

        compression.ensure_compression_level(compression_level);
    }
}

pub(crate) fn validate_copy_from_options(p_stmt: &PgBox<PlannedStmt>) {
    validate_copy_option_names(p_stmt, &["format", "match_by", "freeze"]);

    let format_option = copy_stmt_get_option(p_stmt, "format");

    if !format_option.is_null() {
        let format = unsafe { defGetString(format_option.as_ptr()) };

        let format = unsafe {
            CStr::from_ptr(format)
                .to_str()
                .expect("format option is not a valid CString")
        };

        if format != "parquet" {
            panic!(
                "{} is not a valid format. Only parquet format is supported.",
                format
            );
        }
    }
}

fn validate_copy_option_names(p_stmt: &PgBox<PlannedStmt>, allowed_options: &[&str]) {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    let copy_options = unsafe { PgList::<DefElem>::from_pg(copy_stmt.options) };

    let copy_from_str = if copy_stmt.is_from { "from" } else { "to" };

    for option in copy_options.iter_ptr() {
        let option = unsafe { PgBox::<DefElem>::from_pg(option) };

        let option_name = unsafe {
            CStr::from_ptr(option.defname)
                .to_str()
                .expect("option name is not a valid CString")
        };

        if !allowed_options.contains(&option_name) {
            panic!(
                "{} is not a valid option for \"copy {} parquet\". Supported options are {}",
                option_name,
                copy_from_str,
                allowed_options.join(", ")
            );
        }
    }
}

pub(crate) fn copy_stmt_uri(p_stmt: &PgBox<PlannedStmt>) -> Result<ParsedUriInfo, String> {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    if copy_stmt.is_program {
        return Err("program is not supported".to_string());
    }

    if copy_stmt.filename.is_null() {
        return Ok(ParsedUriInfo::for_std_inout());
    }

    let uri = unsafe {
        CStr::from_ptr(copy_stmt.filename)
            .to_str()
            .expect("uri option is not a valid CString")
    };

    ParsedUriInfo::try_from(uri)
}

pub(crate) fn copy_to_stmt_file_size_bytes(p_stmt: &PgBox<PlannedStmt>) -> i64 {
    let file_size_bytes_option = copy_stmt_get_option(p_stmt, "file_size_bytes");

    if file_size_bytes_option.is_null() {
        INVALID_FILE_SIZE_BYTES
    } else {
        let file_size_bytes = unsafe { defGetString(file_size_bytes_option.as_ptr()) };

        let file_size_bytes = unsafe {
            CStr::from_ptr(file_size_bytes)
                .to_str()
                .expect("file_size_bytes option is not a valid CString")
        };

        parse_file_size(file_size_bytes)
            .unwrap_or_else(|e| panic!("file_size_bytes option is not valid: {}", e)) as i64
    }
}

pub(crate) fn copy_to_stmt_field_ids(p_stmt: &PgBox<PlannedStmt>) -> *const c_char {
    let field_ids_option = copy_stmt_get_option(p_stmt, "field_ids");

    if field_ids_option.is_null() {
        FieldIds::default().to_string().as_pg_cstr()
    } else {
        unsafe { defGetString(field_ids_option.as_ptr()) }
    }
}

pub(crate) fn copy_to_stmt_row_group_size(p_stmt: &PgBox<PlannedStmt>) -> i64 {
    let row_group_size_option = copy_stmt_get_option(p_stmt, "row_group_size");

    if row_group_size_option.is_null() {
        DEFAULT_ROW_GROUP_SIZE
    } else {
        unsafe { defGetInt64(row_group_size_option.as_ptr()) }
    }
}

pub(crate) fn copy_to_stmt_row_group_size_bytes(p_stmt: &PgBox<PlannedStmt>) -> i64 {
    let row_group_size_bytes_option = copy_stmt_get_option(p_stmt, "row_group_size_bytes");

    if row_group_size_bytes_option.is_null() {
        DEFAULT_ROW_GROUP_SIZE_BYTES
    } else {
        unsafe { defGetInt64(row_group_size_bytes_option.as_ptr()) }
    }
}

pub(crate) fn copy_to_stmt_compression(
    p_stmt: &PgBox<PlannedStmt>,
    uri_info: &ParsedUriInfo,
) -> PgParquetCompression {
    let compression_option = copy_stmt_get_option(p_stmt, "compression");

    if compression_option.is_null() {
        PgParquetCompression::try_from(uri_info.uri.clone()).unwrap_or_default()
    } else {
        let compression = unsafe { defGetString(compression_option.as_ptr()) };

        let compression = unsafe {
            CStr::from_ptr(compression)
                .to_str()
                .expect("compression option is not a valid CString")
        };

        PgParquetCompression::from_str(compression).unwrap_or_else(|e| panic!("{}", e))
    }
}

pub(crate) fn copy_to_stmt_compression_level(
    p_stmt: &PgBox<PlannedStmt>,
    uri_info: &ParsedUriInfo,
) -> Option<i32> {
    let compression_level_option = copy_stmt_get_option(p_stmt, "compression_level");

    if compression_level_option.is_null() {
        let compression = copy_to_stmt_compression(p_stmt, uri_info);

        compression.default_compression_level()
    } else {
        Some(unsafe { defGetInt32(compression_level_option.as_ptr()) as _ })
    }
}

pub(crate) fn copy_from_stmt_create_option_list(p_stmt: &PgBox<PlannedStmt>) -> PgList<DefElem> {
    let mut new_copy_options = PgList::<DefElem>::new();

    // ensure binary format
    let format_option_name = "format".as_pg_cstr();

    let format_option_val = unsafe { makeString("binary".as_pg_cstr()) } as _;

    let format_option = unsafe { makeDefElem(format_option_name, format_option_val, -1) };

    new_copy_options.push(format_option);

    // add freeze option if it is  present in original statement
    let freeze_option = copy_stmt_get_option(p_stmt, "freeze");

    if !freeze_option.is_null() {
        new_copy_options.push(freeze_option.as_ptr());
    }

    new_copy_options
}

pub(crate) fn copy_from_stmt_match_by(p_stmt: &PgBox<PlannedStmt>) -> MatchBy {
    let match_by_option = copy_stmt_get_option(p_stmt, "match_by");

    if match_by_option.is_null() {
        MatchBy::default()
    } else {
        let match_by = unsafe { defGetString(match_by_option.as_ptr()) };

        let match_by = unsafe {
            CStr::from_ptr(match_by)
                .to_str()
                .expect("match_by option is not a valid CString")
        };

        MatchBy::from_str(match_by).unwrap_or_else(|e| panic!("{}", e))
    }
}

pub(crate) fn copy_stmt_get_option(
    p_stmt: &PgBox<PlannedStmt>,
    option_name: &str,
) -> PgBox<DefElem> {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    let copy_options = unsafe { PgList::<DefElem>::from_pg(copy_stmt.options) };

    for current_option in copy_options.iter_ptr() {
        let current_option = unsafe { PgBox::<DefElem>::from_pg(current_option) };

        let current_option_name = unsafe {
            CStr::from_ptr(current_option.defname)
                .to_str()
                .expect("copy option is not a valid CString")
        };

        if current_option_name == option_name {
            return current_option;
        }
    }

    PgBox::null()
}

fn is_copy_parquet_stmt(p_stmt: &PgBox<PlannedStmt>, copy_from: bool) -> bool {
    // the GUC pg_parquet.enable_copy_hook must be set to true
    if !ENABLE_PARQUET_COPY_HOOK.get() {
        return false;
    }

    let is_copy_stmt = unsafe { is_a(p_stmt.utilityStmt, T_CopyStmt) };

    if !is_copy_stmt {
        return false;
    }

    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    if copy_from != copy_stmt.is_from {
        return false;
    }

    if copy_stmt.is_program {
        return false;
    }

    let uri_info = copy_stmt_uri(p_stmt);

    // uri can not be handled by pg_parquet
    if uri_info.is_err() {
        return false;
    }

    let uri_info = uri_info.unwrap();

    // only parquet format is supported
    if !is_parquet_format_option(p_stmt) && !is_parquet_uri(uri_info.uri.clone()) {
        return false;
    }

    // extension checks are done via catalog (not yet searched via cache by postgres till pg18)
    // this is why we check them after the uri checks

    // crunchy_query_engine should not be created
    if extension_exists("crunchy_query_engine") {
        return false;
    }

    // pg_parquet should be created
    if !extension_exists("pg_parquet") {
        ereport!(
            PgLogLevel::WARNING,
            PgSqlErrorCode::ERRCODE_WARNING,
            "pg_parquet can handle this COPY command but is not enabled",
            "Run CREATE EXTENSION pg_parquet; to enable the pg_parquet extension.",
        );

        return false;
    }

    true
}

pub(crate) fn is_copy_to_parquet_stmt(p_stmt: &PgBox<PlannedStmt>) -> bool {
    let copy_from = false;
    is_copy_parquet_stmt(p_stmt, copy_from)
}

pub(crate) fn is_copy_from_parquet_stmt(p_stmt: &PgBox<PlannedStmt>) -> bool {
    let copy_from = true;
    is_copy_parquet_stmt(p_stmt, copy_from)
}

fn is_parquet_format_option(p_stmt: &PgBox<PlannedStmt>) -> bool {
    let format_option = copy_stmt_get_option(p_stmt, "format");

    if format_option.is_null() {
        return false;
    }

    let format = unsafe { defGetString(format_option.as_ptr()) };

    let format = unsafe {
        CStr::from_ptr(format)
            .to_str()
            .unwrap_or_else(|e| panic!("format option is not a valid CString: {}", e))
    };

    format == "parquet"
}

fn is_parquet_uri(uri: Url) -> bool {
    PgParquetCompression::try_from(uri).is_ok()
}

pub(crate) fn copy_stmt_has_relation(p_stmt: &PgBox<PlannedStmt>) -> bool {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    !copy_stmt.relation.is_null()
}

pub(crate) fn copy_stmt_lock_mode(p_stmt: &PgBox<PlannedStmt>) -> i32 {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    if copy_stmt.is_from {
        RowExclusiveLock as _
    } else {
        AccessShareLock as _
    }
}

pub(crate) fn copy_stmt_relation_oid(p_stmt: &PgBox<PlannedStmt>) -> Oid {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    let copy_stmt_range_var = copy_stmt.relation;

    let relation_oid = unsafe {
        RangeVarGetRelidExtended(
            copy_stmt_range_var,
            NoLock as _,
            0,
            None,
            std::ptr::null_mut(),
        )
    };

    let schema_oid = unsafe { get_rel_namespace(relation_oid) };

    let schema_name = unsafe { get_namespace_name(schema_oid) };

    let copy_stmt_range_var = unsafe { PgBox::<RangeVar>::from_pg(copy_stmt_range_var) };

    let qualified_relname =
        unsafe { quote_qualified_identifier(schema_name, copy_stmt_range_var.relname) };

    let qualified_relname = unsafe {
        CStr::from_ptr(qualified_relname)
            .to_str()
            .expect("qualified_relname is not a valid CString")
    };

    let copy_stmt_relation = PgRelation::open_with_name_and_share_lock(qualified_relname)
        .unwrap_or_else(|e| panic!("{}", e));

    copy_stmt_relation.rd_id
}

pub(crate) fn copy_stmt_create_parse_state(
    query_string: &CStr,
    query_env: &PgBox<QueryEnvironment>,
) -> PgBox<ParseState> {
    /* construct a parse state similar to standard_ProcessUtility */
    let p_state: *mut ParseState = unsafe { make_parsestate(std::ptr::null_mut()) };
    let mut p_state = unsafe { PgBox::from_pg(p_state) };
    p_state.p_sourcetext = query_string.as_ptr();
    p_state.p_queryEnv = query_env.as_ptr();
    p_state
}

pub(crate) fn copy_stmt_create_namespace_item(
    p_stmt: &PgBox<PlannedStmt>,
    p_state: &PgBox<ParseState>,
    relation: &PgRelation,
) -> PgBox<ParseNamespaceItem> {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    let ns_item = unsafe {
        addRangeTableEntryForRelation(
            p_state.as_ptr(),
            relation.as_ptr(),
            if copy_stmt.is_from {
                RowExclusiveLock
            } else {
                AccessShareLock
            } as _,
            std::ptr::null_mut(),
            false,
            false,
        )
    };
    unsafe { PgBox::from_pg(ns_item) }
}

fn copy_stmt_attribute_names(p_stmt: &PgBox<PlannedStmt>) -> Vec<String> {
    let attribute_name_list = copy_stmt_attribute_list(p_stmt);

    unsafe {
        PgList::from_pg(attribute_name_list)
            .iter_ptr()
            .map(|attribute_name: *mut Node| strVal(attribute_name))
            .collect::<Vec<_>>()
    }
}

pub(crate) fn copy_stmt_attribute_list(p_stmt: &PgBox<PlannedStmt>) -> *mut List {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };
    copy_stmt.attlist
}

// create_filtered_tupledesc_for_relation creates a new tuple descriptor for the COPY operation by
// removing dropped attributes and filtering the attributes based on the attribute name list.
pub(crate) fn create_filtered_tupledesc_for_relation<'a>(
    p_stmt: &PgBox<PlannedStmt>,
    relation: &'a PgRelation,
) -> PgTupleDesc<'a> {
    let attribute_name_list = copy_stmt_attribute_names(p_stmt);

    let relation_tupledesc = relation.tuple_desc();

    if attribute_name_list.is_empty() {
        return relation_tupledesc;
    }

    let filtered_tupledesc = unsafe { CreateTemplateTupleDesc(attribute_name_list.len() as i32) };
    let filtered_tupledesc = unsafe { PgTupleDesc::from_pg(filtered_tupledesc) };

    let mut attribute_number = 1;

    for attribute_name in attribute_name_list.iter() {
        let mut found = false;

        for attribute in relation_tupledesc.iter() {
            if attribute.is_dropped() {
                continue;
            }

            let att_typoid = attribute.type_oid().value();
            let att_typmod = attribute.type_mod();
            let att_ndims = attribute.attndims;

            if attribute.name() == attribute_name {
                unsafe {
                    TupleDescInitEntry(
                        filtered_tupledesc.as_ptr(),
                        attribute_number,
                        attribute_name.as_pg_cstr() as _,
                        att_typoid,
                        att_typmod,
                        att_ndims as _,
                    )
                };

                attribute_number += 1;

                found = true;

                break;
            }
        }

        if !found {
            panic!(
                "column \"{}\" of relation \"{}\" does not exist",
                attribute_name,
                relation.name()
            );
        }
    }

    filtered_tupledesc
}

/// Parses a size string like "1MB", "512KB", or just "1000000" into a byte count.
/// Enforces a minimum of 1MB.
fn parse_file_size(size_str: &str) -> Result<u64, String> {
    // Normalize casing and trim whitespace
    let size_str = size_str.trim().to_uppercase();

    // Find the first non-digit character
    let mut idx = 0;
    for c in size_str.chars() {
        if !c.is_ascii_digit() {
            break;
        }
        idx += 1;
    }

    // If there's no numeric portion, return an error
    if idx == 0 {
        return Err(format!("No numeric value found in '{}'", size_str));
    }

    // Split into numeric part and (optional) unit
    let num_part = &size_str[..idx];
    let unit_part = size_str[idx..].trim();

    // Convert the numeric portion
    let mut bytes = match num_part.parse::<u64>() {
        Ok(n) => n,
        Err(_) => return Err(format!("Invalid numeric portion in '{}'", size_str)),
    };

    // Interpret the suffix, if present
    match unit_part {
        "" => { /* no suffix: treat as bytes */ }
        "KB" => bytes *= 1_024,
        "MB" => bytes *= 1_024 * 1_024,
        "GB" => bytes *= 1_024 * 1_024 * 1_024,
        _ => {
            return Err(format!(
                "Unrecognized unit '{}'. Allowed units are KB, MB or GB.",
                unit_part
            ))
        }
    }

    // Enforce a minimum of 1MB
    if bytes < 1_024 * 1_024 {
        return Err(format!("Minimum allowed size is 1MB. Got {} bytes.", bytes));
    }

    Ok(bytes)
}

use std::ffi::{c_char, CStr};

use pg_sys::{
    standard_ProcessUtility, AsPgCStr, CommandTag, DestReceiver, ParamListInfoData, PlannedStmt,
    ProcessUtility_hook, ProcessUtility_hook_type, QueryCompletion, QueryEnvironment,
};
use pgrx::{prelude::*, GucSetting};

use crate::{
    arrow_parquet::{
        compression::INVALID_COMPRESSION_LEVEL,
        uri_utils::{ensure_access_privilege_to_uri, uri_as_string},
    },
    create_copy_to_parquet_split_dest_receiver,
    parquet_copy_hook::copy_utils::{
        copy_stmt_uri, copy_to_stmt_compression_level, copy_to_stmt_row_group_size,
        copy_to_stmt_row_group_size_bytes, is_copy_from_parquet_stmt, is_copy_to_parquet_stmt,
    },
};

use super::{
    copy_from::{execute_copy_from, pop_parquet_reader_context},
    copy_to::execute_copy_to_with_dest_receiver,
    copy_to_split_dest_receiver::free_copy_to_parquet_split_dest_receiver,
    copy_utils::{
        copy_to_stmt_compression, copy_to_stmt_field_ids, copy_to_stmt_file_size_bytes,
        validate_copy_from_options, validate_copy_to_options,
    },
};

pub(crate) static ENABLE_PARQUET_COPY_HOOK: GucSetting<bool> = GucSetting::<bool>::new(true);

static mut PREV_PROCESS_UTILITY_HOOK: ProcessUtility_hook_type = None;

#[pg_guard]
#[no_mangle]
pub(crate) extern "C-unwind" fn init_parquet_copy_hook() {
    #[allow(static_mut_refs)]
    unsafe {
        if ProcessUtility_hook.is_some() {
            PREV_PROCESS_UTILITY_HOOK = ProcessUtility_hook
        }

        ProcessUtility_hook = Some(parquet_copy_hook);
    }
}

fn process_copy_to_parquet(
    p_stmt: &PgBox<PlannedStmt>,
    query_string: &CStr,
    params: &PgBox<ParamListInfoData>,
    query_env: &PgBox<QueryEnvironment>,
) -> u64 {
    let uri_info = copy_stmt_uri(p_stmt).unwrap_or_else(|e| panic!("{}", e));

    let uri = uri_info.uri.clone();

    let copy_from = false;
    ensure_access_privilege_to_uri(&uri, copy_from);

    validate_copy_to_options(p_stmt, &uri_info);

    let file_size_bytes = copy_to_stmt_file_size_bytes(p_stmt);
    let field_ids = copy_to_stmt_field_ids(p_stmt);
    let row_group_size = copy_to_stmt_row_group_size(p_stmt);
    let row_group_size_bytes = copy_to_stmt_row_group_size_bytes(p_stmt);
    let compression = copy_to_stmt_compression(p_stmt, &uri_info);
    let compression_level = copy_to_stmt_compression_level(p_stmt, &uri_info);

    let parquet_split_dest = create_copy_to_parquet_split_dest_receiver(
        uri_as_string(&uri).as_pg_cstr(),
        uri_info.stdio_tmp_fd.is_some(),
        &file_size_bytes,
        field_ids,
        &row_group_size,
        &row_group_size_bytes,
        &compression,
        &compression_level.unwrap_or(INVALID_COMPRESSION_LEVEL),
    );

    let parquet_split_dest = unsafe { PgBox::from_pg(parquet_split_dest) };

    PgTryBuilder::new(|| {
        execute_copy_to_with_dest_receiver(
            p_stmt,
            query_string,
            params,
            query_env,
            &parquet_split_dest,
        )
    })
    .catch_others(|cause| cause.rethrow())
    .finally(|| {
        free_copy_to_parquet_split_dest_receiver(parquet_split_dest.as_ptr());
    })
    .execute()
}

fn process_copy_from_parquet(
    p_stmt: &PgBox<PlannedStmt>,
    query_string: &CStr,
    query_env: &PgBox<QueryEnvironment>,
) -> u64 {
    let uri_info = copy_stmt_uri(p_stmt).unwrap_or_else(|e| panic!("{}", e));

    let uri = uri_info.uri.clone();

    let copy_from = true;
    ensure_access_privilege_to_uri(&uri, copy_from);

    validate_copy_from_options(p_stmt);

    PgTryBuilder::new(|| execute_copy_from(p_stmt, query_string, query_env, &uri_info))
        .catch_others(|cause| {
            // make sure to pop the parquet reader context
            // In case we did not push the context, we should not throw an error while popping
            let throw_error = false;
            pop_parquet_reader_context(throw_error);

            cause.rethrow()
        })
        .execute()
}

#[pg_guard]
#[allow(clippy::too_many_arguments)]
extern "C-unwind" fn parquet_copy_hook(
    p_stmt: *mut PlannedStmt,
    query_string: *const c_char,
    read_only_tree: bool,
    context: u32,
    params: *mut ParamListInfoData,
    query_env: *mut QueryEnvironment,
    dest: *mut DestReceiver,
    completion_tag: *mut QueryCompletion,
) {
    let p_stmt = unsafe { PgBox::from_pg(p_stmt) };
    let query_string = unsafe { CStr::from_ptr(query_string) };
    let params = unsafe { PgBox::from_pg(params) };
    let query_env = unsafe { PgBox::from_pg(query_env) };
    let mut completion_tag = unsafe { PgBox::from_pg(completion_tag) };

    if is_copy_to_parquet_stmt(&p_stmt) {
        let nprocessed = process_copy_to_parquet(&p_stmt, query_string, &params, &query_env);

        if !completion_tag.is_null() {
            completion_tag.nprocessed = nprocessed;
            completion_tag.commandTag = CommandTag::CMDTAG_COPY;
        }
        return;
    } else if is_copy_from_parquet_stmt(&p_stmt) {
        let nprocessed = process_copy_from_parquet(&p_stmt, query_string, &query_env);

        if !completion_tag.is_null() {
            completion_tag.nprocessed = nprocessed;
            completion_tag.commandTag = CommandTag::CMDTAG_COPY;
        }
        return;
    }

    unsafe {
        if let Some(prev_hook) = PREV_PROCESS_UTILITY_HOOK {
            prev_hook(
                p_stmt.into_pg(),
                query_string.as_ptr(),
                read_only_tree,
                context,
                params.into_pg(),
                query_env.into_pg(),
                dest,
                completion_tag.into_pg(),
            )
        } else {
            standard_ProcessUtility(
                p_stmt.into_pg(),
                query_string.as_ptr(),
                read_only_tree,
                context,
                params.into_pg(),
                query_env.into_pg(),
                dest,
                completion_tag.into_pg(),
            )
        }
    }
}

use std::ffi::CStr;

use pg_sys::{
    standard_ProcessUtility, AsPgCStr, CommandTag, DestReceiver, ParamListInfoData, PlannedStmt,
    ProcessUtility_hook, ProcessUtility_hook_type, QueryCompletion, QueryEnvironment,
};
use pgrx::{prelude::*, GucSetting};

use crate::{
    arrow_parquet::{compression::INVALID_COMPRESSION_LEVEL, uri_utils::uri_as_string},
    parquet_copy_hook::{
        copy_to_dest_receiver::create_copy_to_parquet_dest_receiver,
        copy_utils::{
            copy_stmt_uri, copy_to_stmt_compression_level, copy_to_stmt_row_group_size,
            copy_to_stmt_row_group_size_bytes, is_copy_from_parquet_stmt, is_copy_to_parquet_stmt,
        },
    },
};

use super::{
    copy_from::{execute_copy_from, pop_parquet_reader_context},
    copy_to::execute_copy_to_with_dest_receiver,
    copy_to_dest_receiver::pop_parquet_writer_context,
    copy_utils::{copy_to_stmt_compression, validate_copy_from_options, validate_copy_to_options},
};

pub(crate) static ENABLE_PARQUET_COPY_HOOK: GucSetting<bool> = GucSetting::<bool>::new(true);

static mut PREV_PROCESS_UTILITY_HOOK: ProcessUtility_hook_type = None;

#[pg_guard]
#[no_mangle]
pub(crate) extern "C" fn init_parquet_copy_hook() {
    unsafe {
        if ProcessUtility_hook.is_some() {
            PREV_PROCESS_UTILITY_HOOK = ProcessUtility_hook
        }

        ProcessUtility_hook = Some(parquet_copy_hook);
    }
}

#[pg_guard]
#[allow(clippy::too_many_arguments)]
extern "C" fn parquet_copy_hook(
    p_stmt: *mut PlannedStmt,
    query_string: *const i8,
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

    if ENABLE_PARQUET_COPY_HOOK.get() && is_copy_to_parquet_stmt(&p_stmt) {
        let uri = copy_stmt_uri(&p_stmt).expect("uri is None");

        validate_copy_to_options(&p_stmt, &uri);

        let row_group_size = copy_to_stmt_row_group_size(&p_stmt);
        let row_group_size_bytes = copy_to_stmt_row_group_size_bytes(&p_stmt);
        let compression = copy_to_stmt_compression(&p_stmt, uri.clone());
        let compression_level = copy_to_stmt_compression_level(&p_stmt, uri.clone());

        PgTryBuilder::new(|| {
            let parquet_dest = create_copy_to_parquet_dest_receiver(
                uri_as_string(&uri).as_pg_cstr(),
                &row_group_size,
                &row_group_size_bytes,
                &compression,
                &compression_level.unwrap_or(INVALID_COMPRESSION_LEVEL),
            );

            let parquet_dest = unsafe { PgBox::from_pg(parquet_dest) };

            let nprocessed = execute_copy_to_with_dest_receiver(
                &p_stmt,
                query_string,
                params,
                query_env,
                parquet_dest,
            );

            let mut completion_tag = unsafe { PgBox::from_pg(completion_tag) };

            if !completion_tag.is_null() {
                completion_tag.nprocessed = nprocessed;
                completion_tag.commandTag = CommandTag::CMDTAG_COPY;
            }
        })
        .catch_others(|cause| {
            // make sure to pop the parquet writer context
            // In case we did not push the context, we should not throw an error while popping
            let throw_error = false;
            pop_parquet_writer_context(throw_error);

            cause.rethrow()
        })
        .execute();

        return;
    } else if ENABLE_PARQUET_COPY_HOOK.get() && is_copy_from_parquet_stmt(&p_stmt) {
        let uri = copy_stmt_uri(&p_stmt).expect("uri is None");

        validate_copy_from_options(&p_stmt);

        PgTryBuilder::new(|| {
            let nprocessed = execute_copy_from(p_stmt, query_string, query_env, uri);

            let mut completion_tag = unsafe { PgBox::from_pg(completion_tag) };

            if !completion_tag.is_null() {
                completion_tag.nprocessed = nprocessed;
                completion_tag.commandTag = CommandTag::CMDTAG_COPY;
            }
        })
        .catch_others(|cause| {
            // make sure to pop the parquet reader context
            // In case we did not push the context, we should not throw an error while popping
            let throw_error = false;
            pop_parquet_reader_context(throw_error);

            cause.rethrow()
        })
        .execute();

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
                completion_tag,
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
                completion_tag,
            )
        }
    }
}

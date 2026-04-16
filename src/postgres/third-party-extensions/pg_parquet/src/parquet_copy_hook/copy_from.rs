use std::ffi::CStr;

use pgrx::{
    ereport, pg_guard,
    pg_sys::{
        addNSItemToQuery, assign_expr_collations, canonicalize_qual, check_enable_rls,
        coerce_to_boolean, eval_const_expressions, make_ands_implicit, transformExpr, AsPgCStr,
        BeginCopyFrom, CheckEnableRlsResult, CopyFrom, CopyStmt, EndCopyFrom, InvalidOid, Node,
        Oid, ParseExprKind, ParseNamespaceItem, ParseState, PlannedStmt, QueryEnvironment,
    },
    void_mut_ptr, PgBox, PgLogLevel, PgRelation, PgSqlErrorCode,
};

use crate::{
    arrow_parquet::{parquet_reader::ParquetReaderContext, uri_utils::ParsedUriInfo},
    parquet_copy_hook::copy_utils::{
        copy_from_stmt_create_option_list, copy_stmt_lock_mode, copy_stmt_relation_oid,
    },
};

use super::{
    copy_from_stdin::copy_stdin_to_file,
    copy_utils::{
        copy_from_stmt_match_by, copy_stmt_attribute_list, copy_stmt_create_namespace_item,
        copy_stmt_create_parse_state, create_filtered_tupledesc_for_relation,
    },
    pg_compat::check_copy_table_permission,
};

// stack to store parquet reader contexts for COPY FROM.
// This needs to be a stack since COPY command can be nested.
static mut PARQUET_READER_CONTEXT_STACK: Vec<ParquetReaderContext> = vec![];

pub(crate) fn peek_parquet_reader_context() -> Option<&'static mut ParquetReaderContext> {
    #[allow(static_mut_refs)]
    unsafe {
        PARQUET_READER_CONTEXT_STACK.last_mut()
    }
}

pub(crate) fn pop_parquet_reader_context(throw_error: bool) {
    #[allow(static_mut_refs)]
    let mut current_parquet_reader_context = unsafe { PARQUET_READER_CONTEXT_STACK.pop() };

    if current_parquet_reader_context.is_none() {
        let level = if throw_error {
            PgLogLevel::ERROR
        } else {
            PgLogLevel::DEBUG2
        };

        ereport!(
            level,
            PgSqlErrorCode::ERRCODE_INTERNAL_ERROR,
            "parquet reader context stack is already empty"
        );
    } else {
        current_parquet_reader_context.take();
    }
}

pub(crate) fn push_parquet_reader_context(reader_ctx: ParquetReaderContext) {
    #[allow(static_mut_refs)]
    unsafe {
        PARQUET_READER_CONTEXT_STACK.push(reader_ctx)
    };
}

// This function is called by the COPY FROM command to read data from the parquet file
// into output buffer passed by the executor.
#[pg_guard]
extern "C-unwind" fn copy_parquet_data_to_buffer(
    outbuf: void_mut_ptr,
    _minread: i32,
    maxread: i32,
) -> i32 {
    let current_parquet_reader_context = peek_parquet_reader_context();
    if current_parquet_reader_context.is_none() {
        panic!("parquet reader context is not found");
    }
    let current_parquet_reader_context =
        current_parquet_reader_context.expect("current parquet reader context is not found");

    let mut bytes_size = current_parquet_reader_context.not_yet_copied_bytes();

    if bytes_size == 0 {
        current_parquet_reader_context.reset_buffer();

        if !current_parquet_reader_context.read_parquet() {
            return 0;
        }

        bytes_size = current_parquet_reader_context.not_yet_copied_bytes();
    }

    let bytes_size_to_copy = std::cmp::min(maxread as usize, bytes_size);
    current_parquet_reader_context.copy_to_outbuf(bytes_size_to_copy, outbuf);

    bytes_size_to_copy as _
}

// execute_copy_from is called by the ProcessUtility_hook to execute the COPY FROM command.
// It reads data from the parquet file and writes it to the copy buffer that is passed by
// the registered COPY FROM callback function.
//
// 1. Before reading the data,
//    - creates a new parse state by adding range table entry for the relation,
//    - transforms the WHERE clause to a form that can be used by the executor,
//    - creates a new tuple descriptor for the COPY FROM operation by removing dropped attributes
//      and filtering the attributes based on the attribute name list,
//    - creates a ParquetReaderContext that is used to read data from the parquet file.
// 2. Registers a callback function, which is called by the executor, to read data from
//    the output buffer parameter that is written by ParquetReaderContext.
// 3. Calls the executor's CopyFrom function to read data from the parquet file and write
//    it to the copy buffer.
pub(crate) fn execute_copy_from(
    p_stmt: &PgBox<PlannedStmt>,
    query_string: &CStr,
    query_env: &PgBox<QueryEnvironment>,
    uri_info: &ParsedUriInfo,
) -> u64 {
    let rel_oid = copy_stmt_relation_oid(p_stmt);

    copy_from_stmt_ensure_row_level_security(rel_oid);

    let lock_mode = copy_stmt_lock_mode(p_stmt);

    let relation = unsafe { PgRelation::with_lock(rel_oid, lock_mode) };

    let p_state = copy_stmt_create_parse_state(query_string, query_env);

    let ns_item = copy_stmt_create_namespace_item(p_stmt, &p_state, &relation);

    let mut where_clause = copy_from_stmt_where_clause(p_stmt);

    if !where_clause.is_null() {
        where_clause = copy_from_stmt_transform_where_clause(&p_state, &ns_item, where_clause);
    }

    check_copy_table_permission(p_stmt, &p_state, &ns_item, &relation);

    let attribute_list = copy_stmt_attribute_list(p_stmt);

    let tupledesc = create_filtered_tupledesc_for_relation(p_stmt, &relation);

    let match_by = copy_from_stmt_match_by(p_stmt);

    unsafe {
        if uri_info.stdio_tmp_fd.is_some() {
            let is_binary = true;
            copy_stdin_to_file(uri_info, tupledesc.natts as _, is_binary);
        }

        // parquet reader context is used throughout the COPY FROM operation.
        let parquet_reader_context = ParquetReaderContext::new(uri_info, match_by, &tupledesc);
        push_parquet_reader_context(parquet_reader_context);

        // makes sure to set binary format
        let copy_options = copy_from_stmt_create_option_list(p_stmt);

        let copy_from_state = BeginCopyFrom(
            p_state.as_ptr(),
            relation.as_ptr(),
            where_clause,
            std::ptr::null(),
            false,
            Some(copy_parquet_data_to_buffer),
            attribute_list,
            copy_options.as_ptr(),
        );

        let nprocessed = CopyFrom(copy_from_state);

        EndCopyFrom(copy_from_state);

        let throw_error = true;
        pop_parquet_reader_context(throw_error);

        nprocessed
    }
}

fn copy_from_stmt_where_clause(p_stmt: &PgBox<PlannedStmt>) -> *mut Node {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };
    copy_stmt.whereClause
}

fn copy_from_stmt_transform_where_clause(
    p_state: &PgBox<ParseState>,
    ns_item: &PgBox<ParseNamespaceItem>,
    where_clause: *mut Node,
) -> *mut Node {
    unsafe { addNSItemToQuery(p_state.as_ptr(), ns_item.as_ptr(), false, true, true) };

    let where_clause = unsafe {
        transformExpr(
            p_state.as_ptr(),
            where_clause,
            ParseExprKind::EXPR_KIND_COPY_WHERE,
        )
    };

    let construct_name = "WHERE";
    let where_clause =
        unsafe { coerce_to_boolean(p_state.as_ptr(), where_clause, construct_name.as_pg_cstr()) };

    unsafe { assign_expr_collations(p_state.as_ptr(), where_clause) };

    let where_clause = unsafe { eval_const_expressions(std::ptr::null_mut(), where_clause) };

    let where_clause = unsafe { canonicalize_qual(where_clause as _, false) };

    let where_clause = unsafe { make_ands_implicit(where_clause as _) };

    where_clause as _
}

// copy_from_stmt_ensure_row_level_security ensures that the relation does not have row-level
// security enabled for COPY FROM operation.
// Taken from PG COPY FROM code path.
fn copy_from_stmt_ensure_row_level_security(relation_oid: Oid) {
    if unsafe { check_enable_rls(relation_oid, InvalidOid, false) }
        == CheckEnableRlsResult::RLS_ENABLED as i32
    {
        ereport!(
            pgrx::PgLogLevel::ERROR,
            PgSqlErrorCode::ERRCODE_FEATURE_NOT_SUPPORTED,
            "COPY FROM not supported with row-level security",
            "Use INSERT statements instead."
        );
    }
}

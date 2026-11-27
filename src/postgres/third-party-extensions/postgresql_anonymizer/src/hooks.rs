use crate::compat;
use crate::error;
use crate::guc;
use crate::log;
use crate::masking;
use crate::utils;
use crate::walker;

use pgrx::list::old_list::PgList;
use pgrx::pg_sys::ffi::pg_guard_ffi_boundary;
use pgrx::prelude::*;
use std::os::raw::c_char;

/// Register the PostgreSQL hooks
///
/// Since PGRX 0.16, the pgHooks trait is no longer available and we
/// need to assign the function pointers manually
///
pub unsafe fn register_hooks() {
    //
    // Post Parse Analyze hook
    //
    // The post_parse_analyze hook is called after parse analyze goes,
    // immediately after performing transformTopLevelStmt()
    // When a masked role sends a query, the query will be "masked" using
    // the masking rules available
    //
    static mut PREV_POST_PARSE_ANALYZE_HOOK: pg_sys::post_parse_analyze_hook_type = None;
    unsafe {
        PREV_POST_PARSE_ANALYZE_HOOK = pg_sys::post_parse_analyze_hook;
        pg_sys::post_parse_analyze_hook = Some(post_parse_analyze_hook);
    }

    #[pg_guard]
    unsafe extern "C-unwind" fn post_parse_analyze_hook(
        parse_state: *mut pg_sys::ParseState,
        query: *mut pg_sys::Query,
        jumble_state: *mut pg_sys::JumbleState,
    ) {
        unsafe {
            pa_rewrite_select(&PgBox::from_pg(query));
            if let Some(prev_hook) = PREV_POST_PARSE_ANALYZE_HOOK {
                pg_guard_ffi_boundary(|| prev_hook(parse_state, query, jumble_state));
            }
        }
    }

    //
    // Process Utility Hook
    //
    // The process_utility_hook is called for each utility commands
    // (i.e. anything other SELECT,INSERT, UPDATE,DELETE)
    //
    // It is used to rewrite the `COPY .. TO stdout` statements launched by
    // pg_dump
    //
    static mut PREV_PROCESS_UTILITY_HOOK: pg_sys::ProcessUtility_hook_type = None;
    unsafe {
        PREV_PROCESS_UTILITY_HOOK = pg_sys::ProcessUtility_hook;
        pg_sys::ProcessUtility_hook = Some(process_utility_hook);
    }

    #[allow(clippy::too_many_arguments)]
    #[pg_guard]
    unsafe extern "C-unwind" fn process_utility_hook(
        pstmt: *mut pg_sys::PlannedStmt,
        query_string: *const c_char,
        read_only_tree: bool,
        context: u32,
        params: *mut pg_sys::ParamListInfoData,
        query_env: *mut pg_sys::QueryEnvironment,
        dest: *mut pg_sys::DestReceiver,
        completion_tag: *mut pg_sys::QueryCompletion,
    ) {
        unsafe {
            pa_rewrite_utility(&PgBox::from_pg(pstmt));
            if let Some(prev_hook) = PREV_PROCESS_UTILITY_HOOK {
                pg_guard_ffi_boundary(|| {
                    prev_hook(
                        pstmt,
                        query_string,
                        read_only_tree,
                        context,
                        params,
                        query_env,
                        dest,
                        completion_tag,
                    )
                });
            } else {
                pg_sys::standard_ProcessUtility(
                    pstmt,
                    query_string,
                    read_only_tree,
                    context,
                    params,
                    query_env,
                    dest,
                    completion_tag,
                )
            }
        }
    }
}

/// Apply masking rules to a SELECT statement
///
/// For instance, if a masking rule is defined on the column 'lastname' of table
/// named 'person', then the statement below:
///
/// ```sql no_run
/// SELECT * FROM public.person;
/// ```
///
/// Will be replaced (basically) by
///
/// ```sql
///     SELECT firstname AS firstname,
///            CAST(NULL AS text) AS lastname
///     FROM person;
/// ```
///
/// The function returns None if the query is unchanged
///
fn pa_rewrite_select(query: &PgBox<pg_sys::Query>) -> Option<bool> {
    if !unsafe { pg_sys::IsTransactionState() } {
        return None;
    };

    let uid = unsafe { pg_sys::GetUserId() };
    if !guc::ANON_TRANSPARENT_DYNAMIC_MASKING.get() {
        return None;
    };

    let masking_policy = masking::get_masking_policy(uid)?;

    // the user is masked

    // masked users are not allowed to use restricted functions directly
    // restricted functions (such as pseudonymizing functions) are TRUSTED but
    // cannot be called by a masked user.
    if unsafe { walker::TreeWalker::empty().has_restricted_function(query) } {
        error::insufficient_privilege("role is masked".to_string()).ereport();
    }

    // rewrite the query
    unsafe { walker::TreeWalker::new(masking_policy).rewrite(query) };

    Some(true)
}

/// Apply masking rules to a COPY statement
/// In a COPY statement, substitute the masked relation by its masking view
///
/// For instance, the statement below :
///   COPY person TO stdout;
///
/// will be replaced by :
///
///   COPY ( SELECT * FROM "public"."person" ) TO stdout;
///
/// Later in the process, the post_parse_analyze hook will be triggered
/// and the `rewrite_walker()` function will rewrite it again like this
///
///   COPY (
///       SELECT firstname AS firstname,
///              CAST(NULL AS text) AS lastname
///       FROM person
///   ) TO stdout;
///
/// Arguments:
/// * `pstmt` is the utility statement
/// * `policy` is the masking policy to apply
///
fn pa_rewrite_utility(pstmt: &PgBox<pg_sys::PlannedStmt>) {
    if !unsafe { pg_sys::IsTransactionState() } {
        return;
    }

    // Rewrite the utility command only if transparent dynamic masking is enabled
    if !guc::ANON_TRANSPARENT_DYNAMIC_MASKING.get() {
        return;
    }

    // Rewrite the utility command only for masked users
    let uid = unsafe { pg_sys::GetUserId() };
    let Some(policy) = masking::get_masking_policy(uid) else {
        return;
    };

    // Check that rhe statement is a utility command
    let command_type = pstmt.commandType;
    assert!(command_type == pg_sys::CmdType::CMD_UTILITY);

    // here is a list of statements we can't really handle for now
    //
    // * EXPLAIN can leak metadata to the masked roles
    // * TRUNCATE is not allowed because masked roles are read-only
    //
    unsafe {
        if pgrx::is_a(pstmt.utilityStmt, pg_sys::NodeTag::T_ExplainStmt)
            || pgrx::is_a(pstmt.utilityStmt, pg_sys::NodeTag::T_TruncateStmt)
        {
            error::insufficient_privilege("role is masked".to_string()).ereport();
        }
    }

    if unsafe { pgrx::is_a(pstmt.utilityStmt, pg_sys::NodeTag::T_DeclareCursorStmt) } {
        log::debug1!("Anon: CURSOR found");
        let cursorstmt =
            unsafe { PgBox::from_pg(pstmt.utilityStmt as *mut pg_sys::DeclareCursorStmt) };
        let cursorquery = unsafe { PgBox::from_pg(cursorstmt.query as *mut pg_sys::Query) };

        unsafe {
            walker::TreeWalker::new(policy).rewrite(&cursorquery);
        }
        cursorstmt.into_pg();
    }

    if unsafe { pgrx::is_a(pstmt.utilityStmt, pg_sys::NodeTag::T_CopyStmt) } {
        log::debug1!("Anon: COPY found");
        // The utilityStmt is provided as a pointer to a generice Node
        // But we now know that this Node is a CopyStmt
        // So we cast the Node pointer as a CopyStmt pointer to access the
        // CopyStmt properties
        //
        // see https://doxygen.postgresql.org/structCopyStmt.html
        //
        let mut copystmt = unsafe { PgBox::from_pg(pstmt.utilityStmt as *mut pg_sys::CopyStmt) };

        // ignore `COPY FROM` statements
        if copystmt.is_from {
            return;
        }

        // This is a `COPY (SELECT ...) TO` statements
        // The SELECT subquery will be masked later by the `rewrite_walker()`
        // when triggered by the post_parse_analyze hook
        if copystmt.relation.is_null() {
            return;
        }

        // We now know this is a `COPY xxx TO ...` statement
        // Fetch the relation id
        let relid = unsafe {
            pg_sys::RangeVarGetRelidExtended(
                copystmt.relation,
                pg_sys::AccessShareLock as i32,
                0,
                None,
                core::ptr::null_mut(),
            )
        };

        // Generate the masking subquery (aka msq)
        // Here we just replace
        //  ```
        //  COPY foo (a,b,c) TO [...]
        //  ```
        // with
        //  ```
        //  COPY (
        //     SELECT a,b,c FROM ONLY "public"."foo"
        //  ) TO [...]
        //  ```
        //
        // The SELECT subquery will be masked later by `rewrite_walker()`
        // when triggered by the post_parse_analyze hook
        //
        // The final statement will be
        //  ```
        //  COPY (
        //     SELECT a,b,c FROM (
        //         SELECT <masking_filters>
        //         FROM ONLY "public"."foo"
        //         TABLESAMPLE SYSTEM(33)
        //     ) AS foo
        //  ) TO [...]
        //  ```
        //

        let mut attributes: Vec<String> = vec![];
        let att_nodes = unsafe {
            // SAFETY: when there's no attributes, the pgList is empty
            PgList::<pg_sys::Node>::from_pg(copystmt.attlist)
        };
        for node in att_nodes.iter_ptr() {
            // SAFETY: an att_node is never a null pointer
            let attname_val = unsafe { PgBox::from_pg(node as *mut compat::ColumnValue) };
            let attname_c_ptr = unsafe { compat::strVal(*attname_val) };
            let attname_str = utils::quote_identifier(attname_c_ptr);
            attributes.push(attname_str.to_string());
        }

        if attributes.is_empty() {
            attributes = vec!["*".into()];
        }

        let Some(relname) = utils::get_relation_qualified_name(relid) else {
            error::internal("Cannot get relation name");
            return;
        };
        let msq_sql = format!("SELECT {} FROM ONLY {relname}", attributes.join(","));
        let msq_raw_stmt = masking::parse_subquery(msq_sql.clone());
        log::debug3!("Anon: COPY subquery sql = {:#?}", msq_sql);

        // Replace the relation by the masking subquery
        copystmt.relation = core::ptr::null_mut();
        copystmt.attlist = core::ptr::null_mut();
        copystmt.query = msq_raw_stmt.stmt;

        // Return the pointer to Postgres
        copystmt.into_pg();
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::fixture;
    use pgrx::prelude::*;

    //
    // The unit tests for the hooks are a bit complex because we would have
    // to create planned statements from scratch and pass them to the function.
    //
    // Alternatively, we run SQL commands just like we would do in the
    // functional tests.
    //

    #[pg_test]
    #[ignore]
    fn test_process_utility_hook() {
        fixture::create_table_person();
        fixture::create_masked_role();
        Spi::run(
            "
            SET anon.transparent_dynamic_masking TO TRUE;
            GRANT USAGE ON SCHEMA public TO batman;
            GRANT SELECT ON ALL TABLES IN SCHEMA public TO batman;
            SET ROLE batman;
            COPY person TO stdout;
         ",
        )
        .unwrap();
    }

    #[pg_test]
    fn test_security_label() {
        fixture::create_table_person();
        fixture::create_masked_role();
        Spi::run(
            "
            CREATE TABLE wayne_manor AS
                SELECT '224 Park Dr., Gotham City' as address;
            ALTER TABLE wayne_manor OWNER TO batman;
            SET ROLE batman;
            SECURITY LABEL FOR anon ON COLUMN wayne_manor.address
                IS 'MASKED WITH VALUE $$CONFIDENTIAL$$ ';
        ",
        )
        .unwrap();
    }

    #[pg_test]
    fn test_explain() {
        fixture::create_table_person();
        fixture::create_masked_role();
        Spi::run(
            "
            SET ROLE batman;
            EXPLAIN SELECT 1;
        ",
        )
        .unwrap();
    }

    #[pg_test]
    fn test_post_parse_analyze() {
        fixture::create_table_person();
        fixture::create_masked_role();
        Spi::run(
            "
            SET anon.transparent_dynamic_masking TO TRUE;
            GRANT USAGE ON SCHEMA public TO batman;
            GRANT SELECT ON ALL TABLES IN SCHEMA public TO batman;
            SET ROLE batman;
            SELECT lastname IS NULL FROM person LIMIT 1;
        ",
        )
        .unwrap();
    }

    #[pg_test(error = "Anon: role is masked")]
    fn test_post_parse_analyze_pseudo_func() {
        fixture::create_masked_role();
        Spi::run(
            "
            SET anon.transparent_dynamic_masking TO TRUE;
            SET ROLE batman;
            SELECT anon.pseudo_city('bob'::TEXT);
        ",
        )
        .unwrap();
    }

    #[pg_test]
    fn test_post_parse_analyze_unmasked() {
        fixture::create_table_person();
        Spi::run(
            "
            SET anon.transparent_dynamic_masking TO TRUE;
            SELECT lastname IS NULL FROM person LIMIT 1;
        ",
        )
        .unwrap();
    }
}

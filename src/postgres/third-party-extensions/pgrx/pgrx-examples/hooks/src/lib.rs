//
// pg_guard_ffi_boundary is necessary when calling any PostgreSQL-exported
// functions, especially the hooks functions pointers coming from PostgreSQL
// cf. https://github.com/pgcentralfoundation/pgrx/blob/develop/pgrx-pg-sys/src/submodules/ffi.rs.
//

use pgrx::pg_sys::ffi::pg_guard_ffi_boundary;
use pgrx::prelude::*;

::pgrx::pg_module_magic!(name, version);

fn knock_knock() {
    todo!()
}

fn delete_must_have_a_where(query: PgBox<pg_sys::Query>) {
    if query.commandType != pg_sys::CmdType::CMD_DELETE {
        return ();
    }

    // SAFETY: the jointree is always defined in a DELETE Query
    let jointree = unsafe { PgBox::from_pg(query.jointree) };

    if jointree.quals.is_null() {
        panic!("DELETE queries must have a WHERE clause");
    }
}

fn only_superusers_can_truncate(pstmt: PgBox<pg_sys::PlannedStmt>) {
    if !unsafe { pgrx::is_a(pstmt.utilityStmt, pg_sys::NodeTag::T_TruncateStmt) } {
        return ();
    }

    if !unsafe { pg_sys::superuser() } {
        panic!("Only superusers can truncate")
    }
}

unsafe fn register_hooks() {
    //
    // Client Authentication hook
    //
    static mut PREV_CLIENTAUTHENTICATION_HOOK: pg_sys::libpq::ClientAuthentication_hook_type = None;
    PREV_CLIENTAUTHENTICATION_HOOK = pg_sys::libpq::ClientAuthentication_hook;
    pg_sys::libpq::ClientAuthentication_hook = Some(client_authentication_hook);

    #[pg_guard]
    unsafe extern "C-unwind" fn client_authentication_hook(
        port: *mut pg_sys::libpq::be::Port,
        status: i32,
    ) {
        knock_knock();
        if let Some(prev_hook) = PREV_CLIENTAUTHENTICATION_HOOK {
            pg_guard_ffi_boundary(|| prev_hook(port, status));
        }
    }

    //
    // Post Parse Analyze hook
    //
    static mut PREV_POST_PARSE_ANALYZE_HOOK: pg_sys::post_parse_analyze_hook_type = None;
    PREV_POST_PARSE_ANALYZE_HOOK = pg_sys::post_parse_analyze_hook;
    pg_sys::post_parse_analyze_hook = Some(post_parse_analyze_hook);

    // The hook functions signatures may change between major version
    // For instance: in the post_parse_analyze hook, the JumbleState struct
    // appeared in Postgres 14
    // In that case, we need some conditional compilation to declare the
    // proper signature for each version
    #[cfg(feature = "pg13")]
    #[pg_guard]
    unsafe extern "C-unwind" fn post_parse_analyze_hook(
        parse_state: *mut pg_sys::ParseState,
        query: *mut pg_sys::Query,
    ) {
        delete_must_have_a_where(PgBox::from_pg(query));
        if let Some(prev_hook) = PREV_POST_PARSE_ANALYZE_HOOK {
            pg_guard_ffi_boundary(|| prev_hook(parse_state, query));
        }
    }
    #[cfg(any(
        feature = "pg14",
        feature = "pg15",
        feature = "pg16",
        feature = "pg17",
        feature = "pg18"
    ))]
    #[pg_guard]
    unsafe extern "C-unwind" fn post_parse_analyze_hook(
        parse_state: *mut pg_sys::ParseState,
        query: *mut pg_sys::Query,
        jumble_state: *mut pg_sys::JumbleState,
    ) {
        delete_must_have_a_where(PgBox::from_pg(query));
        if let Some(prev_hook) = PREV_POST_PARSE_ANALYZE_HOOK {
            pg_guard_ffi_boundary(|| prev_hook(parse_state, query, jumble_state));
        }
    }

    //
    // Process Utility Hook
    //
    static mut PREV_PROCESS_UTILITY_HOOK: pg_sys::ProcessUtility_hook_type = None;
    PREV_PROCESS_UTILITY_HOOK = pg_sys::ProcessUtility_hook;
    pg_sys::ProcessUtility_hook = Some(process_utility_hook);

    // Until Postgres 13, the process utility hook didn't have a read_only_tree param
    #[cfg(feature = "pg13")]
    #[pg_guard]
    unsafe extern "C-unwind" fn process_utility_hook(
        pstmt: *mut pg_sys::PlannedStmt,
        query_string: *const core::ffi::c_char,
        context: pg_sys::ProcessUtilityContext::Type,
        params: *mut pg_sys::ParamListInfoData,
        query_env: *mut pg_sys::QueryEnvironment,
        dest: *mut pg_sys::DestReceiver,
        completion_tag: *mut pg_sys::QueryCompletion,
    ) {
        only_superusers_can_truncate(PgBox::from_pg(pstmt));
        if let Some(prev_hook) = PREV_PROCESS_UTILITY_HOOK {
            pg_guard_ffi_boundary(|| {
                prev_hook(pstmt, query_string, context, params, query_env, dest, completion_tag)
            });
        } else {
            pg_sys::standard_ProcessUtility(
                pstmt,
                query_string,
                context,
                params,
                query_env,
                dest,
                completion_tag,
            )
        }
    }

    #[cfg(any(
        feature = "pg14",
        feature = "pg15",
        feature = "pg16",
        feature = "pg17",
        feature = "pg18"
    ))]
    #[pg_guard]
    unsafe extern "C-unwind" fn process_utility_hook(
        pstmt: *mut pg_sys::PlannedStmt,
        query_string: *const core::ffi::c_char,
        read_only_tree: bool,
        context: pg_sys::ProcessUtilityContext::Type,
        params: *mut pg_sys::ParamListInfoData,
        query_env: *mut pg_sys::QueryEnvironment,
        dest: *mut pg_sys::DestReceiver,
        completion_tag: *mut pg_sys::QueryCompletion,
    ) {
        only_superusers_can_truncate(PgBox::from_pg(pstmt));
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

#[pg_guard]
pub unsafe extern "C-unwind" fn _PG_init() {
    register_hooks();
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use pgrx::prelude::*;

    #[pg_test]
    fn test_delete_with_where() {
        Spi::run(
            "
            CREATE TABLE t AS SELECT 1 AS one;
            DELETE FROM t WHERE 0 = 0;
            ",
        )
        .unwrap();
        let result: Option<i64> = Spi::get_one("SELECT COUNT(*) FROM t").unwrap();
        assert_eq!(result.unwrap(), 0);
    }

    #[pg_test(error = "DELETE queries must have a WHERE clause")]
    fn test_delete_without_where() {
        Spi::run(
            "
            CREATE TABLE t AS SELECT 1 AS one;
            DELETE FROM t;
            ",
        )
        .unwrap();
    }

    #[pg_test]
    fn test_truncate_from_superuser() {
        Spi::run(
            "
            CREATE TABLE t AS SELECT 1 AS one;
            TRUNCATE t;
            ",
        )
        .unwrap();
        let result: Option<i64> = Spi::get_one("SELECT COUNT(*) FROM t").unwrap();
        assert_eq!(result.unwrap(), 0);
    }

    #[pg_test(error = "Only superusers can truncate")]
    fn test_truncate_from_bob() {
        Spi::run(
            "
            CREATE TABLE t AS SELECT 1 AS one;
            CREATE ROLE bob;
            SET ROLE bob;
            TRUNCATE t;
            ",
        )
        .unwrap();
    }
}

/// This module is required by `cargo pgrx test` invocations.
/// It must be visible at the root of your extension crate.
#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    #[must_use]
    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec![]
    }
}

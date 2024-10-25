use std::ffi::{c_char, CStr};

use pgrx::pg_sys::{AsPgCStr, List, Node, QueryEnvironment, RawStmt};

pub(crate) fn pg_analyze_and_rewrite(
    raw_stmt: *mut RawStmt,
    query_string: *const c_char,
    query_env: *mut QueryEnvironment,
) -> *mut List {
    #[cfg(feature = "pg14")]
    unsafe {
        pgrx::pg_sys::pg_analyze_and_rewrite(
            raw_stmt,
            query_string,
            std::ptr::null_mut(),
            0,
            query_env,
        )
    }

    #[cfg(any(feature = "pg15", feature = "pg16", feature = "pg17"))]
    unsafe {
        pgrx::pg_sys::pg_analyze_and_rewrite_fixedparams(
            raw_stmt,
            query_string,
            std::ptr::null_mut(),
            0,
            query_env,
        )
    }
}

#[allow(non_snake_case)]
pub(crate) fn strVal(val: *mut Node) -> String {
    #[cfg(feature = "pg14")]
    unsafe {
        let val = (*(val as *mut pgrx::pg_sys::Value)).val.str_;

        CStr::from_ptr(val)
            .to_str()
            .expect("invalid string")
            .to_string()
    }

    #[cfg(any(feature = "pg15", feature = "pg16", feature = "pg17"))]
    unsafe {
        let val = (*(val as *mut pgrx::pg_sys::String)).sval;

        CStr::from_ptr(val)
            .to_str()
            .expect("invalid string")
            .to_string()
    }
}

#[allow(non_snake_case)]
pub(crate) fn MarkGUCPrefixReserved(guc_prefix: &str) {
    #[cfg(feature = "pg14")]
    unsafe {
        pgrx::pg_sys::EmitWarningsOnPlaceholders(guc_prefix.as_pg_cstr())
    }

    #[cfg(any(feature = "pg15", feature = "pg16", feature = "pg17"))]
    unsafe {
        pgrx::pg_sys::MarkGUCPrefixReserved(guc_prefix.as_pg_cstr())
    }
}

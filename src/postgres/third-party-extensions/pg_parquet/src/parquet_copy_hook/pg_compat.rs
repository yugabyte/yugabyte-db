use std::ffi::{c_char, CStr};

use pgrx::{
    pg_sys::{
        bms_add_member, AsPgCStr, CopyGetAttnums, CopyStmt, FirstLowInvalidHeapAttributeNumber,
        List, Node, ParseNamespaceItem, ParseState, PlannedStmt, QueryEnvironment, RawStmt,
        ACL_INSERT, ACL_SELECT,
    },
    PgBox, PgList, PgRelation,
};

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

/// check_copy_table_permission checks if the user has permission to copy from/to the table.
/// This is taken from the original PostgreSQL DoCopy function.
#[cfg(any(feature = "pg16", feature = "pg17"))]
pub(crate) fn check_copy_table_permission(
    p_stmt: &PgBox<PlannedStmt>,
    p_state: &PgBox<ParseState>,
    ns_item: &PgBox<ParseNamespaceItem>,
    relation: &PgRelation,
) {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    // init permissions
    let mut perminfo =
        unsafe { PgBox::<pgrx::pg_sys::RTEPermissionInfo>::from_pg(ns_item.p_perminfo) };

    // set table access mode
    perminfo.requiredPerms = if copy_stmt.is_from {
        ACL_INSERT as _
    } else {
        ACL_SELECT as _
    };

    // set column access modes
    let tup_desc = relation.tuple_desc();

    let attnums =
        unsafe { CopyGetAttnums(tup_desc.as_ptr(), relation.as_ptr(), copy_stmt.attlist) };
    let attnums = unsafe { PgList::<i16>::from_pg(attnums) };

    for attnum in attnums.iter_int() {
        let attno = attnum - FirstLowInvalidHeapAttributeNumber;

        if copy_stmt.is_from {
            unsafe { perminfo.insertedCols = bms_add_member(perminfo.insertedCols, attno) };
        } else {
            unsafe { perminfo.selectedCols = bms_add_member(perminfo.selectedCols, attno) };
        }
    }

    // check permissions
    let mut perm_infos = PgList::<pgrx::pg_sys::RTEPermissionInfo>::new();
    perm_infos.push(perminfo.as_ptr());

    unsafe { pgrx::pg_sys::ExecCheckPermissions(p_state.p_rtable, perm_infos.as_ptr(), true) };
}

#[cfg(any(feature = "pg14", feature = "pg15"))]
pub(crate) fn check_copy_table_permission(
    p_stmt: &PgBox<PlannedStmt>,
    p_state: &PgBox<ParseState>,
    ns_item: &PgBox<ParseNamespaceItem>,
    relation: &PgRelation,
) {
    let copy_stmt = unsafe { PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _) };

    // init rte
    let mut rte = unsafe { PgBox::<pgrx::pg_sys::RangeTblEntry>::from_pg(ns_item.p_rte) };

    // set table access mode
    rte.requiredPerms = if copy_stmt.is_from {
        ACL_INSERT as _
    } else {
        ACL_SELECT as _
    };

    // set column access modes
    let tup_desc = relation.tuple_desc();

    let attnums =
        unsafe { CopyGetAttnums(tup_desc.as_ptr(), relation.as_ptr(), copy_stmt.attlist) };
    let attnums = unsafe { PgList::<i16>::from_pg(attnums) };

    for attnum in attnums.iter_int() {
        let attno = attnum - FirstLowInvalidHeapAttributeNumber;

        if copy_stmt.is_from {
            unsafe { rte.insertedCols = bms_add_member(rte.insertedCols, attno) };
        } else {
            unsafe { rte.selectedCols = bms_add_member(rte.selectedCols, attno) };
        }
    }

    // check permissions
    unsafe { pgrx::pg_sys::ExecCheckRTPerms(p_state.p_rtable, true) };
}

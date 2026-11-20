///
/// # Custom compatibility types and functions
///
/// Each major version of Postgres may introduce internal API changes.
///
/// We're declaring here a set of replacements to avoid conditional macro
/// codeblocks in the main extension codebase.
///
use pgrx::prelude::*;
use std::os::raw::c_char;

///
/// ## rte_perminfo_index_disable
///
/// Since PG16, every top-level RTE_RELATION entry has a new plan node
/// called RTEPermissionInfo and each RTE knows the index of its
/// RTEPermissionInfo in the query's RTEPermissionInfo list.
///
/// When we replace a relation by its masking subquery, we need to disable the
/// PermissionInfo check, otherwise the Executor will detect that the
/// index of the PermissionInfo does not match provided RTE
///
/// https://github.com/postgres/postgres/commit/a61b1f74823c9c4f79c95226a461f1e7a367764b
///
#[cfg(any(feature = "pg14", feature = "pg15"))]
#[macro_export]
macro_rules! rte_perminfo_index_disable {
    ($rte: ident) => {};
}

#[cfg(not(any(feature = "pg14", feature = "pg15")))]
#[macro_export]
macro_rules! rte_perminfo_index_disable {
    ($rte: ident) => {
        $rte.perminfoindex = 0
    };
}

pub(crate) use rte_perminfo_index_disable;

///
/// SchemaValue type
///
#[cfg(not(feature = "pg14"))]
pub use pgrx::pg_sys::String as SchemaValue;

#[cfg(feature = "pg14")]
pub use pgrx::pg_sys::Value as SchemaValue;

pub use SchemaValue as ColumnValue;

///
/// strVal macro
///
#[allow(non_snake_case)]
#[cfg(not(feature = "pg14"))]
pub unsafe fn strVal(v: SchemaValue) -> *const c_char {
    v.sval
}

#[allow(non_snake_case)]
#[cfg(feature = "pg14")]
pub unsafe fn strVal(v: SchemaValue) -> *const c_char {
    unsafe { v.val.str_ }
}

///
/// IsCatalogRelationOid
/// Remove this when catalog.c is available in PGRX
///
#[allow(non_snake_case)]
pub fn IsCatalogRelationOid(relid: pg_sys::Oid) -> bool {
    u32::from(relid) < pg_sys::FirstNormalObjectId
}

///
/// parse_analyze_varparams
///
#[allow(non_snake_case)]
#[cfg(not(feature = "pg14"))]
pub unsafe fn parse_analyze_varparams(
    arg_parseTree: *mut pg_sys::RawStmt,
    arg_sourceText: *const c_char,
    arg_paramTypes: *mut *mut pg_sys::Oid,
    arg_numParams: *mut i32,
    arg_queryEnv: *mut pg_sys::QueryEnvironment,
) -> *mut pg_sys::Query {
    unsafe {
        pg_sys::parse_analyze_varparams(
            arg_parseTree,
            arg_sourceText,
            arg_paramTypes,
            arg_numParams,
            arg_queryEnv,
        )
    }
}

#[allow(non_snake_case)]
#[cfg(feature = "pg14")]
pub unsafe fn parse_analyze_varparams(
    arg_parseTree: *mut pg_sys::RawStmt,
    arg_sourceText: *const c_char,
    arg_paramTypes: *mut *mut pg_sys::Oid,
    arg_numParams: *mut i32,
    arg_queryEnv: *mut pg_sys::QueryEnvironment,
) -> *mut pg_sys::Query {
    unsafe {
        pg_sys::parse_analyze(
            arg_parseTree,
            arg_sourceText,
            *arg_paramTypes,
            *arg_numParams,
            arg_queryEnv,
        )
    }
}

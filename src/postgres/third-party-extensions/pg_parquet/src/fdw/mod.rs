use std::ffi::CStr;
use std::ptr;

use pgrx::pg_sys::{
    get_foreign_data_wrapper_oid, untransformRelOptions, AlterForeignServerStmt,
    AlterUserMappingStmt, CreateForeignServerStmt, CreateUserMappingStmt, DefElem,
    ForeignDataWrapperRelationId, ForeignServerRelationId, ForeignTableRelationId,
    ForeignTableRelidIndexId, GetForeignServer, GetForeignServerByName, InvalidOid, Node,
    UserMappingRelationId,
};
use pgrx::datum::FromDatum;
use pgrx::fcinfo::pg_getarg_datum;
use pgrx::{is_a, pg_guard, pg_sys::NodeTag, pg_sys::Oid, PgList};

const PARQUET_FDW_NAME: &CStr = c"parquet";
const S3_SERVER_TYPE: &str = "s3";
const GCS_SERVER_TYPE: &str = "gcs";
const SUPPORTED_SERVER_TYPES: &[&str] = &[S3_SERVER_TYPE, GCS_SERVER_TYPE];

const S3_SERVER_OPTIONS: &[&str] = &[
    "region",
    "endpoint",
    "scope",
    "url_style",
    "use_ssl",
    "allow_http",
];

const GCS_SERVER_OPTIONS: &[&str] = &["endpoint", "scope"];

const S3_USER_MAPPING_OPTIONS: &[&str] = &["key_id", "secret", "session_token"];

const GCS_USER_MAPPING_OPTIONS: &[&str] = &[
    "key_id",
    "secret",
    "session_token",
    "service_account_key",
    "service_account_path",
];

const SENSITIVE_SERVER_OPTIONS: &[&str] = &[
    "key_id",
    "secret",
    "session_token",
    "service_account_key",
    "service_account_path",
];

static mut CURRENT_SERVER_TYPE: *mut i8 = ptr::null_mut();
static mut CURRENT_SERVER_OID: Oid = InvalidOid;

pub(crate) fn handle_utility_statement_for_fdw(utility_stmt: *mut Node) {
    if utility_stmt.is_null() {
        return;
    }

    unsafe {
        if is_a(utility_stmt, NodeTag::T_CreateForeignServerStmt) {
            handle_create_foreign_server(utility_stmt as *mut CreateForeignServerStmt);
        } else if is_a(utility_stmt, NodeTag::T_AlterForeignServerStmt) {
            handle_alter_foreign_server(utility_stmt as *mut AlterForeignServerStmt);
        } else if is_a(utility_stmt, NodeTag::T_CreateUserMappingStmt) {
            handle_create_user_mapping(utility_stmt as *mut CreateUserMappingStmt);
        } else if is_a(utility_stmt, NodeTag::T_AlterUserMappingStmt) {
            handle_alter_user_mapping(utility_stmt as *mut AlterUserMappingStmt);
        }
    }
}

unsafe fn parquet_fdw_oid() -> Oid {
    get_foreign_data_wrapper_oid(PARQUET_FDW_NAME.as_ptr(), false)
}

unsafe fn reset_current_server_type() {
    CURRENT_SERVER_TYPE = ptr::null_mut();
}

unsafe fn reset_current_server_oid() {
    CURRENT_SERVER_OID = InvalidOid;
}

unsafe fn set_current_server_type(server_type: *mut i8) {
    reset_current_server_type();
    if server_type.is_null() {
        return;
    }
    CURRENT_SERVER_TYPE = server_type;
}

unsafe fn set_current_server_oid(server_oid: Oid) {
    CURRENT_SERVER_OID = server_oid;
}

unsafe fn handle_create_foreign_server(stmt: *mut CreateForeignServerStmt) {
    reset_current_server_type();
    if (*stmt).fdwname.is_null() {
        return;
    }
    let fdwname = CStr::from_ptr((*stmt).fdwname);
    if fdwname != PARQUET_FDW_NAME {
        return;
    }
    if (*stmt).servertype.is_null() {
        return;
    }
    set_current_server_type((*stmt).servertype);
}

unsafe fn handle_alter_foreign_server(stmt: *mut AlterForeignServerStmt) {
    reset_current_server_type();
    let server = GetForeignServerByName((*stmt).servername, false);
    if server.is_null() {
        return;
    }
    if (*server).fdwid != parquet_fdw_oid() {
        return;
    }
    if (*server).servertype.is_null() {
        return;
    }
    set_current_server_type((*server).servertype);
}

unsafe fn handle_create_user_mapping(stmt: *mut CreateUserMappingStmt) {
    reset_current_server_oid();
    let server = GetForeignServerByName((*stmt).servername, false);
    if server.is_null() {
        return;
    }
    if (*server).fdwid != parquet_fdw_oid() {
        return;
    }
    set_current_server_oid((*server).serverid);
}

unsafe fn handle_alter_user_mapping(stmt: *mut AlterUserMappingStmt) {
    reset_current_server_oid();
    let server = GetForeignServerByName((*stmt).servername, false);
    if server.is_null() {
        return;
    }
    if (*server).fdwid != parquet_fdw_oid() {
        return;
    }
    set_current_server_oid((*server).serverid);
}

fn option_name_allowed(name: &str, allowed: &[&str]) -> bool {
    allowed
        .iter()
        .any(|allowed_name| name.eq_ignore_ascii_case(allowed_name))
}

fn validate_options(options: &PgList<DefElem>, allowed: &[&str], object_name: &str) {
    for i in 0..options.len() {
        let def = options
            .get_ptr(i)
            .unwrap_or_else(|| pgrx::error!("invalid option list on parquet {object_name}"));
        let defname = unsafe { CStr::from_ptr((*def).defname) };
        let defname = defname
            .to_str()
            .unwrap_or_else(|_| pgrx::error!("invalid option name on parquet {object_name}"));
        if !option_name_allowed(defname, allowed) {
            pgrx::error!(
                "invalid option \"{}\" for parquet {}",
                defname,
                object_name
            );
        }
    }
}

fn validate_sensitive_server_options(options: &PgList<DefElem>) {
    for i in 0..options.len() {
        let def = options
            .get_ptr(i)
            .unwrap_or_else(|| pgrx::error!("invalid option list on parquet foreign server"));
        let defname = unsafe { CStr::from_ptr((*def).defname) };
        let defname = defname
            .to_str()
            .unwrap_or_else(|_| pgrx::error!("invalid option name on parquet foreign server"));
        if option_name_allowed(defname, SENSITIVE_SERVER_OPTIONS) {
            pgrx::error!(
                "option \"{}\" cannot be used in the SERVER's OPTIONS, please move it to the USER MAPPING",
                defname
            );
        }
    }
}

fn validate_supported_server_type(server_type: Option<&str>) -> String {
    let server_type = server_type.unwrap_or_else(|| {
        pgrx::error!("CREATE SERVER for parquet FDW requires TYPE 's3' or 'gcs'");
    });
    let lower = server_type.to_ascii_lowercase();
    if !SUPPORTED_SERVER_TYPES.iter().any(|t| *t == lower.as_str()) {
        pgrx::error!(
            "parquet foreign server type \"{}\" is not supported, only 's3' and 'gcs' are supported",
            server_type
        );
    }
    lower
}

fn server_options_for_type(server_type: &str) -> &'static [&'static str] {
    match server_type {
        S3_SERVER_TYPE => S3_SERVER_OPTIONS,
        GCS_SERVER_TYPE => GCS_SERVER_OPTIONS,
        _ => pgrx::error!("unsupported parquet foreign server type \"{}\"", server_type),
    }
}

fn user_mapping_options_for_type(server_type: &str) -> &'static [&'static str] {
    match server_type {
        S3_SERVER_TYPE => S3_USER_MAPPING_OPTIONS,
        GCS_SERVER_TYPE => GCS_USER_MAPPING_OPTIONS,
        _ => pgrx::error!("unsupported parquet foreign server type \"{}\"", server_type),
    }
}

fn validate_user_mapping_has_credentials(server_type: &str, options: &PgList<DefElem>) {
    let mut has_key_id = false;
    let mut has_secret = false;
    let mut has_service_account_key = false;
    let mut has_service_account_path = false;

    for i in 0..options.len() {
        let def = options
            .get_ptr(i)
            .unwrap_or_else(|| pgrx::error!("invalid option list on parquet user mapping"));
        let defname = unsafe { CStr::from_ptr((*def).defname) };
        let defname = defname
            .to_str()
            .unwrap_or_else(|_| pgrx::error!("invalid option name on parquet user mapping"));
        if defname.eq_ignore_ascii_case("key_id") {
            has_key_id = true;
        } else if defname.eq_ignore_ascii_case("secret") {
            has_secret = true;
        } else if defname.eq_ignore_ascii_case("service_account_key") {
            has_service_account_key = true;
        } else if defname.eq_ignore_ascii_case("service_account_path") {
            has_service_account_path = true;
        }
    }

    match server_type {
        S3_SERVER_TYPE => {
            if !has_key_id || !has_secret {
                pgrx::error!("parquet user mapping for S3 requires key_id and secret options");
            }
        }
        GCS_SERVER_TYPE => {
            let has_hmac = has_key_id || has_secret;
            let has_service_account = has_service_account_key || has_service_account_path;
            if has_hmac && has_service_account {
                pgrx::error!(
                    "parquet user mapping for GCS cannot mix HMAC (key_id/secret) and service-account options"
                );
            }
            if has_hmac && !(has_key_id && has_secret) {
                pgrx::error!(
                    "parquet user mapping for GCS HMAC auth requires both key_id and secret options"
                );
            }
            if has_service_account_key && has_service_account_path {
                pgrx::error!(
                    "parquet user mapping for GCS may set service_account_key or service_account_path, not both"
                );
            }
            if !has_hmac && !has_service_account {
                pgrx::error!(
                    "parquet user mapping for GCS requires either (key_id, secret) or (service_account_key or service_account_path)"
                );
            }
        }
        _ => pgrx::error!(
            "unsupported parquet foreign server type \"{}\"",
            server_type
        ),
    }
}

#[no_mangle]
#[doc(hidden)]
pub extern "C" fn pg_finfo_parquet_fdw_handler_wrapper() -> &'static pgrx::pg_sys::Pg_finfo_record {
    const V1_API: pgrx::pg_sys::Pg_finfo_record = pgrx::pg_sys::Pg_finfo_record { api_version: 1 };
    &V1_API
}

#[pg_guard]
#[no_mangle]
pub unsafe extern "C-unwind" fn parquet_fdw_handler_wrapper(
    _fcinfo: pgrx::pg_sys::FunctionCallInfo,
) -> pgrx::pg_sys::Datum {
    let routine = ptr::null_mut::<pgrx::pg_sys::FdwRoutine>();
    pgrx::pg_sys::Datum::from(routine as usize)
}

#[no_mangle]
#[doc(hidden)]
pub extern "C" fn pg_finfo_parquet_fdw_validator_wrapper() -> &'static pgrx::pg_sys::Pg_finfo_record {
    const V1_API: pgrx::pg_sys::Pg_finfo_record = pgrx::pg_sys::Pg_finfo_record { api_version: 1 };
    &V1_API
}

#[pg_guard]
#[no_mangle]
pub unsafe extern "C-unwind" fn parquet_fdw_validator_wrapper(
    fcinfo: pgrx::pg_sys::FunctionCallInfo,
) -> pgrx::pg_sys::Datum {
    let options = pg_getarg_datum(fcinfo, 0).expect("parquet FDW validator options are null");
    let catalog_datum =
        pg_getarg_datum(fcinfo, 1).expect("parquet FDW validator catalog is null");
    let catalog = Oid::from_datum(catalog_datum, false).unwrap_or(InvalidOid);
    parquet_fdw_validator(options, catalog);
    pgrx::pg_sys::Datum::from(0)
}

fn parquet_fdw_validator(options: pgrx::pg_sys::Datum, catalog: pgrx::pg_sys::Oid) {
    if catalog == ForeignTableRelationId || catalog == ForeignTableRelidIndexId.into() {
        pgrx::error!("parquet FDW only supports SERVER and USER MAPPING objects");
    }

    let options_list = unsafe { PgList::<DefElem>::from_pg(untransformRelOptions(options)) };

    if catalog == ForeignDataWrapperRelationId {
        if !options_list.is_empty() {
            let def = options_list
                .get_ptr(0)
                .expect("missing option on parquet FDW");
            let defname = unsafe { CStr::from_ptr((*def).defname) };
            let defname = defname
                .to_str()
                .unwrap_or_else(|_| pgrx::error!("invalid option name on parquet FDW"));
            pgrx::error!("parquet FDW does not take any option, found '{defname}'");
        }
        return;
    }

    if catalog == ForeignServerRelationId {
        let server_type_ptr = unsafe {
            let server_type_ptr = CURRENT_SERVER_TYPE;
            reset_current_server_type();
            server_type_ptr
        };
        let server_type = if server_type_ptr.is_null() {
            None
        } else {
            unsafe { CStr::from_ptr(server_type_ptr).to_str().ok() }
        };
        let server_type = validate_supported_server_type(server_type);
        validate_options(
            &options_list,
            server_options_for_type(server_type.as_str()),
            "foreign server",
        );
        validate_sensitive_server_options(&options_list);
        return;
    }

    if catalog == UserMappingRelationId {
        let server_oid = unsafe {
            let server_oid = CURRENT_SERVER_OID;
            reset_current_server_oid();
            server_oid
        };
        if server_oid == InvalidOid {
            pgrx::error!(
                "could not validate parquet user mapping without foreign server context"
            );
        }
        let server = unsafe { GetForeignServer(server_oid) };
        if server.is_null() {
            pgrx::error!("foreign server with oid {server_oid} not found");
        }
        let server_type = unsafe {
            if (*server).servertype.is_null() {
                None
            } else {
                CStr::from_ptr((*server).servertype).to_str().ok()
            }
        };
        let server_type = validate_supported_server_type(server_type);
        validate_options(
            &options_list,
            user_mapping_options_for_type(server_type.as_str()),
            "user mapping",
        );
        if !options_list.is_empty() {
            validate_user_mapping_has_credentials(server_type.as_str(), &options_list);
        }
        return;
    }

    pgrx::error!("unknown catalog oid {catalog} for parquet FDW validator");
}

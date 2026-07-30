// YB: Use std::ffi::CStr instead of pgrx::ffi::CString for GUC string settings,
// as the YB pgrx (patched for PG15 fork) uses &'static CStr for GucSetting<Option<...>>.
use std::ffi::CStr;

use pgrx::{GucContext, GucFlags, GucRegistry, GucSetting};

// YB: GucSetting type changed from Option<CString> to Option<&'static CStr> to match
// the YB pgrx API.
pub(crate) static PG_DOCUMENTDB_GATEWAY_DATABASE: GucSetting<Option<&'static CStr>> =
    GucSetting::<Option<&'static CStr>>::new(None);

pub(crate) static PG_DOCUMENTDB_SETUP_CONFIGURATION: GucSetting<Option<&'static CStr>> =
    GucSetting::<Option<&'static CStr>>::new(None);

pub fn init() {
    // YB: GUC name/description arguments changed from c"..." (C string literals) to
    // plain &str to match the YB pgrx API.
    GucRegistry::define_string_guc(
        "documentdb_gateway.database",
        "The database that the pg_documentdb_gateway BGWorker will connect to",
        "This should be the database that you ran `CREATE EXTENSION pg_documentdb_gateway` in",
        &PG_DOCUMENTDB_GATEWAY_DATABASE,
        GucContext::Postmaster,
        GucFlags::SUPERUSER_ONLY,
    );
    GucRegistry::define_string_guc(
        "documentdb_gateway.setup_configuration_file",
        "The setup configuration file for the pg_documentdb_gateway BGWorker",
        "This should be the path to the setup configuration file",
        &PG_DOCUMENTDB_SETUP_CONFIGURATION,
        GucContext::Postmaster,
        GucFlags::SUPERUSER_ONLY,
    );
}

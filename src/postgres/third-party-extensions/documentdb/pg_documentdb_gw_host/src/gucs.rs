use pgrx::ffi::CString;
use pgrx::{GucContext, GucFlags, GucRegistry, GucSetting};

pub(crate) static PG_DOCUMENTDB_GATEWAY_DATABASE: GucSetting<Option<CString>> =
    GucSetting::<Option<CString>>::new(None);

pub(crate) static PG_DOCUMENTDB_SETUP_CONFIGURATION: GucSetting<Option<CString>> =
    GucSetting::<Option<CString>>::new(None);

pub fn init() {
    GucRegistry::define_string_guc(
        c"documentdb_gateway.database",
        c"The database that the pg_documentdb_gateway BGWorker will connect to",
        c"This should be the database that you ran `CREATE EXTENSION pg_documentdb_gateway` in",
        &PG_DOCUMENTDB_GATEWAY_DATABASE,
        GucContext::Postmaster,
        GucFlags::SUPERUSER_ONLY,
    );
    GucRegistry::define_string_guc(
        c"documentdb_gateway.setup_configuration_file",
        c"The setup configuration file for the pg_documentdb_gateway BGWorker",
        c"This should be the path to the setup configuration file",
        &PG_DOCUMENTDB_SETUP_CONFIGURATION,
        GucContext::Postmaster,
        GucFlags::SUPERUSER_ONLY,
    );
}

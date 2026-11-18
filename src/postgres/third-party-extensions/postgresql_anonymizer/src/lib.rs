use pgrx::pgrx_macros::extension_sql_file;
use pgrx::prelude::*;
#[allow(unused_imports)]
use std::ffi::CString;

mod compat;
mod dummy;
mod error;
mod fixture;
mod guc;
mod hooks;
mod impexp;
mod input;
mod label_providers;
mod ldp;
mod log;
mod macros;
mod masking;
mod noise;
mod random;
pub mod re;
mod replica_masking;
mod rule;
mod static_masking;
mod utils;
mod walker;

// Load the SQL functions AFTER the rust functions
// GCOVR_EXCL_START
extension_sql_file!("../sql/anon.sql", name = "anon", finalize);

extension_sql_file!(
    "../sql/fake_data_tables.sql",
    name = "fake_data_tables",
    requires = ["anon"]
);
extension_sql_file!(
    "../sql/init.sql",
    name = "init",
    requires = ["fake_data_tables"]
);

extension_sql_file!("../sql/fake.sql", requires = ["init"]);
extension_sql_file!("../sql/hash.sql", requires = ["init"]);
extension_sql_file!("../sql/pseudo.sql", requires = ["init"]);

extension_sql_file!("../sql/bindings.sql", requires = ["anon"]);
extension_sql_file!("../sql/random.sql", requires = ["anon"]);
extension_sql_file!("../sql/static_masking.sql", requires = ["anon"]);
extension_sql_file!("../sql/parallel_static_masking.sql", requires = ["anon"]);
extension_sql_file!("../sql/replica_masking.sql", requires = ["anon"]);
extension_sql_file!("../sql/custom_values.sql", requires = ["anon"]);

// GCOVR_EXCL_STOP

pgrx::pg_module_magic!();

//----------------------------------------------------------------------------
// External Functions
//----------------------------------------------------------------------------

// All external functions are defined in the anon schema

#[pg_schema]
mod anon {
    use pgrx::prelude::*;

    //------------------------------------------------------------------------
    // Dummy Functions
    //------------------------------------------------------------------------
    use crate::dummy;
    use crate::impexp;
    use crate::noise;
    use fake::Fake;
    use fake::locales::*;

    // Address
    use fake::faker::address::raw::*;
    dummy::declare_l10n_fn_String!(dummy_city_prefix, CityPrefix);
    dummy::declare_l10n_fn_String!(dummy_city_suffix, CitySuffix);
    dummy::declare_l10n_fn_String!(dummy_city_name, CityName);
    dummy::declare_l10n_fn_String!(dummy_country_name, CountryName);
    dummy::declare_l10n_fn_String!(dummy_country_code, CountryCode);
    dummy::declare_l10n_fn_String!(dummy_street_suffix, StreetSuffix);
    dummy::declare_l10n_fn_String!(dummy_street_name, StreetName);
    dummy::declare_l10n_fn_String!(dummy_timezone, TimeZone);
    dummy::declare_l10n_fn_String!(dummy_state_name, StateName);
    dummy::declare_l10n_fn_String!(dummy_state_abbr, StateAbbr);
    dummy::declare_l10n_fn_String!(dummy_secondary_address_type, SecondaryAddressType);
    dummy::declare_l10n_fn_String!(dummy_secondary_address, SecondaryAddress);
    dummy::declare_l10n_fn_String!(dummy_zip_code, ZipCode);
    dummy::declare_l10n_fn_String!(dummy_post_code, PostCode);
    dummy::declare_l10n_fn_String!(dummy_building_number, BuildingNumber);
    dummy::declare_l10n_fn_String!(dummy_latitude, Latitude);
    dummy::declare_l10n_fn_String!(dummy_longitude, Longitude);
    //dummy::declare_l10n_fn_String!(dummy_Geohash(precision: u8);

    // Administrative
    use fake::faker::administrative::raw::*;
    dummy::declare_french_fn_String!(dummy_health_insurance_code, HealthInsuranceCode);

    // Automotive
    use fake::faker::automotive::raw::*;
    dummy::declare_french_fn_String!(dummy_licence_plate, LicencePlate);

    // Barcode
    use fake::faker::barcode::raw::*;
    dummy::declare_l10n_fn_String!(dummy_isbn, Isbn);
    dummy::declare_l10n_fn_String!(dummy_isbn13, Isbn13);

    // Color
    use fake::faker::color::raw::*;
    dummy::declare_l10n_fn_String!(dummy_hex_color, HexColor);
    dummy::declare_l10n_fn_String!(dummy_rgb_color, RgbColor);
    dummy::declare_l10n_fn_String!(dummy_rgba_color, RgbaColor);
    dummy::declare_l10n_fn_String!(dummy_hsl_color, HslColor);
    dummy::declare_l10n_fn_String!(dummy_hsla_color, HslaColor);
    dummy::declare_l10n_fn_String!(dummy_color, Color);

    // Company
    use fake::faker::company::raw::*;
    dummy::declare_l10n_fn_String!(dummy_company_suffix, CompanySuffix);
    dummy::declare_l10n_fn_String!(dummy_company_name, CompanyName);
    dummy::declare_l10n_fn_String!(dummy_buzzword, Buzzword);
    dummy::declare_l10n_fn_String!(dummy_buzzword_middle, BuzzwordMiddle);
    dummy::declare_l10n_fn_String!(dummy_buzzword_tail, BuzzwordTail);
    dummy::declare_l10n_fn_String!(dummy_catchphrase, CatchPhrase);
    dummy::declare_l10n_fn_String!(dummy_bs_verb, BsVerb);
    dummy::declare_l10n_fn_String!(dummy_bs_adj, BsAdj);
    dummy::declare_l10n_fn_String!(dummy_bs_noun, BsNoun);
    dummy::declare_l10n_fn_String!(dummy_bs, Bs);
    dummy::declare_l10n_fn_String!(dummy_profession, Profession);
    dummy::declare_l10n_fn_String!(dummy_industry, Industry);

    // Creditcard
    use fake::faker::creditcard::raw::*;
    dummy::declare_l10n_fn_String!(dummy_credit_card_number, CreditCardNumber);

    // Currency
    use fake::faker::currency::raw::*;
    dummy::declare_l10n_fn_String!(dummy_currency_code, CurrencyCode);
    dummy::declare_l10n_fn_String!(dummy_currency_name, CurrencyName);
    dummy::declare_l10n_fn_String!(dummy_currency_symbol, CurrencySymbol);

    // Filesystem
    use fake::faker::filesystem::raw::*;
    dummy::declare_l10n_fn_String!(dummy_file_path, FilePath);
    dummy::declare_l10n_fn_String!(dummy_file_name, FileName);
    dummy::declare_l10n_fn_String!(dummy_file_extension, FileExtension);
    dummy::declare_l10n_fn_String!(dummy_dir_path, DirPath);

    // Finance
    use fake::faker::finance::raw::*;
    dummy::declare_l10n_fn_String!(dummy_bic, Bic);
    dummy::declare_l10n_fn_String!(dummy_isin, Isin);

    // HTTP
    use fake::faker::http::raw::*;
    dummy::declare_l10n_fn_String!(dummy_rfc_status_code, RfcStatusCode);
    dummy::declare_l10n_fn_String!(dummy_valid_statux_code, ValidStatusCode);

    // Internet
    use fake::faker::internet::raw::*;
    dummy::declare_l10n_fn_String!(dummy_free_email_provider, FreeEmailProvider);
    dummy::declare_l10n_fn_String!(dummy_domain_suffix, DomainSuffix);
    dummy::declare_l10n_fn_String!(dummy_free_email, FreeEmail);
    dummy::declare_l10n_fn_String!(dummy_safe_email, SafeEmail);
    dummy::declare_l10n_fn_String!(dummy_username, Username);
    //dummy::declare_l10n_fn_with_range_to_string!(dummy_password,Password);
    //dummy::declare_l10n_fn_String!(dummy_Password(len_range: Range<usize>);
    dummy::declare_l10n_fn_String!(dummy_ipv4, IPv4);
    dummy::declare_l10n_fn_String!(dummy_ipv6, IPv6);
    dummy::declare_l10n_fn_String!(dummy_ip, IP);
    dummy::declare_l10n_fn_String!(dummy_mac_address, MACAddress);
    dummy::declare_l10n_fn_String!(dummy_user_agent, UserAgent);

    // Lorem
    use fake::faker::lorem::raw::*;
    dummy::declare_l10n_fn_String!(dummy_word, Word);
    dummy::declare_l10n_fn_with_range_to_string!(dummy_words, Words);
    //dummy::declare_l10n_fn_with_range_to_string!(dummy_sentence,Sentence);
    //dummy::declare_l10n_fn_with_range_to_string!(dummy_sentences,Sentences);

    // Person
    use fake::faker::name::raw::*;
    dummy::declare_l10n_fn_String!(dummy_first_name, FirstName);
    dummy::declare_l10n_fn_String!(dummy_last_name, LastName);
    dummy::declare_l10n_fn_String!(dummy_title, Title);
    dummy::declare_l10n_fn_String!(dummy_suffix, Suffix);
    dummy::declare_l10n_fn_String!(dummy_name, Name);
    dummy::declare_l10n_fn_String!(dummy_name_with_title, NameWithTitle);

    // Phone Number
    use fake::faker::phone_number::raw::*;
    dummy::declare_l10n_fn_String!(dummy_phone_number, PhoneNumber);
    dummy::declare_l10n_fn_String!(dummy_cell_number, CellNumber);

    // UUID
    use fake::uuid::*;
    dummy::declare_fn_String!(dummy_uuidv1, UUIDv1);
    dummy::declare_fn_String!(dummy_uuidv3, UUIDv3);
    dummy::declare_fn_String!(dummy_uuidv4, UUIDv4);
    dummy::declare_fn_String!(dummy_uuidv5, UUIDv5);

    //------------------------------------------------------------------------
    // Random Functions
    //------------------------------------------------------------------------
    use crate::random;

    // Time

    // Random functions must be marked PARALLEL RESTRICTED because they access
    // a backend-local state that the system cannot synchronize across workers.
    // https://www.postgresql.org/docs/current/parallel-safety.html#PARALLEL-LABELING

    #[pg_extern(parallel_restricted)]
    pub fn random_time() -> pgrx::datum::Time {
        random::time()
    }

    #[pg_extern(parallel_restricted)]
    pub fn random_date() -> pgrx::datum::TimestampWithTimeZone {
        random::date()
    }

    // Random Numbers

    // BIGINT

    #[pg_extern(parallel_restricted)]
    pub fn random_in_int8range(r: Range<i64>) -> Option<i64> {
        random::bigint(r)
    }

    // +1 because the stop parameter is inclusive
    // but the range upper bound is exclusive
    #[pg_extern(parallel_restricted)]
    pub fn random_bigint_between(start: i64, stop: i64) -> Option<i64> {
        random::bigint(Range::<i64>::new(start, stop + 1))
    }

    // INT
    #[pg_extern(parallel_restricted)]
    pub fn random_in_int4range(r: Range<i32>) -> Option<i32> {
        random::int(r)
    }

    // +1 because the stop parameter is inclusive
    // but the range upper bound is exclusive
    #[pg_extern(parallel_restricted)]
    pub fn random_int_between(start: i32, stop: i32) -> Option<i32> {
        random::int(Range::<i32>::new(start, stop + 1))
    }

    #[pg_extern(parallel_restricted)]
    pub fn random_number_with_format(format: String) -> String {
        random::number_with_format(format)
    }

    // FLOATS

    #[pg_extern(parallel_restricted)]
    pub fn random_double_precision(start: f64, stop: f64) -> Option<f64> {
        let range = Range::<AnyNumeric>::new(
            AnyNumeric::try_from(start).unwrap(),
            AnyNumeric::try_from(stop).unwrap(),
        );
        random::double_precision(range)
    }

    #[pg_extern(parallel_restricted)]
    pub fn random_in_numrange(r: Range<AnyNumeric>) -> Option<AnyNumeric> {
        random::numeric(r)
    }

    #[pg_extern(parallel_restricted)]
    pub fn random_real(start: f32, stop: f32) -> Option<f32> {
        let range = Range::<AnyNumeric>::new(
            AnyNumeric::try_from(start).unwrap(),
            AnyNumeric::try_from(stop).unwrap(),
        );
        random::real(range)
    }

    // PHONE
    #[pg_extern(parallel_restricted)]
    pub fn random_phone() -> String {
        random::number_with_format("0#########".to_string())
    }

    #[pg_extern(parallel_restricted)]
    pub fn random_phone_with_format(format: String) -> String {
        random::number_with_format(format)
    }

    #[pg_extern(parallel_restricted)]
    pub fn random_zip() -> String {
        random::number_with_format("#####".to_string())
    }

    // Strings

    #[pg_extern(parallel_restricted)]
    pub fn random_string(r: Range<i32>) -> Option<String> {
        random::string(r)
    }

    // GPS coordinates

    #[pg_extern()]
    pub fn noisy_gps_coordinates(
        origin: pgrx::pg_sys::Point,
        radius: default!(f64, "10.0"),
        is_metric: default!(bool, "true"),
    ) -> pgrx::pg_sys::Point {
        noise::noisy_gps_coordinates(origin, radius, is_metric)
    }

    #[pg_extern()]
    pub fn random_gps_coordinates() -> pgrx::pg_sys::Point {
        random::random_gps_coordinates()
    }

    // Points

    #[pg_extern]
    pub fn random_point_in_box(rect: pgrx::pg_sys::BOX) -> pgrx::pg_sys::Point {
        random::random_point_in_box(rect)
    }

    //------------------------------------------------------------------------
    // Masking engine
    //------------------------------------------------------------------------
    use crate::masking;

    #[pg_extern]
    pub fn masking_value_for_column(r: pg_sys::Oid, c: i32, p: String) -> Option<String> {
        let (val, _) = masking::masking_value_for_column(r, c, p)?;
        Some(val)
    }

    //
    // The masking engine functions are used by the V1 dynamic masking engine
    // They are exposed for backward compat' and may be made private in
    // further versions
    // Anyway they should not be used as masking filters !
    //
    extension_sql!(
        r#"
    SECURITY LABEL FOR anon ON FUNCTION anon.masking_value_for_column IS 'UNTRUSTED';
    "#,
        name = "unstrust_masking_engine_functions",
        requires = ["anon"]
    );

    //------------------------------------------------------------------------
    // Replica Masking
    //------------------------------------------------------------------------
    use crate::replica_masking;

    #[pg_extern]
    pub fn drop_replica_trigger_for_table(r: pg_sys::Oid) -> Option<bool> {
        replica_masking::drop_replica_trigger_for_table(r)
    }

    #[pg_extern]
    pub fn refresh_replica_trigger_for_table(r: pg_sys::Oid, p: String) -> Option<bool> {
        replica_masking::refresh_replica_trigger_for_table(r, p)
    }

    //
    // The replica masking functions should not be used as masking filters
    //
    extension_sql!(
        r#"
    SECURITY LABEL FOR anon ON FUNCTION anon.drop_replica_trigger_for_table(OID) IS 'UNTRUSTED';
    SECURITY LABEL FOR anon ON FUNCTION anon.refresh_replica_trigger_for_table(OID,TEXT) IS 'UNTRUSTED';
    "#,
        name = "unstrust_replica_masking_functions",
        requires = ["anon"]
    );

    //------------------------------------------------------------------------
    // Masking engine
    //------------------------------------------------------------------------
    use crate::static_masking;

    //
    // Here way need to declare manually the SQL mapping function for 2 reasons:
    //
    // 1. The PGRX does not handle default value for parameter in the SQL
    //    functions. To maintain backward compat' when need to expose functions
    //    without the `policy` parameter, where the default policy will be
    //    "anon". We currently do that with multiple function signatures
    //
    // 2. Currently (july 2024), PGRX does not support the REGCLASS type
    //    https://github.com/pgcentralfoundation/pgrx/issues/1773
    //
    // This is why we declare the SQL mapping functions
    //

    #[pg_extern(sql = "
        CREATE FUNCTION anon.anonymize_column(tablename OID, colname TEXT, policy TEXT)
        RETURNS BOOLEAN
        AS 'MODULE_PATHNAME', 'anonymize_column_wrapper'
        LANGUAGE C STRICT;

        CREATE FUNCTION anon.anonymize_column(tablename TEXT, colname TEXT, policy TEXT)
        RETURNS BOOLEAN
        AS $$ SELECT anon.anonymize_column(tablename::REGCLASS::OID, colname, policy); $$
        LANGUAGE SQL STRICT;

        CREATE FUNCTION anon.anonymize_column(tablename TEXT, colname NAME, policy TEXT)
        RETURNS BOOLEAN
        AS $$ SELECT anon.anonymize_column(tablename::REGCLASS::OID, colname::TEXT, policy); $$
        LANGUAGE SQL STRICT;

        CREATE FUNCTION anon.anonymize_column(tablename TEXT, colname NAME)
        RETURNS BOOLEAN
        AS $$ SELECT anon.anonymize_column(tablename::REGCLASS::OID, colname::TEXT, 'anon'::TEXT); $$
        LANGUAGE SQL STRICT;
    ")]

    pub fn anonymize_column(r: pg_sys::Oid, c: String, p: String) -> Option<bool> {
        static_masking::anonymize_column(r, c, p)
    }

    #[pg_extern(sql = "
        CREATE FUNCTION anon.anonymize_table(tablename OID, policy TEXT)
        RETURNS BOOLEAN
        AS 'MODULE_PATHNAME', 'anonymize_table_wrapper'
        LANGUAGE C STRICT;

        CREATE FUNCTION anon.anonymize_table(tablename TEXT, policy TEXT)
        RETURNS BOOLEAN
        AS $$ SELECT anon.anonymize_table(tablename::REGCLASS::OID, policy); $$
        LANGUAGE SQL STRICT;

        CREATE FUNCTION anon.anonymize_table(tablename REGCLASS)
        RETURNS BOOLEAN
        AS $$ SELECT anon.anonymize_table(tablename::OID, 'anon'); $$
        LANGUAGE SQL STRICT;

        CREATE FUNCTION anon.anonymize_table(tablename TEXT)
        RETURNS BOOLEAN
        AS $$ SELECT anon.anonymize_table(tablename::REGCLASS::OID, 'anon'); $$
        LANGUAGE SQL STRICT;
    ")]
    pub fn anonymize_table(r: pg_sys::Oid, p: String) -> Option<bool> {
        static_masking::anonymize_table(r, p)
    }

    //
    // The static masking should not be used as masking filters, otherwise
    // it would create infinite loops !
    //
    extension_sql!(
        r#"
    SECURITY LABEL FOR anon ON FUNCTION anon.anonymize_column(TEXT,NAME) IS 'UNTRUSTED';
    SECURITY LABEL FOR anon ON FUNCTION anon.anonymize_table(TEXT) IS 'UNTRUSTED';
    "#,
        name = "unstrust_static_masking_functions",
        requires = ["anon"]
    );

    //------------------------------------------------------------------------
    // Utils
    //------------------------------------------------------------------------
    use crate::utils;

    /// Used by the V1 masking engine
    #[pg_extern]
    pub fn get_function_schema(f: String) -> String {
        utils::get_function_schema(f)
    }

    #[pg_extern]
    pub fn list_masking_policies() -> Vec<String> {
        masking::list_masking_policies()
    }

    #[pg_extern]
    pub fn export_current_database_rules(provider: default!(String, "'anon'")) -> pgrx::JsonB {
        impexp::export_current_database_rules(provider.as_ref())
    }

    #[pg_extern]
    pub fn export_roles_rules(provider: default!(String, "'anon'")) -> pgrx::JsonB {
        impexp::export_role_rules(provider.as_ref())
    }

    #[pg_extern(volatile, parallel_unsafe)]
    pub fn import_roles_rules(role_rules: pgrx::JsonB, provider: default!(String, "'anon'")) {
        impexp::import_role_rules(role_rules, provider.as_ref())
    }

    #[pg_extern(volatile, parallel_unsafe)]
    pub fn import_database_rules(
        database_rules: pgrx::JsonB,
        provider: default!(String, "'anon'"),
    ) {
        impexp::import_database_rules(database_rules, provider.as_ref())
    }

    //------------------------------------------------------------------------
    // DEBUG
    //
    // The functions below are not useful for the end user.
    // However they can provide priceless information when we need to fix a bug
    // or develop a new feature.
    // All functions below are NOT exposed when the extension is compiled
    // with `cargo pgrx run --release`
    //
    //------------------------------------------------------------------------
    #[cfg(debug_assertions)]
    #[pg_extern]
    pub fn get_masking_policy(roleid: pg_sys::Oid) -> Option<String> {
        masking::get_masking_policy(roleid)
    }

    #[pg_extern(sql = "
        CREATE FUNCTION anon.image_blur(data BYTEA)
        RETURNS BYTEA
        AS 'MODULE_PATHNAME', 'image_blur_without_sigma_wrapper'
        LANGUAGE C STRICT;
    ")]
    pub fn image_blur_without_sigma(data: Vec<u8>) -> Vec<u8> {
        image_blur(data, 10.0)
    }

    use image::guess_format;
    #[pg_extern]
    pub fn image_blur(data: Vec<u8>, sigma: f32) -> Vec<u8> {
        let format = guess_format(&data).expect("Failed to guess image format");
        let img = image::load_from_memory_with_format(&data, format).expect("Failed to load image");
        let mut output = std::io::Cursor::new(Vec::new());
        img.blur(sigma)
            .write_to(&mut output, format)
            .expect("Failed to write image");
        output.into_inner()
    }

    use pgrx::bgworkers::*;
    const MAX_BG_WORKERS: i32 = 4;
    use pgrx::pg_sys::MyProcPid;
    #[pg_extern(sql = "
        CREATE FUNCTION anon.anonymize_database_parallel(jobs INTEGER)
        RETURNS BOOLEAN
        AS 'MODULE_PATHNAME', 'anonymize_database_parallel_wrapper'
        LANGUAGE C STRICT;
    ")]
    pub fn anonymize_database_parallel(jobs: i32) -> Option<bool> {
        let nworkers = jobs.clamp(1, MAX_BG_WORKERS);
        let notify_pid = unsafe { MyProcPid };
        let current_db = Spi::get_one::<String>("SELECT current_database()::TEXT")
            .expect("Failed to get current database name");

        let mut workers = Vec::new(); // Store background workers
        let oids = static_masking::get_masked_table_oids().unwrap();
        for _group_idx in 0..nworkers {
            //let serialized_tables = serde_json::to_string(group).expect("Failed to serialize tables");

            let table_list = static_masking::dispatch_tables_oid(nworkers, _group_idx + 1, &oids)
                .expect("Failed to get split table groups");
            //info!("Table list for group {}/{}: {}", _group_idx + 1, nworkers,table_list.as_deref().unwrap_or("None"));
            if let Some(table_list) = table_list {
                let worker_name = format!("worker_{}", _group_idx);
                let worker_data = format!("{}|{}", current_db.as_deref().unwrap(), table_list);
                let _worker = BackgroundWorkerBuilder::new(&worker_name)
                    .set_function("bgw_anon")
                    .set_library("anon")
                    .set_notify_pid(notify_pid)
                    .set_argument(Some(notify_pid.into()))
                    .set_extra(&worker_data)
                    .enable_shmem_access(None)
                    .enable_spi_access()
                    .load_dynamic();
                let worker: DynamicBackgroundWorker = _worker.expect("Failed to load bg worker");
                worker
                    .wait_for_startup()
                    .expect("Failed to wait for startup");
                workers.push(worker);
            }
        }

        // Wait for all workers to complete and handle their results
        for (idx, worker) in workers.into_iter().enumerate() {
            let worker_name = format!("worker_{}", idx);

            // Get the PID before waiting (while worker is still running)
            let worker_pid = match worker.pid() {
                Ok(pid) => {
                    debug1!("Background worker {} has PID: {}", worker_name, pid);
                    Some(pid)
                }
                Err(_e) => {
                    debug1!("Could not get PID for worker {}", worker_name);
                    None
                }
            };

            // Wait for the worker to finish and handle the result
            match worker.wait_for_shutdown() {
                Ok(()) => {
                    debug1!(
                        "Background worker {} (PID: {:?}) completed successfully",
                        worker_name,
                        worker_pid
                    );
                }
                Err(_e) => {
                    debug1!(
                        "Background worker {} (PID: {:?}) failed",
                        worker_name,
                        worker_pid
                    );
                }
            }
        }
        Some(true)
    }
}

//----------------------------------------------------------------------------
// Initialization
//----------------------------------------------------------------------------

const ANON: &core::ffi::CStr = c"anon";

/// _PG_init() is called when the module is loaded, not when the extension
/// is created. There is presently no way to unload a loaded module.
///
/// # Safety
///
/// The `#[pg_guard]` macro ensures that Rust `panic!()` and Postgres
/// `elog(ERROR)` are properly handled by PGRX. So even if the `extern 'C'
/// functions are declared `unsafe`, they are actually "less unsafe"  than some
/// C functions because of this guard.
///
#[pg_guard]
pub unsafe extern "C-unwind" fn _PG_init() {
    #[allow(static_mut_refs, deprecated)]
    unsafe {
        hooks::register_hooks();
    }
    guc::register_gucs();
    label_providers::register_label_providers();
    log::debug1!("Anon: extension initialized");
}

//----------------------------------------------------------------------------
// Unit tests
//----------------------------------------------------------------------------

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::anon::*;
    use crate::fixture;
    use pgrx::prelude::*;

    #[pg_test]
    #[ignore]
    fn test_pg_init() {
        // not sure how to handle the _PG_init() tests at this level
        // This function is tested via `make installcheck`
    }

    #[pg_test]
    fn test_random_time() {
        assert!(random_time().into_datum().is_some());
    }

    #[pg_test]
    fn test_random_date() {
        assert!(random_date().into_datum().is_some());
    }

    #[pg_test]
    fn test_random_in_int8range() {
        let range = Range::<i64>::new(1, 10);
        assert!(random_in_int8range(range).is_some());
    }

    #[pg_test]
    fn test_random_bigint_between() {
        assert!(random_bigint_between(1, 10).is_some());
    }

    #[pg_test]
    fn test_random_in_int4range() {
        let range = Range::<i32>::new(1, 10);
        assert!(random_in_int4range(range).is_some());
    }

    #[pg_test]
    fn test_random_int_between() {
        assert!(random_int_between(1, 10).is_some());
    }

    #[pg_test]
    fn test_random_number_with_format() {
        let format = "###-###".to_string();
        assert!(random_number_with_format(format).into_datum().is_some());
    }

    #[pg_test]
    fn test_random_double_precision() {
        assert!(random_double_precision(111.1, 999.9).unwrap() > 0.0);
        assert!(random_double_precision(111.1, 999.9).unwrap() < 1000.0);
    }

    #[pg_test]
    fn test_random_in_numrange() {
        let range = Range::<AnyNumeric>::new(AnyNumeric::from(1), AnyNumeric::from(10));
        assert!(random_in_numrange(range).is_some());
    }

    #[pg_test]
    fn test_random_real() {
        assert!(random_real(1.1, 10.333).unwrap() > 0.0);
        assert!(random_real(1.1, 10.333).unwrap() < 11.0);
    }

    #[pg_test]
    fn test_random_phone() {
        assert!(random_phone().into_datum().is_some());
    }

    #[pg_test]
    fn test_random_phone_with_format() {
        let format = "###-###-####".to_string();
        assert!(random_phone_with_format(format).into_datum().is_some());
    }

    #[pg_test]
    fn test_random_zip() {
        assert!(random_zip().into_datum().is_some());
    }

    #[pg_test]
    fn test_random_string() {
        let range = Range::<i32>::new(5, 10);
        assert!(random_string(range).is_some());
    }

    #[pg_test]
    fn test_anon_masking_value_for_column() {
        let oid = fixture::create_table_person();
        // dropped column
        assert_eq!(masking_value_for_column(oid, 1, "anon".into()), None);
        // column without a mask
        assert_eq!(
            masking_value_for_column(oid, 2, "anon".into()),
            Some("firstname".into())
        );
        assert_eq!(
            masking_value_for_column(oid, 2, "does_not_exist".into()),
            Some("firstname".into())
        );
        // masked_column
        assert_eq!(
            masking_value_for_column(oid, 3, "anon".into()),
            Some("CAST(NULL AS text)".into())
        );
        assert_eq!(
            masking_value_for_column(oid, 3, "does_not_exist".into()),
            Some("lastname".into())
        );
    }

    #[pg_test(error = "could not open relation with OID 0")]
    fn test_anon_masking_value_for_column_invalid_oid() {
        masking_value_for_column(pg_sys::InvalidOid, 2, "anon".into());
    }

    #[pg_test]
    fn test_anon_anonymize_table() {
        let oid = fixture::create_table_person();
        assert_eq!(anonymize_table(oid, "anon".into()), Some(true));
        assert_eq!(anonymize_table(oid, "does_not_exist".into()), None);
    }

    #[pg_test]
    fn test_anon_anonymize_table_invalid_oid() {
        assert_eq!(anonymize_table(pg_sys::InvalidOid, "anon".into()), None);
    }

    #[pg_test]
    fn test_anon_anonymize_column() {
        let oid = fixture::create_table_person();
        assert!(anonymize_column(oid, "lastname".into(), "anon".into()).unwrap());
        assert!(!anonymize_column(oid, "lastname".into(), "does_not_exist".into()).unwrap());
    }

    #[pg_test]
    fn test_anon_anonymize_column_invalid_oid() {
        assert_eq!(
            anonymize_column(pg_sys::InvalidOid, "lastname".into(), "anon".into()),
            None
        );
    }

    #[pg_test]
    fn test_anon_get_function_schema() {
        assert_eq!(
            get_function_schema("pg_catalog.now()".into()),
            "pg_catalog".to_string()
        );

        assert_eq!(get_function_schema("now()".into()), "".to_string());
    }

    #[pg_test]
    fn test_image_blur() {
        let mut file = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        file.push("tests/sql/barcode1.jpg");
        let data = std::fs::read(file).unwrap();
        image_blur(data.clone(), 99.9);
        image_blur_without_sigma(data.clone());
    }
}

/// This module is required by `cargo pgrx test` invocations.
/// It must be visible at the root of your extension crate.
#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec![]
    }
}

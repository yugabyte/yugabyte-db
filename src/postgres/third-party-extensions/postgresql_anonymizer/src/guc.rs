//----------------------------------------------------------------------------
// GUC Variables
//----------------------------------------------------------------------------

use pgrx::*;
use std::ffi::CString;

pub static ANON_CUSTOM_VALUES: GucSetting<Option<CString>> =
    GucSetting::<Option<CString>>::new(None);

pub static ANON_DUMMY_LOCALE: GucSetting<Option<CString>> =
    GucSetting::<Option<CString>>::new(Some(c"en_US"));

pub static ANON_K_ANONYMITY_PROVIDER: GucSetting<Option<CString>> =
    GucSetting::<Option<CString>>::new(Some(c"k_anonymity"));

pub static ANON_MASKING_POLICIES: GucSetting<Option<CString>> =
    GucSetting::<Option<CString>>::new(None);

pub static ANON_PRIVACY_BY_DEFAULT: GucSetting<bool> = GucSetting::<bool>::new(false);

pub static ANON_REPLICA_MASKING: GucSetting<bool> = GucSetting::<bool>::new(false);

pub static ANON_RESTRICT_TO_TRUSTED_SCHEMAS: GucSetting<bool> = GucSetting::<bool>::new(true);

pub static ANON_STRICT_MODE: GucSetting<bool> = GucSetting::<bool>::new(true);

pub static ANON_TRANSPARENT_DYNAMIC_MASKING: GucSetting<bool> = GucSetting::<bool>::new(false);

pub static ANON_STATIC_MASKING: GucSetting<bool> = GucSetting::<bool>::new(true);

pub static ANON_MAX_BG_WORKERS: GucSetting<i32> = GucSetting::<i32>::new(4);

// The GUC vars below are not used in the Rust code
// but they are used in the plpgsql code

static ANON_ALGORITHM: GucSetting<Option<CString>> =
    GucSetting::<Option<CString>>::new(Some(c"sha256"));

static ANON_SALT: GucSetting<Option<CString>> = GucSetting::<Option<CString>>::new(None);

static ANON_SHIFT: GucSetting<i32> = GucSetting::<i32>::new(0);

// Register the GUC parameters for the extension
//
pub fn register_gucs() {
    GucRegistry::define_string_guc(
        c"anon.custom_values",
        c"a JSON object containing custom values for the anonymization policy",
        c"",
        &ANON_CUSTOM_VALUES,
        GucContext::Suset,
        GucFlags::SUPERUSER_ONLY,
    );

    GucRegistry::define_string_guc(
        c"anon.dummy_locale",
        c"The default locale for the dummy data functions",
        c"",
        &ANON_DUMMY_LOCALE,
        GucContext::Suset,
        GucFlags::SUPERUSER_ONLY,
    );

    GucRegistry::define_string_guc(
        c"anon.k_anonymity_provider",
        c"The security label provider used for k-anonymity",
        c"",
        &ANON_K_ANONYMITY_PROVIDER,
        GucContext::Suset,
        GucFlags::SUPERUSER_ONLY,
    );

    //
    // As of PGRX 0.12, GUC_LIST_INPUT is not supported which means this
    // parameter can't be properly handled by `SHOW anon.masking_policies` or
    // in the pg_settings catalog. And SplitGUCList has a really weird
    // behaviour with `anon.masking_policies` ¯\_(ツ)_/¯
    //
    // https://github.com/pgcentralfoundation/pgrx/commit/d096efe6fb2d86e87d117b520b9ccd2f90b2e0d1
    //
    GucRegistry::define_string_guc(
        c"anon.masking_policies",
        c"Define additional masking policies (the 'anon' policy is already defined)",
        c"",
        &ANON_MASKING_POLICIES,
        GucContext::Suset,
        GucFlags::SUPERUSER_ONLY, /* | GucFlags::LIST_INPUT */
    );

    GucRegistry::define_bool_guc(
        c"anon.privacy_by_default",
        c"Mask all columns with NULL (or the default value for NOT NULL columns)",
        c"",
        &ANON_PRIVACY_BY_DEFAULT,
        GucContext::Suset,
        GucFlags::default(),
    );
    GucRegistry::define_bool_guc(
        c"anon.transparent_dynamic_masking",
        c"New masking engine (EXPERIMENTAL)",
        c"",
        &ANON_TRANSPARENT_DYNAMIC_MASKING,
        GucContext::Suset,
        GucFlags::default(),
    );

    GucRegistry::define_bool_guc(
        c"anon.static_masking",
        c"Static Masking engine",
        c"",
        &ANON_STATIC_MASKING,
        GucContext::Suset,
        GucFlags::default(),
    );

    GucRegistry::define_bool_guc(
        c"anon.replica_masking",
        c"Masking a logical replica (EXPERIMENTAL)",
        c"",
        &ANON_REPLICA_MASKING,
        GucContext::Suset,
        GucFlags::default(),
    );

    GucRegistry::define_bool_guc(
        c"anon.restrict_to_trusted_schemas",
        c"Masking filters must be in a trusted schema",
        c"Activate this option to prevent non-superuser from using their own masking filters",
        &ANON_RESTRICT_TO_TRUSTED_SCHEMAS,
        GucContext::Suset,
        GucFlags::SUPERUSER_ONLY,
    );

    GucRegistry::define_bool_guc(
        c"anon.strict_mode",
        c"A masking rule cannot change a column data type, unless you disable this",
        c"Disabling the mode is not recommended",
        &ANON_STRICT_MODE,
        GucContext::Suset,
        GucFlags::default(),
    );

    // The GUC vars below are not used in the Rust code
    // but they are used in the plpgsql code

    GucRegistry::define_string_guc(
        c"anon.algorithm",
        c"The hash method used for pseudonymizing functions",
        c"",
        &ANON_ALGORITHM,
        GucContext::Suset,
        GucFlags::SUPERUSER_ONLY,
    );

    GucRegistry::define_string_guc(
        c"anon.salt",
        c"The salt value used for the pseudonymizing functions",
        c"",
        &ANON_SALT,
        GucContext::Suset,
        GucFlags::SUPERUSER_ONLY,
    );

    GucRegistry::define_int_guc(
        c"anon.shift",
        c"The random distance value used in some pseudonymizing functions",
        c"",
        &ANON_SHIFT,
        0,
        2147483647,
        GucContext::Suset,
        GucFlags::SUPERUSER_ONLY,
    );

    GucRegistry::define_int_guc(
        c"anon.max_bg_workers",
        c"Maximum number of background workers for parallel static masking",
        c"",
        &ANON_MAX_BG_WORKERS,
        1,
        64, // reasonable max
        GucContext::Suset,
        GucFlags::empty(),
    );
}

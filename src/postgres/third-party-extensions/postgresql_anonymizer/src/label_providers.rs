///
/// # Security Label Providers
///
/// The *_relabel functions are called every time a security label is declared
///
use crate::error;
use crate::guc;
use crate::input;
use crate::log;
use crate::masking;
use crate::re;
use pgrx::prelude::*;
use std::ffi::CStr;
use std::ffi::CString;
use std::os::raw::c_char;

///
/// The default masking policy is named "anon".
/// It cannot be renamed or removed
///
pub static ANON_DEFAULT_MASKING_POLICY: &str = "anon";

pub fn register_label_providers() {
    // Register the security label provider for k-anonymity
    log::debug1!("Anon: registering k_anonymity provider");
    unsafe {
        pg_sys::register_label_provider(
            guc::ANON_K_ANONYMITY_PROVIDER
                .get()
                .unwrap_or(c"k_anonymity".into())
                .to_bytes_with_nul()
                .as_ptr() as *const c_char,
            Some(k_anonymity_object_relabel),
        )
    };

    // Register the default masking policy and the user-defined masking policies
    for policy in masking::list_masking_policies() {
        let policy_cstring: CString = CString::new(policy.clone()).unwrap();
        let policy_ptr: *const c_char = policy_cstring.as_ptr();
        unsafe {
            log::debug1!("Anon: registering masking policy '{}'", policy.clone());
            pg_sys::register_label_provider(policy_ptr, Some(masking_policy_object_relabel));
        }
    }
}

/// Checking the syntax of a k-anonymity rules
///
#[pg_guard]
unsafe extern "C-unwind" fn k_anonymity_object_relabel(
    object_ptr: *const pg_sys::ObjectAddress,
    seclabel_ptr: *const c_char,
) {
    /* SECURITY LABEL FOR k_anonymity ON COLUMN client.zipcode IS NULL */
    if seclabel_ptr.is_null() {
        return;
    }

    // Transform the object C pointer into a smart pointer
    let object = unsafe {
        PgBox::<pg_sys::ObjectAddress>::from_pg(object_ptr as *mut pg_sys::ObjectAddress)
    };

    /*
     * SECURITY LABEL FOR k_anonymity ON COLUMN client.zipcode IS 'INDIRECT IDENTIFIER';
     * SECURITY LABEL FOR k_anonymity ON COLUMN client.zipcode IS 'QUASI IDENTIFIER';
     */
    if object.classId == pg_sys::RelationRelationId {
        let label_cstr = unsafe { CStr::from_ptr(seclabel_ptr) };
        let label = label_cstr.to_str().expect("Failed to convert seclabel");
        if re::is_match_indirect_identifier(label) {
            return;
        }
        error::invalid_label_for("a column", label, None).ereport();
    }

    /* Everything else is not supported */
    error::feature_not_supported("Placing a k_anonymity label on this object").ereport();
}

/// Checking the syntax of a masking rule
///
/// This function is a callback called whenever a SECURITY LABEL is declared on
/// a registered masking policy
///
/// The function returns `()` if the label if fine
/// otherwise it throws an error via ereport() to ROLLBACK the transaction
///
#[pg_guard]
unsafe extern "C-unwind" fn masking_policy_object_relabel(
    object_ptr: *const pg_sys::ObjectAddress,
    seclabel_ptr: *const c_char,
) {
    /* SECURITY LABEL FOR anon ON COLUMN foo.bar IS NULL */
    if seclabel_ptr.is_null() {
        return;
    }

    // convert the object C pointer into a smart pointer
    let object = unsafe {
        PgBox::<pg_sys::ObjectAddress>::from_pg(object_ptr as *mut pg_sys::ObjectAddress)
    };

    // Extract the security label
    let label_cstr = unsafe { CStr::from_ptr(seclabel_ptr) };
    let label = label_cstr.to_str().expect("Failed to convert seclabel");

    match object.classId {
        /* SECURITY LABEL FOR anon ON FUNCTION public.foo() IS 'TRUSTED' */
        pg_sys::ProcedureRelationId => relabel_function(label),

        /* SECURITY LABEL FOR anon ON DATABASE d IS 'TABLESAMPLE SYSTEM(10)' */
        pg_sys::DatabaseRelationId => relabel_database(label),

        /* SECURITY LABEL FOR anon ON TABLE t IS 'TABLESAMPLE SYSTEM(10)' */
        /* SECURITY LABEL FOR anon ON COLUMN t.i IS 'MASKED WITH VALUE $x$' */
        /* SECURITY LABEL FOR anon ON COLUMN t.i IS 'MASKED WITH FUNCTION $x$' */
        /* SECURITY LABEL FOR anon ON COLUMN t.i IS 'NOT MASKED */
        pg_sys::RelationRelationId => {
            /*
             * RelationRelationId will match either a table or a column !
             * If the object subId is 0, it's a table
             */

            if object.objectSubId == 0 {
                /* SECURITY LABEL FOR anon ON TABLE t IS '[...]' */
                relabel_table(label)
            } else {
                /* SECURITY LABEL FOR anon ON COLUMN t.i IS '[...]' */
                relabel_column(label)
            }
        }

        /* SECURITY LABEL FOR anon ON ROLE batman IS 'MASKED' */
        pg_sys::AuthIdRelationId => relabel_role(label),

        /* SECURITY LABEL FOR anon ON SCHEMA public IS 'TRUSTED' */
        pg_sys::NamespaceRelationId => relabel_schema(label),

        /* Any other label is refused */
        _ => error::feature_not_supported("Labeling this object").ereport(),
    }
}

fn relabel_column(label: &str) {
    /* SECURITY LABEL FOR anon ON COLUMN t.i IS 'MASKED WITH VALUE $x$' */
    if let Some(val) = re::capture_value(label) {
        let check_val = input::check_value(val);
        if check_val.is_ok() {
            return;
        }
        error::invalid_label_for("a column", label, Some(check_val.unwrap_err())).ereport();
    }

    /* SECURITY LABEL FOR anon ON COLUMN t.i IS 'MASKED WITH FUNCTION $x$' */
    if let Some(func) = re::capture_function(label) {
        //
        // Inside a *_relabel function, we can't know the name of the label
        // provider, because most of the extensions that use security labels
        // will use only one label provider. We're probably the first extension
        // to allow user-defined label providers, so nobody ever needed
        // to access the name of the provider from within the _relabel function.
        //
        // Anyway this means that we can only check the function is trusted for
        // the default masking policy. In consequence, if a function declared
        // as trusted, it is trusted for all policies.
        //
        let check_func = input::check_function(func, ANON_DEFAULT_MASKING_POLICY);
        if check_func.is_ok() {
            return;
        }
        error::invalid_label_for("a column", label, Some(check_func.unwrap_err())).ereport();
    }

    /* SECURITY LABEL FOR anon ON COLUMN t.i IS 'NOT MASKED */
    if re::is_match_not_masked(label) {
        return;
    }

    error::invalid_label_for("a column", label, None).ereport();
}

fn relabel_database(label: &str) {
    let mut detail: Option<String> = None;

    if re::capture_tablesample(label).is_some() {
        let check_tbs = input::check_tablesample(label);
        if check_tbs.is_ok() {
            return;
        }
        detail = Some(check_tbs.unwrap_err());
    }

    error::invalid_label_for("a database", label, detail).ereport();
}

fn relabel_function(label: &str) {
    if !unsafe { pg_sys::superuser() } {
        error::insufficient_privilege(
            "only a superuser can set an anon label for a function".to_string(),
        )
        .ereport();
    }

    if re::is_match_trusted(label)
        || re::is_match_restricted(label)
        || re::is_match_untrusted(label)
    {
        return;
    }

    error::invalid_label_for("a function", label, None).ereport();
}

fn relabel_role(label: &str) {
    if re::is_match_masked(label) {
        return;
    }
    error::invalid_label_for("a role", label, None).ereport();
}

fn relabel_schema(label: &str) {
    if !unsafe { pg_sys::superuser() } {
        error::insufficient_privilege(
            "only a superuser can set an anon label for a schema".to_string(),
        )
        .ereport();
    }
    if re::is_match_trusted(label) {
        return;
    }

    error::invalid_label_for("a schema", label, None).ereport();
}

/// relabel_table is **almost** equivalent to relabel_database
///
pub fn relabel_table(label: &str) {
    let mut detail: Option<String> = None;
    if re::capture_tablesample(label).is_some() {
        let check_tbs = input::check_tablesample(label);
        if check_tbs.is_ok() {
            return;
        }
        detail = Some(check_tbs.unwrap_err());
    }

    if let Some(when) = re::capture_when(label) {
        let check_when = input::check_when(when, ANON_DEFAULT_MASKING_POLICY);
        if check_when.is_ok() {
            return;
        }
        detail = Some(check_when.unwrap_err());
    }
    error::invalid_label_for("a table", label, detail).ereport();
}

//----------------------------------------------------------------------------
// Tests
//----------------------------------------------------------------------------

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::fixture;
    use crate::label_providers::*;

    #[pg_test]
    fn test_relabel_database_valid_label() {
        relabel_database("TABLESAMPLE SYSTEM(10)")
    }

    #[pg_test(error = "Anon: `INVALID LABEL` is not a valid label for a database")]
    fn test_relabel_database_invalid_label() {
        relabel_database("INVALID LABEL")
    }

    #[pg_test]
    fn test_relabel_table_valid_labels() {
        relabel_table("TABLESAMPLE SYSTEM(10)");
        relabel_table("MASKED WHEN admin IS FALSE");
        relabel_table("MASKED WHEN NOT is_admin");
        relabel_table("MASKED WHEN i > 1000");
    }

    #[pg_test(error = "Anon: `INVALID LABEL` is not a valid label for a table")]
    fn test_relabel_table_invalid_label() {
        relabel_table("INVALID LABEL")
    }

    #[pg_test(error = "Anon: `MASK WHEN i > 1000` is not a valid label for a table")]
    fn test_relabel_table_invalid_label_when() {
        relabel_table("MASK WHEN i > 1000");
    }

    #[pg_test]
    fn test_relabel_schema_valid_label() {
        relabel_schema("TRUSTED")
    }

    #[pg_test(error = "Anon: `INVALID LABEL` is not a valid label for a schema")]
    fn test_relabel_schema_invalid_label() {
        relabel_schema("INVALID LABEL")
    }

    #[pg_test]
    fn test_relabel_function_valid_label() {
        relabel_function("TRUSTED")
    }

    #[pg_test(error = "Anon: `INVALID LABEL` is not a valid label for a function")]
    fn test_relabel_function_invalid_label() {
        relabel_function("INVALID LABEL")
    }

    #[pg_test]
    fn test_relabel_role_valid_label() {
        relabel_role("MASKED")
    }

    #[pg_test(error = "Anon: `INVALID LABEL` is not a valid label for a role")]
    fn test_relabel_role_invalid_label() {
        relabel_role("INVALID LABEL")
    }

    #[pg_test]
    fn test_relabel_column_valid_label() {
        relabel_column("MASKED WITH VALUE NULL");
        relabel_column("MASKED WITH VALUE 'x'");
        relabel_column("MASKED WITH FUNCTION anon.firstname()");
    }

    #[pg_test(error = "Anon: `INVALID LABEL` is not a valid label for a column")]
    fn test_relabel_column_invalid_label() {
        relabel_column("INVALID LABEL")
    }

    #[pg_test(error = "Anon: Labeling this object is not supported")]
    fn test_label_on_type() {
        Spi::run(
            "
           SECURITY LABEL FOR anon ON TYPE int IS 'SHOULD NOT WORK';
        ",
        )
        .unwrap();
    }

    #[pg_test]
    fn test_kanonymity_null() {
        fixture::create_table_person();
        Spi::run(
            "
            SECURITY LABEL FOR k_anonymity ON COLUMN person.lastname IS NULL;
        ",
        )
        .unwrap();
    }

    #[pg_test]
    fn test_kanonymity_indirect_identifier() {
        fixture::create_table_person();
        Spi::run(
            "
            SECURITY LABEL FOR k_anonymity ON COLUMN person.lastname IS 'INDIRECT IDENTIFIER';
        ",
        )
        .unwrap();
    }

    #[pg_test(error = "Anon: Placing a k_anonymity label on this object is not supported")]
    fn test_kanonymity_not_supported() {
        fixture::create_table_person();
        Spi::run(
            "
            SECURITY LABEL FOR k_anonymity ON TYPE int IS 'INDIRECT IDENTIFIER';
        ",
        )
        .unwrap();
    }
}

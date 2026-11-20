///
/// # Masking Rules upon databases
///
/// a set of utility functions to check and manage database masking rules
///
use crate::re;
use crate::rule::Rule;
use crate::rule::RuleError;
use pgrx::prelude::*;

#[derive(PartialEq, Clone, Debug)]
pub struct Database(Rule);

impl Database {
    fn from_seclabel(object_id: pg_sys::Oid, policy: &str) -> Result<Self, RuleError> {
        Ok(Self(Rule::from_seclabel(
            pg_sys::DatabaseRelationId,
            object_id,
            0,
            policy,
        )?))
    }

    pub fn get_current_database_ratio(policy: &str) -> Option<String> {
        let current_db_id = unsafe { pg_sys::MyDatabaseId };
        let rule_on_db = Self::from_seclabel(current_db_id, policy).ok()?;
        re::capture_tablesample(&rule_on_db.0.seclabel).map(|s| s.to_string())
    }
}

//----------------------------------------------------------------------------
// Tests
//----------------------------------------------------------------------------

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use super::*;
    use crate::fixture;
    use crate::label_providers;
    use pgrx::pg_sys;
    use std::ffi::CStr;

    static ANON: &str = label_providers::ANON_DEFAULT_MASKING_POLICY;

    #[pg_test]
    fn test_get_current_database_ratio() {
        let db_name_ptr = unsafe { pg_sys::get_database_name(pg_sys::MyDatabaseId) };
        let db_name_cstr = unsafe { CStr::from_ptr(db_name_ptr) };
        assert!(Database::get_current_database_ratio(ANON).is_none());

        fixture::declare_sampling_for_database(db_name_cstr.to_str().unwrap().to_string());
        assert!(Database::get_current_database_ratio(ANON).is_some());
    }

    #[pg_test]
    fn test_get_current_database_ratio_none() {
        assert!(Database::get_current_database_ratio(ANON).is_none());
    }
}

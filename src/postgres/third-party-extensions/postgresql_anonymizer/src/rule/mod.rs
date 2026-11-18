///
/// # Rule Module
///
use pgrx::prelude::*;
use std::ffi::CStr;
use std::ffi::CString;
use std::os::raw::c_char;

pub mod column;
pub mod database;
pub mod function;
pub mod role;
pub mod schema;
pub mod table;

//----------------------------------------------------------------------------
// Errors
//----------------------------------------------------------------------------

///
/// The Reason enum describes a series of problem that may occur when trying
/// to read the masking rule of an object
///
#[derive(PartialEq, Eq, Clone, Debug)]
pub enum RuleError {
    NoRule,
    InvalidObject,
    //    InvalidInput,
}

use RuleError::*;

impl std::fmt::Display for RuleError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            NoRule => write!(f, "Anon: No rule found"),
            InvalidObject => write!(f, "Anon: Invalid Object"),
        }
    }
}

impl std::error::Error for RuleError {}

#[derive(PartialEq, Clone, Debug)]
struct Rule {
    seclabel: String,
}

impl Rule {
    pub fn from_seclabel(
        class_id: pg_sys::Oid,
        object_id: pg_sys::Oid,
        object_sub_id: i32,
        policy: &str,
    ) -> Result<Self, RuleError> {
        // Build the object
        let object = pg_sys::ObjectAddress {
            classId: class_id,
            objectId: object_id,
            objectSubId: object_sub_id,
        };

        let policy_c_str = CString::new(policy).unwrap();
        let policy_c_ptr = policy_c_str.as_ptr();

        let Some(seclabel_box) = PgTryBuilder::new(|| {
            Some(unsafe { PgBox::from_pg(pg_sys::GetSecurityLabel(&object, policy_c_ptr)) })
        })
        .catch_others(|_| None)
        .execute() else {
            return Err(InvalidObject);
        };

        // When the seclabel is NULL,
        // it means the object has no masking rule in this policy
        if seclabel_box.is_null() {
            return Err(NoRule);
        }

        let seclabel_cstr = unsafe { CStr::from_ptr(seclabel_box.as_ptr() as *const c_char) };
        let seclabel_str = seclabel_cstr.to_str().expect("Failed to convert seclabel");

        // return the Rule
        Ok(seclabel_str.into())
    }
}

impl From<&str> for Rule {
    fn from(seclabel_str: &str) -> Self {
        Self {
            seclabel: seclabel_str.to_string(),
        }
    }
}

impl std::fmt::Display for Rule {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{}", self.seclabel)
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

    static ANON: &str = label_providers::ANON_DEFAULT_MASKING_POLICY;

    #[pg_test]
    fn test_valid() {
        let batman = fixture::create_masked_role();
        let masked = Rule::from("MASKED");
        assert_eq!(
            Rule::from_seclabel(pg_sys::AuthIdRelationId, batman, 0, ANON),
            Ok(masked)
        );
    }

    #[pg_test]
    fn test_no_rule() {
        let bruce = fixture::create_unmasked_role();
        assert_eq!(
            Err(RuleError::NoRule),
            Rule::from_seclabel(pg_sys::AuthIdRelationId, bruce, 0, ANON)
        );
    }

    #[pg_test]
    fn test_invalid_classid() {
        let bruce = fixture::create_unmasked_role();
        assert!(Rule::from_seclabel(pg_sys::InvalidOid, bruce, 0, ANON).is_err());
    }

    #[pg_test]
    fn test_invalid_objectid() {
        assert!(
            Rule::from_seclabel(pg_sys::AuthIdRelationId, pg_sys::InvalidOid, 0, ANON).is_err()
        );
    }
}

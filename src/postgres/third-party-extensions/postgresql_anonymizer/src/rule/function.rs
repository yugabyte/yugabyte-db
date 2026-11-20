///
/// # Masking Rules upon functions
///
///
use crate::error;
use crate::macros;
use crate::re;
use crate::rule::Rule;
use crate::rule::RuleError;
use pgrx::prelude::*;

#[derive(PartialEq, Clone, Debug)]
pub struct Function(Rule);

impl Function {
    pub fn from_seclabel(object_id: pg_sys::Oid, policy: &str) -> Result<Self, RuleError> {
        Ok(Self(Rule::from_seclabel(
            pg_sys::ProcedureRelationId,
            object_id,
            0,
            policy,
        )?))
    }

    /// Check that a function is restricted in a given policy
    pub fn is_empty(&self) -> bool {
        self.0.seclabel.is_empty()
    }

    /// Check that a function is restricted in a given policy
    pub fn is_trusted(&self) -> bool {
        re::is_match_trusted(&self.0.seclabel)
    }

    /// Check that a function is restricted in a given policy
    pub fn is_untrusted(&self) -> bool {
        re::is_match_untrusted(&self.0.seclabel)
    }

    /// Check that a function is restricted in a given policy
    pub fn is_restricted(&self) -> bool {
        re::is_match_restricted(&self.0.seclabel)
    }

    /// Check that a function is restricted in a given policy
    pub fn is_restricted_function(func_id: pg_sys::Oid, policy: &str) -> bool {
        // Check that the OID is fine
        if !macros::OidIsValid(func_id) {
            error::internal("Function OID is invalid").ereport();
        };

        // Get the security label for this definition
        if let Ok(rule_on_func) = Self::from_seclabel(func_id, policy) {
            return re::is_match_restricted(&rule_on_func.0.seclabel);
        }
        false
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use super::*;
    use crate::fixture;
    use crate::label_providers;

    static ANON: &str = label_providers::ANON_DEFAULT_MASKING_POLICY;

    #[pg_test]
    fn test_is_restricted_function() {
        let pseudo_id = fixture::create_restricted_function();
        assert!(Function::is_restricted_function(pseudo_id, ANON));

        let rule_func = Function::from_seclabel(pseudo_id, ANON);
        assert!(rule_func.unwrap().is_restricted());

        let lower = Spi::get_one::<pg_sys::Oid>("SELECT 'lower(TEXT)'::REGPROCEDURE::OID;")
            .unwrap()
            .expect("should be an OID");
        assert!(!Function::is_restricted_function(lower, ANON));
    }

    #[pg_test(error = "Anon: Function OID is invalid")]
    fn test_is_restricted_function_invalid() {
        assert!(Function::is_restricted_function(0.into(), ANON));
    }
}

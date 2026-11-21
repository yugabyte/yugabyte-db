///
/// # Masking Rules upon schemas
///
/// a set of utility schemas to check and manage masking rules
///
use crate::error;
use crate::macros;
use crate::re;
use crate::rule::Rule;
use crate::rule::RuleError;
use pgrx::prelude::*;

#[derive(PartialEq, Clone, Debug)]
pub struct Schema(Rule);

impl Schema {
    fn from_seclabel(object_id: pg_sys::Oid, policy: &str) -> Result<Self, RuleError> {
        Ok(Self(Rule::from_seclabel(
            pg_sys::NamespaceRelationId,
            object_id,
            0,
            policy,
        )?))
    }

    /// Check that a schema is trusted in a given policy
    pub fn is_trusted(schema_id: pg_sys::Oid, policy: &str) -> bool {
        // Check that the OID is fine
        if !macros::OidIsValid(schema_id) {
            error::internal("Schema OID is invalid").ereport();
        };

        if let Ok(rule_on_schema) = Self::from_seclabel(schema_id, policy) {
            return re::is_match_trusted(&rule_on_schema.0.seclabel);
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
    fn test_is_trusted_namespace() {
        let gotham = fixture::create_trusted_schema();
        let arkham = fixture::create_untrusted_schema();
        assert!(Schema::is_trusted(gotham, ANON));
        assert!(!Schema::is_trusted(arkham, ANON));
    }

    #[pg_test(error = "Anon: Schema OID is invalid")]
    fn test_is_trusted_namespace_invalid_schema() {
        let _ = Schema::is_trusted(0.into(), ANON);
    }
}

///
/// # Masking Rules upon Roles
///
use crate::re;
use crate::rule::Rule;
use crate::rule::RuleError;
use pgrx::prelude::*;

#[derive(PartialEq, Clone, Debug)]
pub struct Role(Rule);

impl Role {
    fn from_seclabel(object_id: pg_sys::Oid, policy: &str) -> Result<Self, RuleError> {
        Ok(Self(Rule::from_seclabel(
            pg_sys::AuthIdRelationId,
            object_id,
            0,
            policy,
        )?))
    }

    /// Check that a role is masked in the given policy
    ///
    pub fn has_mask_in_policy(roleid: pg_sys::Oid, policy: &str) -> bool {
        if let Ok(rule) = Self::from_seclabel(roleid, policy) {
            return re::is_match_masked(&rule.0.seclabel);
        }
        false
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
    fn test_rule_role_valid() {
        let batman = fixture::create_masked_role();
        let masked = Role(Rule::from("MASKED"));
        assert_eq!(Ok(masked), Role::from_seclabel(batman, ANON));
    }

    #[pg_test]
    fn test_rule_role_no_rule() {
        let bruce = fixture::create_unmasked_role();
        assert_eq!(Err(RuleError::NoRule), Role::from_seclabel(bruce, ANON));
    }

    #[pg_test]
    fn test_rule_role_invalid_input() {
        assert!(Role::from_seclabel(0.into(), ANON).is_err());
        assert!(Role::from_seclabel(0.into(), "").is_err());
        assert!(Role::from_seclabel(pg_sys::InvalidOid, "").is_err());
    }

    #[pg_test]
    fn test_has_mask_in_policy_anon() {
        let batman = fixture::create_masked_role();
        let bruce = fixture::create_unmasked_role();
        assert!(Role::has_mask_in_policy(batman, ANON));
        assert!(!Role::has_mask_in_policy(bruce, ANON));
        assert!(!Role::has_mask_in_policy(batman, "does_not_exist"));

        let not_a_real_roleid = pg_sys::Oid::from(99999999);
        assert!(!Role::has_mask_in_policy(not_a_real_roleid, ANON));
    }

    #[pg_test]
    fn test_has_mask_in_multiple_policies() {
        fixture::declare_masking_policies();
        label_providers::register_label_providers();
        let devin = fixture::create_masked_role_in_policy("devin", "devtests");
        let anna = fixture::create_masked_role_in_policy("anna", "analytics");
        assert!(Role::has_mask_in_policy(devin, "devtests"));
        assert!(!Role::has_mask_in_policy(devin, ANON));
        assert!(Role::has_mask_in_policy(anna, "analytics"));
        assert!(!Role::has_mask_in_policy(anna, "devtests"));
    }
}

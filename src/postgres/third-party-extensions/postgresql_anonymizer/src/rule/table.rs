///
/// # Masking Rules upon Roles
///
use crate::re;
use crate::rule::Rule;
use crate::rule::RuleError;
use pgrx::prelude::*;

#[derive(PartialEq, Clone, Debug)]
pub struct Table(Rule);

impl Table {
    fn from_seclabel(object_id: pg_sys::Oid, policy: &str) -> Result<Self, RuleError> {
        Ok(Self(Rule::from_seclabel(
            pg_sys::RelationRelationId,
            object_id,
            0,
            policy,
        )?))
    }

    ///
    /// ## Selective Masking
    ///
    /// Extract the `MASKED WHEN` condition of a table
    ///
    pub fn get_when(relid: pg_sys::Oid, policy: &str) -> Option<String> {
        match Self::from_seclabel(relid, policy).ok() {
            Some(rule) => re::capture_when(&rule.0.seclabel).map(|s| s.to_string()),
            None => None,
        }
    }

    ///
    /// ## Sampling
    ///
    pub fn get_ratio(relid: pg_sys::Oid, policy: &str) -> Option<String> {
        use crate::rule::database::Database;
        Self::get_table_ratio(relid, policy).or(Database::get_current_database_ratio(policy))
    }

    fn get_table_ratio(relid: pg_sys::Oid, policy: &str) -> Option<String> {
        let rule_on_table = Self::from_seclabel(relid, policy).ok()?;
        re::capture_tablesample(&rule_on_table.0.seclabel).map(|s| s.to_string())
        // Convert &str to String
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

    static ANON: &str = label_providers::ANON_DEFAULT_MASKING_POLICY;
    #[pg_test]
    fn test_rule_on_table() {
        let relid = fixture::create_table_person();
        assert!(Table::from_seclabel(relid, ANON).is_ok());
    }

    #[pg_test]
    fn test_rule_on_table_no_rule() {
        let relid = fixture::create_table_location();
        assert_eq!(Err(RuleError::NoRule), Table::from_seclabel(relid, ANON));
    }

    #[pg_test]
    fn test_rule_on_table_invalid_input() {
        let relid = fixture::create_table_person();
        assert_eq!(Err(RuleError::NoRule), Table::from_seclabel(relid, ""));
        assert_eq!(
            Err(RuleError::NoRule),
            Table::from_seclabel(pg_sys::InvalidOid, ANON)
        );
    }

    #[pg_test]
    fn test_get_table_when() {
        let relid = fixture::create_table_account();
        assert_eq!(Some("NOT is_admin".into()), Table::get_when(relid, ANON));
    }

    #[pg_test]
    fn test_get_table_when_no_policy() {
        let relid = fixture::create_table_account();
        assert!(Table::get_when(relid, "does_not_exist").is_none());
        assert!(Table::get_when(relid, "").is_none());
    }

    #[pg_test]
    fn test_get_table_when_invalid_oid() {
        let invalid = pg_sys::InvalidOid;
        assert!(Table::get_when(invalid, ANON).is_none());
    }

    #[pg_test]
    fn test_get_table_when_none() {
        let relid = fixture::create_table_location();
        assert!(Table::get_when(relid, ANON).is_none());
    }

    #[pg_test]
    fn test_get_table_ratio() {
        let relid = fixture::create_table_person();
        assert_eq!(
            Some("BERNOULLI(10)".into()),
            Table::get_table_ratio(relid, ANON)
        );
    }

    #[pg_test]
    fn test_get_table_ratio_no_policy() {
        let relid = fixture::create_table_person();
        assert!(Table::get_table_ratio(relid, "does_not_exist").is_none());
        assert!(Table::get_table_ratio(relid, "").is_none());
    }

    #[pg_test]
    fn test_get_table_ratio_invalid_oid() {
        let invalid = pg_sys::InvalidOid;
        assert!(Table::get_table_ratio(invalid, ANON).is_none());
    }

    #[pg_test]
    fn test_get_table_ratio_none() {
        let relid = fixture::create_table_location();
        assert!(Table::get_table_ratio(relid, ANON).is_none());
    }
}

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
pub struct Column(Rule);

impl Column {
    pub fn from_seclabel(
        object_id: pg_sys::Oid,
        object_sub_id: i32,
        policy: &str,
    ) -> Result<Self, RuleError> {
        Ok(Self(Rule::from_seclabel(
            pg_sys::RelationRelationId,
            object_id,
            object_sub_id,
            policy,
        )?))
    }

    /// Extract the function from a Masking Rule
    pub fn get_function(&self) -> Option<String> {
        re::capture_function(&self.0.seclabel).map(|s| s.to_string())
    }

    /// Extract the function from a Masking Rule
    pub fn get_value(&self) -> Option<String> {
        re::capture_value(&self.0.seclabel).map(|s| s.to_string())
    }

    /// Check if a column is declared as "NOT MASKED"
    pub fn is_not_masked(&self) -> bool {
        re::is_match_not_masked(&self.0.seclabel)
    }
}

impl From<&str> for Column {
    fn from(seclabel_str: &str) -> Self {
        Self(Rule::from(seclabel_str))
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
    use crate::rule::RuleError;

    static ANON: &str = label_providers::ANON_DEFAULT_MASKING_POLICY;

    #[pg_test]
    fn test_column_from_seclabel() {
        let relid = fixture::create_table_person();

        // testing a dropped column
        let col1 = Column::from_seclabel(relid, 1, ANON);
        assert_eq!(col1, Err(RuleError::NoRule));

        // testing the first column
        let col2 = Column::from_seclabel(relid, 2, ANON);
        assert_eq!(col2, Err(RuleError::NoRule));

        // testing the second column
        let col3 = Column::from_seclabel(relid, 3, ANON);
        assert_eq!(col3, Ok(Column(Rule::from("MASKED WITH VALUE NULL"))));
    }
}

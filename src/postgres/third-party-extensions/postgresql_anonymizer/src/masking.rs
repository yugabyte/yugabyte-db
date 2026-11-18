///
/// # Masking Engine
///
use crate::error;
use crate::guc;
use crate::re;
use crate::utils;
use md5::{Digest, Md5};
use pgrx::PgRelation;
use pgrx::prelude::*;
use std::ffi::CStr;
use std::ffi::CString;
use std::os::raw::c_char;

//----------------------------------------------------------------------------
// Public functions
//----------------------------------------------------------------------------

/// For a given role, returns the policy in which he/she is masked
/// or the NULL if the role is not masked.
///
/// * roleid is the id of the user we want to mask
///
pub fn get_masking_policy(roleid: pg_sys::Oid) -> Option<String> {
    // Possible Improvement : allow masking rule inheritance by checking
    // also the roles that the user belongs to
    // This may be done by using `roles_is_member_of()` ?

    /*
        for policy in list_masking_policies() {
            if has_mask_in_policy(roleid, policy.clone()) {
                return Some(policy);
            }
        }

        // Found nothing, return NULL
        None
    */

    use crate::rule::role::Role;

    list_masking_policies()
        .into_iter()
        .find(|policy| Role::has_mask_in_policy(roleid, policy))
}

/// Return all the registered masking policies
///
/// We can't use pg_sys::SplitGUCList(...) here because extension are not
/// allowed to define custom GUC_LIST_QUOTE variables and thus PGRX does not
/// support the GUC_LIST_INPUT. So we split the variable here with a very basic
/// approach (spaces are not handled) and we use `:` as separator to avoid
/// confusion with traditional GUC_LIST_QUOTE parameters.
///
pub fn list_masking_policies() -> Vec<String> {
    use crate::label_providers::ANON_DEFAULT_MASKING_POLICY;

    let mut masking_policies = vec![ANON_DEFAULT_MASKING_POLICY.into()];
    masking_policies.append(&mut re::capture_guc_list(
        guc::ANON_MASKING_POLICIES.get().unwrap_or(c"".into()),
    ));
    masking_policies
}

/// Returns a String and bool
///
/// The String is the "select clause filters" that will mask the authentic data
/// of a table for a given masking policy
///
/// the bool indicate is the table as at least one masked column
///
pub fn masking_expressions(relid: pg_sys::Oid, policy: String) -> (String, bool) {
    use crate::rule::table::Table;

    let mut table_has_one_masked_column = false;

    // SAFETY: `pg_sys::relation_open()` will raise XX000
    // if the specified oid isn't a valid relation
    let relation = unsafe { PgRelation::with_lock(relid, pg_sys::AccessShareLock as i32) };
    let when = Table::get_when(relid, &policy);

    let mut expressions = Vec::new();
    for attribute in relation.tuple_desc().iter() {
        if attribute.attisdropped {
            continue;
        }

        let (filter_value, att_is_masked) =
            value_for_att(&relation, attribute, when.clone(), policy.clone());

        if att_is_masked {
            table_has_one_masked_column = true;
        }
        let attname_quoted = utils::quote_name_data(&attribute.attname);
        let filter = format!("{filter_value} AS {attname_quoted}");
        expressions.push(filter);
    }

    (
        expressions.join(", ").to_string(),
        table_has_one_masked_column,
    )
}

/// Returns the masking filter that will mask the authentic data
/// of a column for a given masking policy.
/// the 2nd return value is a bool that indicate if the column is masked or not
///
/// * relid is the relation OID
/// * colnum is the attribute position, numbered from 1 up
/// * policy is the masking policy
///
pub fn masking_value_for_column(
    relid: pg_sys::Oid,
    colnum: i32,
    policy: String,
) -> Option<(String, bool)> {
    use crate::rule::table::Table;

    let relation = unsafe { PgRelation::with_lock(relid, pg_sys::AccessShareLock as i32) };
    let p = policy.clone();
    let when = Table::get_when(relid, &p);

    let desc = relation.tuple_desc();

    // Here attributes are numbered from 0 up
    let attribute = desc.get(colnum as usize - 1)?;

    if attribute.attisdropped {
        return None;
    }

    let (masking_value, att_is_masked) = value_for_att(&relation, attribute, when, policy);

    Some((masking_value, att_is_masked))
}

/// Prepare a SQL Statement object that will replace the authentic relation
///
/// * relid is the oid of the relation
/// * policy is the masking policy to apply
///
/// The masking subquery is composed of 2 SELECT
///   - The first will apply the masking filters and the tablesample ratio
///   - The second will apply the generated column expressions (if any)
///
/// Example:
///
/// Imagine the table below:
///
///   ```sql
///   CREATE TABLE nba.player (
///     name TEXT,
///     height_cm SMALLINT,
///     height_in NUMERIC GENERATED ALWAYS AS (height_cm / 2.54) STORED
///   );
///
///   SECURITY LABEL FOR anon ON COLUMN nba.player.height_cm
///     IS 'MASKED WITH FUNCTION pg_catalog.random(170,220)';
///
///   SECURITY LABEL FOR anon ON TABLE nba.player
///     IS 'TABLESAMPLE BERNOULLI(50)';
///   ```
///
/// The masking subquery for this table would be
///
///   ``` sql
///   SELECT name, height_cm, height_cm / 2.54 AS height_in
///   FROM (
///      SELECT name, pg_catalog.random(170,220), height_in
///      FROM nba.player
///      TABLESAMPLE BERNOULLI(50)
///   ) AS anon_tmp_5eb63bbbe01eeed093cb22bb8f5acdc3;
///   ```
///
pub fn subquery(relid: pg_sys::Oid, inherit: bool, policy: String) -> Option<String> {
    use crate::rule::table::Table;

    let (masking_expressions, table_is_masked) = masking_expressions(relid, policy.clone());
    let ratio = Table::get_ratio(relid, &policy);

    // if there's no mask and no tablesample ratio,
    // do not provide a subquery for this table
    if !table_is_masked && ratio.is_none() {
        return None;
    }

    let gen_expressions = generation_expressions(relid);

    let tablename = utils::get_relation_qualified_name(relid)?;

    // respect the FROM ONLY clause
    let only = if inherit { "" } else { "ONLY" };

    let tablesample: String = ratio
        .map(|v| format!("TABLESAMPLE {}", v))
        .unwrap_or_default();

    // build an alias for the masking subquery and use the hash of the table
    // name to avoid collisions.
    // Alias on subqueries are no longer required since PG16
    //
    let mut hasher = Md5::new();
    hasher.update(tablename.clone());
    let tablename_hash = format!("{:X}", hasher.finalize());

    Some(format!(
        "
        SELECT {gen_expressions}
        FROM (
            SELECT {masking_expressions}
            FROM {only} {tablename}
            {tablesample}
        ) AS anon_alias_{tablename_hash}"
    ))
}

/// Prepare a ParseTree object from a SQL query
///
pub fn parse_subquery(query_sql: String) -> PgBox<pg_sys::RawStmt> {
    let query_c_string = CString::new(query_sql.as_str()).unwrap();
    let query_ptr = query_c_string.as_c_str().as_ptr() as *const c_char;

    let raw_parsetree_list = unsafe { pg_sys::pg_parse_query(query_ptr) };

    // extract the raw statement
    // this is the equivalent of the linitial_node C macro
    // https://doxygen.postgresql.org/pg__list_8h.html#a213ac28ac83471f2a47d4e3918f720b4
    unsafe { PgBox::from_pg(pg_sys::list_nth(raw_parsetree_list, 0) as *mut pg_sys::RawStmt) }
}

//----------------------------------------------------------------------------
// Private functions
//----------------------------------------------------------------------------

/// Decorate a value with a CAST function
///
/// Example: the value `1` will be transformed into `CAST(1 AS INT)`
///
/// * value is the value to transform
/// * atttypid is the id of the type for this data
/// * atttypmod is the type modifier (for ARRAY types)
///
fn cast_as_regtype(value: String, atttypid: pg_sys::Oid, atttypmod: i32) -> String {
    let type_extended = unsafe {
        CStr::from_ptr(pg_sys::format_type_extended(
            atttypid,
            atttypmod,
            pg_sys::FORMAT_TYPE_TYPEMOD_GIVEN.try_into().unwrap(),
        ))
    }
    .to_str()
    .unwrap();
    format!("CAST({value} AS {type_extended})")
}

/// Returns a String and bool
///
/// The String is the list of "select clause filters" containing the column
/// names or the generation expression for generated columns.
///
fn generation_expressions(relid: pg_sys::Oid) -> String {
    let mut table_has_one_generated_column = false;

    // SAFETY: `pg_sys::relation_open()` will raise XX000
    // if the specified oid isn't a valid relation
    let relation = unsafe { PgRelation::with_lock(relid, pg_sys::AccessShareLock as i32) };

    let mut expressions = Vec::new();
    for attribute in relation.tuple_desc().iter() {
        if attribute.attisdropped {
            continue;
        }
        let attname_quoted = utils::quote_name_data(&attribute.attname);
        let generation_expression = default_for_att(&relation, attribute, true);

        if let Some(filter_value) = generation_expression {
            table_has_one_generated_column = true;
            expressions.push(format!("{filter_value} AS {attname_quoted}"));
        } else {
            expressions.push(attname_quoted.into());
        }
    }

    if table_has_one_generated_column {
        expressions.join(", ").to_string()
    } else {
        "*".into()
    }
}

/// Checks weither a column is generated or not
fn is_generated(att: &pg_sys::FormData_pg_attribute) -> bool {
    att.attgenerated != '\0' as c_char
}

/// Returns the default value or generated value for a column
///
/// this is similar to `SELECT pg_get_expr(adbin, adrelid) FROM pg_attrdef`
///
fn default_for_att(
    rel: &PgRelation,
    att: &pg_sys::FormData_pg_attribute,
    generated: bool,
) -> Option<String> {
    // Skip if the attribute is dropped
    if att.attisdropped {
        return None;
    }

    // skip if this is a generated column and we don't want them
    if generated != is_generated(att) {
        return None;
    }

    // constr is a TupleConstr object
    // https://doxygen.postgresql.org/structTupleConstr.html
    let constr = unsafe {
        // SAFETY: constr is always defined
        PgBox::from_pg(rel.tuple_desc().constr)
    };

    // loop over the constraints of the relation in search of
    // the default value of this column
    for i in 0..constr.num_defval {
        // defval is a AttrDefault object
        // https://doxygen.postgresql.org/structAttrDefault.html
        let defval = unsafe {
            // SAFETY: constr.defval is an array with an entry per column
            PgBox::from_pg(constr.defval.wrapping_add(i.into()))
        };

        if defval.adnum == att.attnum {
            // Found it !

            // Extract the textual representation of the default value of
            // this column. The default value is stored in a binary format
            let context = unsafe {
                pg_sys::deparse_context_for(pg_sys::get_rel_name(att.attrelid), att.attrelid)
            };

            let default_value_c_ptr = unsafe {
                // SAFETY: deparse_expression is unsafe but we can assume
                // that `defval.adbin` is always a correct Node
                pg_sys::deparse_expression(
                    pg_sys::stringToNode(defval.adbin) as *mut pg_sys::Node,
                    context,
                    false,
                    false,
                    false, /* yb_pretty */
                    false, /* yb_maskconstants */
                ) as *mut c_char
            };

            // Convert the c_char pointer into a string
            let default_value_c_str = unsafe { CStr::from_ptr(default_value_c_ptr) };

            // Stop the loop once we found the right column
            return Some(default_value_c_str.to_str().unwrap().to_string());
        }
    }
    // found nothing
    None
}

/// Write a classic CASE conditional expression around the masked value
///
fn case(when: Option<String>, masked_value: &str, real_value: &str) -> Option<String> {
    let some_when = when?;
    Some(format!(
        "\
        CASE WHEN CAST({some_when} AS BOOL) \
              THEN {masked_value} \
              ELSE {real_value} \
        END"
    ))
}

/// Decorate a masking value
///
/// A masking value may be surrounded by a CAST and/or a CASE WHEN expressions
///
fn decorate_mask(
    masking_value: String,
    att: &pg_sys::FormData_pg_attribute,
    when: Option<String>,
) -> String {
    let attname = utils::quote_name_data(&att.attname);

    let casted_value = if guc::ANON_STRICT_MODE.get() {
        cast_as_regtype(masking_value, att.atttypid, att.atttypmod)
    } else {
        masking_value
    };

    // Conditional masking if a `when` is defined on the table
    case(when, &casted_value, attname).unwrap_or(casted_value)
}

/// Returns the masking value for a column, with a string and a bool
///
/// the bool means whether the column is masked or not
/// the string is the value of the attribute based on its masking rule (if any),
/// which can be either:
///     - the attribute name (i.e. the authentic value)
///     - the function or value from the masking rule
///     - the "generation expression" of a generated column
///     - the default value of the column
///     - "NULL"
///
pub fn value_for_att(
    rel: &PgRelation,
    att: &pg_sys::FormData_pg_attribute,
    when: Option<String>,
    policy: String,
) -> (String, bool) {
    use crate::rule::RuleError;
    use crate::rule::column::Column;

    let attname = utils::quote_name_data(&att.attname);

    // Get the masking rule, if any
    let rule_on_column = match Column::from_seclabel(rel.rd_id, att.attnum as i32, &policy) {
        Ok(seclabel) => seclabel,
        Err(RuleError::NoRule) => {
            // If no masking rule found and Privacy By Default is off,
            // the authentic value is revealed
            if !guc::ANON_PRIVACY_BY_DEFAULT.get() {
                return (attname.to_string(), false);
            }
            Column::from("")
        }
        Err(error) => {
            error::internal(&format!("{error}"));
            panic!()
        }
    };

    // Search for a masking function
    if let Some(function) = rule_on_column.get_function() {
        return (decorate_mask(function, att, when), true);
    }

    // Search for a masking value
    if let Some(value) = rule_on_column.get_value() {
        return (decorate_mask(value, att, when), true);
    }

    // The column is declared as not masked, the authentic value is shown
    if rule_on_column.is_not_masked() {
        return (attname.to_string(), false);
    }

    // There's no masking rule

    // At this stage, we know privacy_by_default is on
    // Let's try to find the default value of the column
    if att.atthasdef && att.attnum > 0 && !att.attisdropped {
        if let Some(default_value) = default_for_att(rel, att, false) {
            // mask with the default value
            return (default_value, true);
        }
        // no default value, mask with "NULL"
        return ("NULL".to_string(), true);
    }

    // No default value, "NULL" (the literal value) is the last possibility
    ("NULL".to_string(), true)
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
    use crate::label_providers::ANON_DEFAULT_MASKING_POLICY;
    use crate::rule::table::Table;

    #[pg_test]
    fn test_case() {
        assert!(case(None, "'CONFIDENTIAL'", "login").is_none());
        assert_eq!(
            case(Some("is_admin IS FALSE".into()), "'CONFIDENTIAL'", "login"),
            Some(
                "CASE WHEN CAST(is_admin IS FALSE AS BOOL) THEN 'CONFIDENTIAL' ELSE login END"
                    .into()
            )
        );
    }

    #[pg_test]
    fn test_cast_as_regtype() {
        let smallint_oid = pg_sys::Oid::from(21);
        assert_eq!(
            "CAST(0 AS smallint)",
            cast_as_regtype('0'.to_string(), smallint_oid, -1)
        );
        let char_oid = pg_sys::Oid::from(18);
        assert_eq!(
            "CAST('abcd' AS \"char\"(4))",
            cast_as_regtype("'abcd'".to_string(), char_oid, 4)
        );
    }

    #[pg_test]
    fn test_default_for_att() {
        // Create a table with default values
        let relid = fixture::create_table_with_defaults();
        let relation = unsafe { PgRelation::with_lock(relid, pg_sys::AccessShareLock as i32) };

        let desc = relation.tuple_desc();

        // Test column with default value
        // Assuming the second column has a default value
        let att_with_default = desc.get(1).unwrap();
        let default_value = default_for_att(&relation, att_with_default, false);
        assert_eq!(default_value, Some("'default_value'::text".to_string()));
        let generation_expr = default_for_att(&relation, att_with_default, true);
        assert_eq!(generation_expr, None);

        // Test column with complex default expression
        // Assuming the third column has a complex default
        let att_with_complex_default = desc.get(2).unwrap();
        let complex_default_value = default_for_att(&relation, att_with_complex_default, false);
        assert_eq!(complex_default_value, Some("now()".to_string()));

        // Test column without default value
        // Assuming the fourth column has no default
        let att_without_default = desc.get(3).unwrap();
        let no_default_value = default_for_att(&relation, att_without_default, false);
        assert_eq!(no_default_value, None);

        // Test column without generated value
        // Assuming the fifth column a generation expression
        let att_generated = desc.get(4).unwrap();
        let generation_expr = default_for_att(&relation, att_generated, true);
        assert_eq!(
            generation_expr,
            Some("((col_without_default)::numeric / 2.54)".into())
        );

        let not_generation_expr = default_for_att(&relation, att_generated, false);
        assert_eq!(not_generation_expr, None);

        // Test dropped column
        let att_dropped = desc.get(5).unwrap();
        let nothing = default_for_att(&relation, att_dropped, true);
        assert_eq!(nothing, None);
    }

    #[pg_test]
    fn test_default_for_att_non_existent_column() {
        let relid = fixture::create_table_with_defaults();
        let relation = unsafe { PgRelation::with_lock(relid, pg_sys::AccessShareLock as i32) };

        // Create a fake attribute that doesn't exist in the table
        let fake_att = pg_sys::FormData_pg_attribute {
            attnum: 999, // A column number that doesn't exist
            ..Default::default()
        };

        let default_value = default_for_att(&relation, &fake_att, false);
        assert_eq!(default_value, None);
        let generated_value = default_for_att(&relation, &fake_att, true);
        assert_eq!(generated_value, None);
    }

    #[pg_test]
    fn test_get_masking_policy() {
        let batman = fixture::create_masked_role();
        let bruce = fixture::create_unmasked_role();
        let expected = Some(ANON_DEFAULT_MASKING_POLICY.to_string());
        assert_eq!(get_masking_policy(batman), expected);
        assert!(get_masking_policy(bruce).is_none())
    }

    #[pg_test]
    fn test_get_multiple_policies() {
        fixture::declare_masking_policies();
        label_providers::register_label_providers();
        let devin = fixture::create_masked_role_in_policy("devin", "devtests");
        let anna = fixture::create_masked_role_in_policy("anna", "analytics");
        let devtests = Some("devtests".to_string());
        let analytics = Some("analytics".to_string());
        assert_eq!(get_masking_policy(devin), devtests);
        assert_eq!(get_masking_policy(anna), analytics);
    }

    #[pg_test]
    fn test_list_masking_policies_default() {
        assert_eq!(vec![ANON_DEFAULT_MASKING_POLICY], list_masking_policies());
    }

    #[pg_test]
    fn test_list_masking_policies_multiple() {
        fixture::declare_masking_policies();
        assert_eq!(
            vec![ANON_DEFAULT_MASKING_POLICY, "devtests", "analytics"],
            list_masking_policies()
        );
    }

    #[pg_test]
    fn test_masking_value_for_column() {
        let relid = fixture::create_table_person();
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();

        // testing a dropped column
        let none = masking_value_for_column(relid, 1, anon.clone());
        assert_eq!(None, none);

        // testing the first column
        let (result_2, is_masked_2) = masking_value_for_column(relid, 2, anon.clone()).unwrap();
        let expected_2 = "firstname".to_string();
        assert_eq!(expected_2, result_2);
        assert!(!is_masked_2);

        // testing the second column
        let (result_3, is_masked_3) = masking_value_for_column(relid, 3, anon.clone()).unwrap();
        let expected_3 = "CAST(NULL AS text)".to_string();
        assert!(is_masked_3);
        assert_eq!(expected_3, result_3);
    }

    #[pg_test]
    fn test_masking_expressions() {
        let relid = fixture::create_table_person();
        let (result, masked) = masking_expressions(relid, ANON_DEFAULT_MASKING_POLICY.to_string());
        let expected = "firstname AS firstname, CAST(NULL AS text) AS lastname".to_string();
        assert!(masked);
        assert_eq!(expected, result);

        // now with a non-existing policy
        let (result2, masked2) = masking_expressions(relid, "".to_string());
        assert!(!masked2);
        let expected2 = "firstname AS firstname, lastname AS lastname".to_string();
        assert_eq!(expected2, result2);
    }

    #[pg_test]
    fn test_subquery_some() {
        let relid = fixture::create_table_person();
        let result = subquery(relid, true, ANON_DEFAULT_MASKING_POLICY.to_string());
        assert!(result.is_some());
        assert!(result.clone().unwrap().contains("firstname"));
        assert!(result.clone().unwrap().contains("lastname"));
        let another_policy = "does_not_exist".to_string();
        let result_in_another_policy = subquery(relid, true, another_policy);
        assert!(result_in_another_policy.is_none());
    }

    #[pg_test]
    fn test_subquery_none() {
        let relid = fixture::create_table_call();
        let result = subquery(relid, true, ANON_DEFAULT_MASKING_POLICY.to_string());
        assert!(result.is_none());
    }

    #[pg_test]
    fn test_subquery_only() {
        let relid = fixture::create_table_person();
        let result = subquery(relid, false, ANON_DEFAULT_MASKING_POLICY.to_string());
        assert!(result.clone().unwrap().contains("FROM ONLY "))
    }

    #[pg_test]
    fn test_parse_subquery() {
        let relid = fixture::create_table_person();
        let subquery = subquery(relid, true, ANON_DEFAULT_MASKING_POLICY.to_string());
        let raw_stmt = parse_subquery(subquery.clone().unwrap());
        let result = unsafe { pgrx::nodes::node_to_string(raw_stmt.stmt).unwrap() };
        assert!(result.contains("firstname"));
        assert!(result.contains("lastname"));
    }

    #[pg_test]
    fn test_value_for_att() {
        // Create a table
        let relid = fixture::create_table_person();
        let relation = unsafe { PgRelation::with_lock(relid, pg_sys::AccessShareLock as i32) };
        let when = Table::get_when(relid, "anon");
        let desc = relation.tuple_desc();

        let att_dropped = desc.get(0).unwrap();
        let att_firstname = desc.get(1).unwrap();
        let att_lastname = desc.get(2).unwrap();

        let (val1, masked1) = value_for_att(&relation, att_firstname, when.clone(), "anon".into());
        assert_eq!(val1, "firstname");
        assert!(!masked1);

        let (val2, masked2) = value_for_att(
            &relation,
            att_firstname,
            when.clone(),
            "does_not_exists".into(),
        );
        assert_eq!(val2, "firstname");
        assert!(!masked2);

        let (val3, masked3) = value_for_att(&relation, att_lastname, when.clone(), "anon".into());
        assert_eq!(val3, "CAST(NULL AS text)");
        assert!(masked3);

        let (val4, masked4) = value_for_att(
            &relation,
            att_lastname,
            when.clone(),
            "does_not_exists".into(),
        );
        assert_eq!(val4, "lastname");
        assert!(!masked4);

        let (val5, masked5) = value_for_att(&relation, att_dropped, when.clone(), "anon".into());
        assert_eq!(val5, "\"........pg.dropped.1........\"");
        assert!(!masked5);

        let (val6, masked6) =
            value_for_att(&relation, att_lastname, Some("1=1".into()), "anon".into());
        assert_eq!(
            val6,
            "CASE WHEN CAST(1=1 AS BOOL) THEN CAST(NULL AS text) ELSE lastname END"
        );
        assert!(masked6);
    }

    #[pg_test]
    fn test_value_for_att_when() {
        // Create a table
        let relid = fixture::create_table_account();
        let relation = unsafe { PgRelation::with_lock(relid, pg_sys::AccessShareLock as i32) };
        let when = Table::get_when(relid, "anon");
        let desc = relation.tuple_desc();

        let att_email = desc.get(0).unwrap();
        let att_login = desc.get(1).unwrap();

        let (val1, masked1) = value_for_att(&relation, att_email, when.clone(), "anon".into());
        assert_eq!(
            val1,
            "CASE WHEN CAST(NOT is_admin AS BOOL) THEN CAST(anon.fake_email() AS text) ELSE email END"
        );
        assert!(masked1);

        let (val2, masked2) = value_for_att(&relation, att_login, when.clone(), "anon".into());
        assert_eq!(val2, "login");
        assert!(!masked2);
    }

    #[pg_test]
    fn test_value_for_att_with_quotes() {
        // Create a table
        let relid = fixture::create_table_user();
        let relation = unsafe { PgRelation::with_lock(relid, pg_sys::AccessShareLock as i32) };
        let when = Table::get_when(relid, "anon");
        let desc = relation.tuple_desc();

        let att_email = desc.get(0).unwrap();
        let att_login = desc.get(1).unwrap();

        let (val1, masked1) = value_for_att(&relation, att_email, when.clone(), "anon".into());
        assert_eq!(val1, "CAST(anon.fake_email() AS text)");
        assert!(masked1);

        let (val2, masked2) = value_for_att(&relation, att_login, when.clone(), "anon".into());
        assert_eq!(val2, "\"LoGiN\"");
        assert!(!masked2);
    }
}

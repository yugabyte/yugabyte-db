///
/// # Static Masking
///
use crate::error;
use crate::guc;
use crate::log;
use crate::masking;
use crate::utils;
use pgrx::PgRelation;
use pgrx::bgworkers::*;
use pgrx::prelude::*;
use std::ffi::CString;
use std::ffi::c_char;

/// Return the SQL assignment which will mask the data in a column
/// or null when no masking rule was found
///
fn column_assignment(relid: pg_sys::Oid, colname: String, policy: String) -> Option<String> {
    let attname_cstr = CString::new(colname.clone()).unwrap();
    let attnum = unsafe { pg_sys::get_attnum(relid, attname_cstr.as_ptr() as *const c_char) };

    // ignore system columns
    if attnum < 1 {
        return None;
    }

    let (masking_filter, att_is_masked) =
        masking::masking_value_for_column(relid, attnum.into(), policy)?;

    if !att_is_masked {
        return None;
    }

    Some(format!("{colname:?} = {masking_filter}"))
}

/// Return the SQL assignments which will mask the data in a table
///
fn table_assignments(relid: pg_sys::Oid, policy: String) -> Option<String> {
    // SAFETY: `pg_sys::relation_open()` will raise XX000 if the specified oid
    // isn't a valid relation
    let relation = unsafe { PgRelation::with_lock(relid, pg_sys::AccessShareLock as i32) };

    let mut assignments = Vec::new();
    for attribute in relation.tuple_desc().iter() {
        if attribute.attisdropped {
            continue;
        }

        // We do not want the WHEN clause here, it will be treated as an UPDATE .. WHERE instead
        let (filter_value, att_is_masked) =
            masking::value_for_att(&relation, attribute, None, policy.clone());

        if att_is_masked {
            assignments.push(format!(
                "{:?} = {}",
                name_data_to_str(&attribute.attname),
                filter_value
            ));
        }
    }

    if assignments.is_empty() {
        return None;
    }
    Some(assignments.join(", ").to_string())
}

/// Apply a masking policy to a column
pub fn anonymize_column(relid: pg_sys::Oid, colname: String, policy: String) -> Option<bool> {
    use crate::rule::table::Table;

    if !guc::ANON_STATIC_MASKING.get() {
        error::feature_not_enabled(
            "Static Masking",
            Some("Check the anon.static_masking parameter".to_string()),
        )
        .ereport();
    }

    // We can't apply a tablesample rules to just a column
    if Table::get_ratio(relid, &policy).is_some() {
        notice!(
            "The TABLESAMPLE rule will be ignored.
            Only anonymize_table() and anonymize_database() can apply sampling rules"
        );
    }

    let tablename = utils::get_relation_qualified_name(relid)?;

    let Some(assign) = column_assignment(relid, colname.clone(), policy.clone()) else {
        warning!(
            "There is no masking rule for column {:?} in table {}",
            colname.clone(),
            tablename
        );
        return Some(false);
    };

    let where_clause = match Table::get_when(relid, &policy) {
        Some(when) => format!("WHERE {when}"),
        None => "".to_string(),
    };

    let sql = format!(
        "
        SET CONSTRAINTS ALL DEFERRED;
        UPDATE {tablename}
        SET {assign}
        {where_clause};
    "
    );
    log::debug1!("Anon: {sql}");

    Spi::run(&sql).expect("Failed to anonymize column");

    Some(true)
}

/// Apply a masking policy to a relation
pub fn anonymize_table(relid: pg_sys::Oid, policy: String) -> Option<bool> {
    use crate::rule::table::Table;

    if !guc::ANON_STATIC_MASKING.get() {
        error::feature_not_enabled(
            "Static Masking",
            Some("Check the anon.static_masking parameter".to_string()),
        )
        .ereport();
    }

    let p = policy.clone();
    let ratio = Table::get_ratio(relid, &p);
    let tablename = utils::get_relation_qualified_name(relid)?;

    let sql: String = if ratio.is_some() {
        // If there's a tablesample ratio then we can't simply update the table.
        // we have to rewrite it completely.
        //
        // /!\ If the table has a foreign key, this will likely fail
        //
        let Some(masking_subquery) = masking::subquery(relid, false, policy) else {
            return Some(false);
        };
        let relint: u32 = relid.into();

        use fake::{Fake, Faker};
        let swap_table = format!("anon_swap_{relint}_{}", Faker.fake::<u32>());

        format!(
            "
            CREATE TEMPORARY TABLE {swap_table}
                AS {masking_subquery};
            TRUNCATE TABLE {tablename};
            INSERT INTO {tablename} SELECT * FROM {swap_table};
            DROP TABLE {swap_table};
        "
        )
    } else {
        let where_clause = match Table::get_when(relid, &policy) {
            Some(when) => format!("WHERE {when}"),
            None => "".to_string(),
        };

        // For compatibility with version 1, instead of returning `Some(false)`
        // we return None/NULL when no rule is found for the table
        //
        let masking_assignments = table_assignments(relid, policy)?;
        format!(
            "
            SET CONSTRAINTS ALL DEFERRED;
            UPDATE {tablename}
            SET {masking_assignments}
            {where_clause}
        "
        )
    };

    log::debug1!("Anon: {sql}");
    Spi::run(&sql).expect("Failed to anonymize table");

    Some(true)
}

pub fn dispatch_tables_oid(
    group_count: i32,
    group_id: i32,
    oids: &[pg_sys::Oid],
) -> spi::Result<Option<String>> {
    let query = "SELECT list_oid FROM anon.dispatch($1, $3) WHERE group_id=$2";
    let oid_array: Option<Vec<pg_sys::Oid>> = Spi::connect(|client| {
        client
            .select(
                query,
                None,
                &[group_count.into(), group_id.into(), oids.into()],
            )?
            .first()
            .get_one()
    })?;

    // Convert OID array to comma-separated string
    Ok(oid_array.and_then(|oids| {
        if oids.is_empty() {
            None
        } else {
            Some(
                oids.iter()
                    .map(|oid| oid.to_string())
                    .collect::<Vec<String>>()
                    .join(","),
            )
        }
    }))
}

/// Helper to get all OIDs from pg_masking_rules
pub fn get_masked_table_oids() -> Result<Vec<pg_sys::Oid>, spi::Error> {
    let query = "SELECT DISTINCT attrelid FROM anon.pg_masking_rules";
    Spi::connect(|client| {
        let mut oids = Vec::new();
        for row in client.select(query, None, &[])? {
            let oid_opt = row.get::<pg_sys::Oid>(1)?;
            if let Some(oid) = oid_opt {
                oids.push(oid);
            }
        }
        Ok(oids)
    })
}

// The function is the dbgw function. It iterates over the list of table OIDs.
// The oid list is given through the get_extra() function.
// For all in each list, it executes a transaction using BackgroundWorker::transaction.
// The pid is used to signal its ending to the main caller process.
#[pg_guard]
#[unsafe(no_mangle)]
pub extern "C-unwind" fn bgw_anon(pid: pg_sys::Datum) {
    let _pid = unsafe { i32::from_polymorphic_datum(pid, false, pg_sys::INT4OID) }
        .expect("This argument was set at worker invocation");
    let worker_data = BackgroundWorker::get_extra();
    let parts: Vec<&str> = worker_data.splitn(2, '|').collect();
    let (database_name, table_list) = { (parts[0], parts[1]) };
    let table_oids: Vec<&str> = table_list.split(',').collect();

    BackgroundWorker::connect_worker_to_spi(Some(database_name), None);

    info!(
        "Background worker {} (PID: {:?}) started with tables: {:?}",
        BackgroundWorker::get_name(),
        _pid,
        table_list
    );
    info!(
        "Background worker {} (PID: {:?}) started with tables oids: {:?}",
        BackgroundWorker::get_name(),
        _pid,
        table_oids
    );

    // Start a single transaction for all tables in the list.
    let result = BackgroundWorker::transaction(|| {
        Spi::connect_mut(|client| {
            // Set all deferrable constraints to be checked at the end of the transaction.
            client.update("SET CONSTRAINTS ALL DEFERRED", None, &[])?;
            Ok::<(), pgrx::spi::Error>(())
        })?;

        for table_str in table_oids.iter() {
            if table_str.trim().is_empty() {
                continue;
            }

            let table_id = match table_str.trim().parse::<i32>() {
                Ok(id) => id,
                Err(e) => {
                    error!("Invalid table ID '{}': {}", table_str, e);
                }
            };

            let table_oid: pg_sys::Oid = pg_sys::Oid::from(table_id as u32);

            let table_name = match utils::get_relation_qualified_name(table_oid) {
                Some(name) => name,
                None => {
                    warning!("Could not get table name for OID {}, skipping", table_oid);
                    continue;
                }
            };

            info!(
                "Worker {} processing table: {} (OID: {})",
                BackgroundWorker::get_name(),
                table_name,
                table_oid
            );

            // Anonymize the table directly using anonymize_table function
            match anonymize_table(table_oid, "anon".to_string()) {
                Some(true) => {
                    info!("Successfully anonymized table: {}", table_name);
                }
                Some(false) => {
                    error!("Failed to anonymize table: {}", table_name);
                }
                None => {
                    warning!("No masking rules found for table: {}", table_name);
                    // Continue processing other tables even if this one has no rules
                }
            }
        }

        Ok::<(), pgrx::spi::Error>(())
    });

    // Handle the result and exit with appropriate code
    match result {
        Ok(_) => {
            info!(
                "Worker {} completed successfully",
                BackgroundWorker::get_name()
            );
            std::process::exit(0);
        }
        Err(e) => {
            error!("Worker {} failed: {}", BackgroundWorker::get_name(), e);
        }
    }
    // No code should follow std::process::exit()
}

//----------------------------------------------------------------------------
// Tests
//----------------------------------------------------------------------------

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::fixture;
    use crate::label_providers::ANON_DEFAULT_MASKING_POLICY;
    use crate::static_masking::*;

    /// Helper function to validate that a result contains valid OID values without NULLs
    fn assert_valid_oid_string(oids_str: &str) {
        assert!(
            !oids_str.contains("NULL"),
            "Result should not contain NULL values"
        );
        assert!(!oids_str.is_empty(), "Result should not be empty");
        // Verify all comma-separated values are valid numeric OIDs
        let oid_parts: Vec<&str> = oids_str.split(',').collect();
        assert!(!oid_parts.is_empty(), "Should have at least one OID");
        for oid_part in oid_parts {
            let trimmed = oid_part.trim();
            assert!(!trimmed.is_empty(), "OID should not be empty");
            assert!(
                trimmed.parse::<u32>().is_ok(),
                "OID '{}' should be a valid number",
                trimmed
            );
        }
    }

    #[pg_test]
    fn test_column_assignment() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_person();
        assert_eq!(
            None,
            column_assignment(relid, "firstname".to_string(), anon.clone())
        );
        assert_eq!(
            Some("\"lastname\" = CAST(NULL AS text)".to_string()),
            column_assignment(relid, "lastname".to_string(), anon.clone())
        );
    }

    #[pg_test]
    fn test_column_assignment_with_quotes() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_user();
        assert_eq!(
            column_assignment(relid, "Email".to_string(), anon),
            Some("\"Email\" = CAST(anon.fake_email() AS text)".into())
        );
    }

    #[pg_test]
    fn test_table_assignments() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_person();
        assert_eq!(table_assignments(relid, "does_not_exits".into()), None);
        assert_eq!(
            table_assignments(relid, anon.clone()),
            Some("\"lastname\" = CAST(NULL AS text)".into())
        );
    }

    #[pg_test]
    fn test_table_assignments_with_quotes() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_user();
        assert_eq!(
            table_assignments(relid, anon),
            Some("\"Email\" = CAST(anon.fake_email() AS text)".into())
        );
    }

    #[pg_test]
    fn test_anonymize_column() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_person();
        assert_eq!(
            Some(false),
            anonymize_column(relid, "firstname".to_string(), anon.clone())
        );
        assert_eq!(
            Some(true),
            anonymize_column(relid, "lastname".to_string(), anon.clone())
        );
    }

    #[pg_test(error = "Anon: Static Masking is not enabled")]
    fn test_anonymize_column_not_enabled() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_person();
        fixture::disable_static_masking();
        anonymize_column(relid, "lastname".to_string(), anon);
    }

    #[pg_test]
    fn test_anonymize_column_no_policy() {
        let policy = "does_not_exist".to_string();
        let relid = fixture::create_table_person();
        assert_eq!(
            Some(false),
            anonymize_column(relid, "firstname".to_string(), policy.clone())
        );
        assert_eq!(
            Some(false),
            anonymize_column(relid, "lastname".to_string(), policy.clone())
        );
    }

    #[pg_test]
    fn test_anonymize_column_does_not_exist() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_person();
        assert_eq!(
            Some(false),
            anonymize_column(relid, "does_not_exists".to_string(), anon.clone())
        );
        assert_eq!(
            Some(false),
            anonymize_column(relid, "".to_string(), anon.clone())
        );
    }

    #[pg_test]
    fn test_anonymize_column_invalid_oid() {
        assert_eq!(
            None,
            anonymize_column(pg_sys::InvalidOid, "".to_string(), "anon".to_string())
        );
    }

    #[pg_test]
    fn test_anonymize_table() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_person();
        assert_eq!(Some(true), anonymize_table(relid, anon.clone()));
    }

    #[pg_test(error = "Anon: Static Masking is not enabled")]
    fn test_anonymize_table_not_enabled() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_person();
        fixture::disable_static_masking();
        anonymize_table(relid, anon);
    }

    #[pg_test]
    fn test_anonymize_table_does_not_exist() {
        assert_eq!(
            None,
            anonymize_table(pg_sys::InvalidOid, "anon".to_string())
        );
    }

    #[pg_test]
    fn test_anonymize_table_no_policy() {
        let relid = fixture::create_table_person();
        assert_eq!(None, anonymize_table(relid, "does_not_exist".to_string()));
    }

    #[pg_test]
    fn test_anonymize_table_no_rules() {
        let anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let relid = fixture::create_table_location();
        assert_eq!(None, anonymize_table(relid, anon.clone()));
    }

    #[pg_test]
    fn test_get_tables_oid_should_exists() {
        //given 2 tables person and user with masking rules
        //when dispatch(2,[oids]) is called
        //then it should return a list of OIDs for each group
        //because  person and User are not linked together by a masked key
        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_table_person();
        let _relid2 = fixture::create_table_user();
        let oids = get_masked_table_oids().unwrap();

        let result = dispatch_tables_oid(2, 1, &oids).unwrap();

        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 1,
                "Expected 1 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(2, 2, &oids).unwrap();

        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 1,
                "Expected 1 OIDs in group 2, got {}",
                oid_count
            );
        }

        //if njob = 3 and the number of tables to anonymize is 2
        //then it should return an empty list for the last job
        let result = dispatch_tables_oid(3, 3, &oids);
        assert!(
            result.is_err(),
            "Expected get_tables_oid to return an error"
        );
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_tables_oid_should_not_exists() {
        //given a table without masking rules
        //when dispatch is called
        //then it should return an empty list
        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_table_with_defaults();

        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(2, 2, &oids);
        assert!(
            result.is_err(),
            "Expected get_tables_oid to return an error"
        );
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_parent_tables_oid_should_exists() {
        //given a table orders and its child orders_details with masking rules
        //when dispatch is called, with 2 parallel jobs
        //then all tables should be dispatch in all groups.
        //because even if both tables are linked through foreign keys
        //the keys are not masked

        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_table_orders_with_masked_column();
        let _relid2 = fixture::create_table_orders_details_with_masked_column();
        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(2, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 1,
                "Expected 1 OIDs in group 2, got {}",
                oid_count
            );
        }
        let result = dispatch_tables_oid(2, 2, &[_relid1, _relid2]).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 1,
                "Expected 1 OIDs in group 2, got {}",
                oid_count
            );
        }
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_parent_only_tables_oid_should_exists() {
        //given a table orders only with masking rules, and tble person with masking rules
        //when dispatch is called, with 2 parallel jobs
        //then both groups are filled.
        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_table_orders_with_masked_column();
        let _relid2 = fixture::create_table_orders_details_without_masked_column();
        let _relid3 = fixture::create_table_person();
        let oids = get_masked_table_oids().unwrap();

        let result = dispatch_tables_oid(2, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 1,
                "Expected 1 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(2, 2, &[_relid1, _relid2, _relid3]).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 1,
                "Expected 1 OIDs in group 2, got {}",
                oid_count
            );
        }
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_parent_and_child_only_tables_oid_should_exists() {
        //given a table employee only with masking rules, and table call_history both with masking rules
        //tables are linked through foreign keys when dispatch is called, with 2 parallel jobs
        //then one group is filled.
        //because both tables are linked through foreign keys and the key is a masked columns (phone_number)

        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_table_employee_with_masked_column();
        let _relid2 = fixture::create_table_call_history_with_masked_column();
        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(2, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 2,
                "Expected 2 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(2, 2, &[_relid1, _relid2]);
        assert!(
            result.is_err(),
            "Expected dispatch_tables_oid to return an error"
        );
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_self_joined_tables_oid_should_exists() {
        //given a table employee with masking rules, and a self join on employee_id
        //given a table call_history also with masking rules
        //tables are not linked together, so when dispatch is called, with 2 parallel jobs
        //then 2 groups are filled.
        //dispatch_tables_oid(2,1) should return a list of OIDs
        //dispatch_tables_oid(2,2) should return a list of OIDs
        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_table_employee_with_self_join_and_masked_column();
        let _relid2 = fixture::create_table_call_history_without_fk_with_masked_column();
        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(2, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 1,
                "Expected 1 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(2, 2, &[_relid1, _relid2]).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 1,
                "Expected 1 OIDs in group 2, got {}",
                oid_count
            );
        }

        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_10_linked_tables_and_one_table_alone_should_exists() {
        //given 10  tables customers,products,orders,order_items, with masking rules,
        //given a table employee not linked to any other table with masking rules
        //when get_tables_oid is called, with 5 parallel jobs
        //then 4 groups are filled, all linked tables in the same group, and the employee table in an other group.
        //get_tables_oid(5,1) should return a list of OIDs without NULL values
        //get_tables_oid(5,2) should return a list of OIDs without NULL values
        //get_tables_oid(5,3) should return a list of OIDs without NULL values
        //get_tables_oid(5,4) should return a list of OIDs without NULL values
        //get_tables_oid(5,5) should return a list of OIDs without NULL values
        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_table_employee_with_self_join_and_masked_column();
        let _relid2 = fixture::create_4_linked_tables_and_masked_column();
        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(5, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
        }

        let result = dispatch_tables_oid(5, 2, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
        }

        let result = dispatch_tables_oid(5, 3, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
        }
        let result = dispatch_tables_oid(5, 4, &oids).unwrap();
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
        }
        let result = dispatch_tables_oid(5, 5, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
        }
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_10_linked_tables_by_masked_fk_2_groups() {
        //given 8 linked tables with masking rules on fk,
        // 2 tables with masking rules but not linked,
        //then linked tables with their fk anon should be in the same group.

        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_10_tables_with_fk_masked();

        // i want ot be sure that for 2 groups, tables are well distributed
        //          group_id |                tables                | sum_rows
        // ----------+--------------------------------------+----------
        //         1 | {235763,235770,235782,235794,235806...} |      320
        //         2 | {235811,235842} |      320
        // (2 rows)
        // in group 1 i expect 8 tables and oid are valid
        // in group 2 i expect 2 tables and oid are valid
        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(2, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 8,
                "Expected 8 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(2, 2, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 2,
                "Expected 2 OIDs in group 2, got {}",
                oid_count
            );
        }
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_10_linked_tables_by_masked_fk_3_groups() {
        //given 8 linked tables with masking rules on fk,
        // 2 tables with masking rules but not linked,
        //then linked tables with their fk anon should be in the same group.

        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_10_tables_with_fk_masked();

        // for 3 groups
        //  group_id |                tables                | sum_rows
        // ----------+--------------------------------------+----------
        //         1 | {235763,235770,235782,235794,235854} |      320
        //         2 | {235811,235818,235830,235842}        |      220
        //         3 | {235806}                             |      100
        // (3 rows)
        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(3, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 8,
                "Expected 8 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(3, 2, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 2, got {}", oid_count);
        }
        let result = dispatch_tables_oid(3, 3, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 3, got {}", oid_count);
        }
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_10_linked_tables_by_masked_fk_4_groups() {
        // given 8 linked tables with masking rules on fk,
        // 2 tables with masking rules but not linked,
        //then linked tables with their fk anon should be in the same group.
        //         test=# with c1 as (
        //     SELECT array_agg(attrelid) as array_agg FROM anon.pg_masking_rules
        //     )
        // select anon.dispatch(4,c1.array_agg) from c1;
        //             dispatch
        // ---------------------------------
        //  (1,"{33948,33955,33967,33979}")
        //  (2,"{33996,34003,34015,34027}")
        //  (3,{33991})
        //  (4,{34039})
        // (4 rows)

        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_10_tables_with_fk_masked();

        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(4, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 4,
                "Expected 4 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(4, 2, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 4,
                "Expected 4 OIDs in group 2, got {}",
                oid_count
            );
        }
        let result = dispatch_tables_oid(4, 3, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 3, got {}", oid_count);
        }
        let result = dispatch_tables_oid(4, 4, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 4, got {}", oid_count);
        }
    }

    #[pg_test]
    fn test_get_10_linked_tables_by_masked_fk_5_groups() {
        //given 10 linked tables with masking rules on fk,
        //then linked tables should be in the same group.
        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_10_tables_with_fk_masked();

        // for 5 groups only 4 exists. so the last one should return an error
        //  group_id |            tables             | sum_rows
        // ----------+-------------------------------+----------
        //         1 | {235763,235770,235782,235794} |      220
        //         2 | {235811,235818,235830,235842} |      220
        //         3 | {235806}                      |      100
        //         4 | {235854}                      |      100
        // (4 rows)
        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(5, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 4,
                "Expected 4 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(5, 2, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 4,
                "Expected 4 OIDs in group 2, got {}",
                oid_count
            );
        }
        let result = dispatch_tables_oid(5, 3, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 3, got {}", oid_count);
        }
        let result = dispatch_tables_oid(5, 4, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 4, got {}", oid_count);
        }
        let result = dispatch_tables_oid(5, 5, &oids);
        assert!(
            result.is_err(),
            "Expected dispatch_tables_oid(5, 5, &oids) to return an error"
        );
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_5_linked_tables_but_fk_is_not_masked_2_groups() {
        //given 5 linked tables without masking rules on fk, but on other columns (t),
        //then linked tables should not be in the same group.
        // i want ot be sure that for 2 groups, tables are well distributed

        // (2 rows)        //  group_id |         tables         | sum_rows
        // ----------+------------------------+----------
        //         1 | {237648,237679,237691} |      210
        //         2 | {237655,237667}        |      110

        // in group 1 i expect 3 tables and oid are valid
        // in group 2 i expect 2 tables and oid are valid

        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_5_tables_with_fk_not_masked();
        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(2, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 3,
                "Expected 3 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(2, 2, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 2,
                "Expected 2 OIDs in group 2, got {}",
                oid_count
            );
        }
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_5_linked_tables_but_fk_is_not_masked_3_groups() {
        //given 5 linked tables without masking rules on fk, but on other columns (t),
        //then linked tables should not be in the same group.

        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_5_tables_with_fk_not_masked();
        let oids = get_masked_table_oids().unwrap();

        // i want to be sure that for 3 groups, tables are well distributed
        //  group_id |     tables      | sum_rows
        // ----------+-----------------+----------
        //         1 | {237648,237655} |      110
        //         2 | {237667,237679} |      110
        //         3 | {237691}        |      100

        // in group 1 i expect 2 tables and oid are valid
        // in group 2 i expect 2 tables and oid are valid
        // in group 3 i expect 1 table and oid are valid
        let result = dispatch_tables_oid(3, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 2,
                "Expected 2 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(3, 2, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 2,
                "Expected 2 OIDs in group 2, got {}",
                oid_count
            );
        }
        let result = dispatch_tables_oid(3, 3, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 3, got {}", oid_count);
        }
        fixture::clean_tables();
    }

    #[pg_test]
    fn test_get_5_linked_tables_with_cycling_fk() {
        //given 5 linked tables with cycling foreign keys with masking rules on fk,
        //then linked tables with their fk anon should be in the same group.

        let _anon = ANON_DEFAULT_MASKING_POLICY.to_string();
        let _relid1 = fixture::create_5_tables_with_fk_not_anon_but_with_cycling_fk();

        // test=# with c1 as (
        //     SELECT array_agg(attrelid) as array_agg FROM anon.pg_masking_rules
        //     )
        // select anon.dispatch(4,c1.array_agg) from c1;
        //       dispatch
        // ---------------------
        //  (1,"{34548,34555}")
        //  (2,{34567})
        //  (3,{34579})
        //  (4,{34596})
        // (4 rows)

        let oids = get_masked_table_oids().unwrap();
        let result = dispatch_tables_oid(4, 1, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(
                oid_count, 2,
                "Expected 2 OIDs in group 1, got {}",
                oid_count
            );
        }

        let result = dispatch_tables_oid(4, 2, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 2, got {}", oid_count);
        }

        let result = dispatch_tables_oid(4, 3, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 3, got {}", oid_count);
        }

        let result = dispatch_tables_oid(4, 4, &oids).unwrap();
        assert!(result.is_some());
        if let Some(ref oids_str) = result {
            assert_valid_oid_string(oids_str);
            let oid_count = oids_str.split(',').filter(|s| !s.trim().is_empty()).count();
            assert_eq!(oid_count, 1, "Expected 1 OID in group 4, got {}", oid_count);
        }
        fixture::clean_tables();
    }
}

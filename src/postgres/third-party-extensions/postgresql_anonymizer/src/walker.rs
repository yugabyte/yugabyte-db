///
/// # Walker module
///
/// This module contains recursive function called "walkers" that will go
/// through a Postgres Tree object ( either a PlannerStmt or a Query )
///
/// ZomboDB is the main inspiration for this module
/// https://github.com/zombodb/zombodb/blob/v3000.2.5/src/walker/mod.rs
///
use crate::compat;
use crate::error;
use crate::input;
use crate::label_providers;
use crate::log;
use crate::masking;
use crate::utils;
use pgrx::*;
use std::ffi::CString;
use std::ffi::c_char;

///
/// TreeWalker is the context object that will passed along at each stage
/// of the recursive walks. We use it to carry information such as the masking
/// policy name or store an error report
///
pub struct TreeWalker {
    policy: String,
    pub reason: Option<input::Reason>,
}

impl TreeWalker {
    pub fn new(policy: String) -> Self {
        TreeWalker {
            policy,
            reason: None,
        }
    }

    pub fn empty() -> Self {
        TreeWalker {
            policy: "".to_string(),
            reason: None,
        }
    }

    pub unsafe fn has_restricted_function(&mut self, query: &PgBox<pg_sys::Query>) -> bool {
        if query.is_null() {
            return false;
        }

        log::debug1!("query= {:#?}", query);

        unsafe {
            pg_sys::query_tree_walker(
                query.as_ptr(),
                Some(has_restricted_function_walker),
                self as *mut TreeWalker as void_mut_ptr,
                pg_sys::QTW_EXAMINE_RTES as i32,
            )
        }
    }
    pub unsafe fn is_untrusted(&mut self, node: &PgBox<pg_sys::Node>) -> bool {
        // Calling raw_expression_tree_walker() directly here would skip the
        // first node of the tree... Instead we call the walker function
        // directly
        is_untrusted_walker(node.as_ptr(), self as *mut TreeWalker as void_mut_ptr)
    }

    pub unsafe fn rewrite(&mut self, query: &PgBox<pg_sys::Query>) -> bool {
        if query.is_null() {
            return false;
        }
        unsafe {
            pg_sys::query_tree_walker(
                query.as_ptr(),
                Some(rewrite_walker),
                self as *mut TreeWalker as void_mut_ptr,
                pg_sys::QTW_EXAMINE_RTES as i32,
            )
        }
    }
}

/// Recurvive walk through a Query and check there's a func_call to
/// a restricted function.
///
#[pg_guard]
unsafe extern "C-unwind" fn has_restricted_function_walker(
    node: *mut pg_sys::Node,
    context_ptr: *mut ::core::ffi::c_void,
) -> bool {
    use crate::rule::function::Function;

    if node.is_null() {
        return false;
    }

    if unsafe { is_a(node, pg_sys::NodeTag::T_FuncExpr) } {
        let funcexpr = unsafe { PgBox::from_pg(node as *mut pg_sys::FuncExpr) };
        if Function::is_restricted_function(
            funcexpr.funcid,
            label_providers::ANON_DEFAULT_MASKING_POLICY,
        ) {
            return true;
        }
    } else if unsafe { is_a(node, pg_sys::NodeTag::T_CommonTableExpr) } {
        let cte = unsafe { PgBox::from_pg(node as *mut pg_sys::CommonTableExpr) };

        if cte.ctequery.is_null() {
            return false;
        }

        return unsafe {
            pg_sys::query_tree_walker(
                cte.ctequery as *mut pg_sys::Query,
                Some(has_restricted_function_walker),
                context_ptr,
                pg_sys::QTW_EXAMINE_RTES as i32,
            )
        };
    } else if unsafe { is_a(node, pg_sys::NodeTag::T_RangeTblEntry) } {
        let rte = unsafe { PgBox::from_pg(node as *mut pg_sys::RangeTblEntry) };

        if rte.relid != 0.into() {
            return false;
        }

        if rte.subquery.is_null() {
            return false;
        }

        return unsafe {
            pg_sys::query_tree_walker(
                rte.subquery,
                Some(has_restricted_function_walker),
                context_ptr,
                pg_sys::QTW_EXAMINE_RTES as i32,
            )
        };
    }

    unsafe {
        pg_sys::expression_tree_walker(node, Some(has_restricted_function_walker), context_ptr)
    }
}

/// Recurvive walk through a Raw Expression ( a FuncCall ) and check that all
/// functions are TRUSTED for anonymization
///
/// The walker should not return true without defining an input::Reason in
/// the context
///
#[pg_guard]
extern "C-unwind" fn is_untrusted_walker(
    node: *mut pg_sys::Node,
    context_ptr: *mut ::core::ffi::c_void,
) -> bool {
    if node.is_null() {
        return false;
    }

    // Fetch and cast the context
    let mut context = unsafe { PgBox::<TreeWalker>::from_pg(context_ptr as *mut TreeWalker) };

    if unsafe { pgrx::is_a(node, pg_sys::NodeTag::T_FuncCall) } {
        let fc = unsafe { PgBox::from_pg(node as *mut pg_sys::FuncCall) };

        // fc.funcname is a pointer to a pg_sys::List
        let funcname = unsafe { PgBox::from_pg(fc.funcname) };

        // if the function name is not qualified, we can't trust it
        if funcname.length != 2 {
            context.reason = Some(input::Reason::FunctionUnqualified);
            return true;
        }

        // Now we know the function name is qualified,
        // the first element of the list is the schema name
        let schema_val = unsafe {
            PgBox::from_pg(pg_sys::list_nth(funcname.as_ptr(), 0) as *mut compat::SchemaValue)
        };
        let schema_c_ptr = unsafe { compat::strVal(*schema_val) };

        let namespaceId = unsafe { pg_sys::get_namespace_oid(schema_c_ptr, false) };

        let name_val = unsafe {
            PgBox::from_pg(pg_sys::list_nth(funcname.as_ptr(), 1) as *mut compat::SchemaValue)
        };
        let name_c_ptr = unsafe { compat::strVal(*name_val) };

        // Returning true will stop the tree walker right away
        // So the logic is inverted: we stop the search once an unstrusted
        // function is found.
        if let Err(error) = input::is_trusted_function(namespaceId, name_c_ptr, &context.policy) {
            context.reason = Some(error);
            return true;
        }
    }

    unsafe { pg_sys::raw_expression_tree_walker(node, Some(is_untrusted_walker), context_ptr) }
}

/// Resurively walk through a Query tree and replace each masked relation with
/// its "Masking SubQuery" (msq)
///
#[pg_guard]
unsafe extern "C-unwind" fn rewrite_walker(
    node: *mut pg_sys::Node,
    context_ptr: void_mut_ptr,
) -> bool {
    if node.is_null() {
        return false;
    }

    let context = unsafe { PgBox::<TreeWalker>::from_pg(context_ptr as *mut TreeWalker) };
    let policy = context.policy.clone();

    if unsafe { is_a(node, pg_sys::NodeTag::T_RangeTblEntry) } {
        // The node is a Range Table Entry
        let mut rte = unsafe { PgBox::from_pg(node as *mut pg_sys::RangeTblEntry) };
        log::debug1!("rte= {:?}", rte);

        // We do not mask catalog relations
        if compat::IsCatalogRelationOid(rte.relid) {
            return false;
        }

        // We do not mask anon relations
        if utils::is_anon_relation_oid(rte.relid) {
            return false;
        }

        // This is a subquery, continue to the next node
        if rte.relid == 0.into() {
            return false;
        }

        // Create the Masking Sub Query (msq) that will replace the relation
        let msq_sql = masking::subquery(rte.relid, rte.inh, policy);

        // This table is not masked, skip to the next node
        if msq_sql.is_none() {
            return false;
        }

        log::debug1!("msq_sql= {}", msq_sql.clone().unwrap());

        // Create the Raw Statement from the SQL subquery
        let msq_raw_stmt = masking::parse_subquery(msq_sql.clone().unwrap());
        log::debug1!("msq_raw_stmt= {:?}", *msq_raw_stmt);

        // Create the Parse State with the SQL subquery within
        let mut msq_pstate = unsafe {
            PgBox::from_pg(pg_sys::make_parsestate(std::ptr::null_mut::<
                pg_sys::ParseState,
            >()))
        };
        let msq_sql_c_string = CString::new(msq_sql.clone().unwrap().as_str()).unwrap();
        let msq_sql_ptr = msq_sql_c_string.as_c_str().as_ptr() as *const c_char;
        msq_pstate.p_sourcetext = msq_sql_ptr;

        // Create the Query object
        // Calling parse_analyze_varparams(...) would trigger the
        // post_parse_analyze hook again and we'd stuck in an infinite loop
        let mut msq_query = unsafe {
            PgBox::from_pg(pg_sys::transformTopLevelStmt(
                msq_pstate.as_ptr(),
                msq_raw_stmt.as_ptr(),
            ))
        };

        //
        // QSRC_PARSER is not used anymore by Postgres.
        // This is convenient because we can use it as a marker to signal
        // that this query was generated by the extension, and thus it should
        // NOT be masked otherwise we'd trapped in an infinite loop
        //
        msq_query.querySource = pg_sys::QuerySource::QSRC_PARSER;
        log::debug1!("new_subquery= {:?}", *msq_query);

        //
        // Somehow later in the process, the optimizer will run a callback
        // named `pullup_replace_vars_callback` on each Range_Table_Entry
        // variable and use the original attribute number (attnum/resno) in
        // order to find the attribute.
        // Since we replace the original table with a masking subquery, there
        // may be some inconsistencies when the original tables contains a
        // dropped column.
        // For instance, if a table has 4 columns a,b,c,d and the c column was
        // dropped, then the attribute numbers of a,b,d would be 1,2,4.
        // But in the masking subquery the attribute numbers would be 1,2,3
        // This would lead to an error during the optimizer stage.
        //
        // We avoid this issue by assigning the attribute numbers of the
        // original table upon the columns of the masking subquery
        //
        let relation = unsafe { PgRelation::with_lock(rte.relid, pg_sys::AccessShareLock as i32) };

        // With example (a,b,d) then original_attnums would be {1,2,4}
        let mut original_attnums = vec![];
        for attribute in relation.tuple_desc().iter() {
            if !attribute.attisdropped {
                original_attnums.push(attribute.attnum)
            }
        }

        let target_list = unsafe { PgList::<pg_sys::TargetEntry>::from_pg(msq_query.targetList) };

        for (i, target_ptr) in target_list.iter_ptr().enumerate() {
            let mut target = unsafe { PgBox::<pg_sys::TargetEntry>::from_pg(target_ptr) };
            target.resno = original_attnums[i];
            target.into_pg();
        }

        // Do the substitution
        //pg_sys::AcquireRewriteLocks(msq_query.as_ptr(), true, false);
        rte.rtekind = pg_sys::RTEKind::RTE_SUBQUERY;
        rte.subquery = msq_query.as_ptr();
        rte.relid = pg_sys::InvalidOid;
        rte.relkind = 0;
        compat::rte_perminfo_index_disable!(rte);

        // We must set `rte.inh` to false, otherwise the volatile functions
        // are not executed
        rte.inh = false;

        // TODO apply the table sampling ratio
        // rte.tablesample = ....;

        // Return the modified RTE to Postgres
        rte.into_pg();

        return false;
    } else if unsafe { is_a(node, pg_sys::NodeTag::T_Query) } {
        let query = unsafe { PgBox::from_pg(node as *mut pg_sys::Query) };

        // The query is a masking subquery
        // we don't need to apply the masks on it !
        if query.querySource == pg_sys::QuerySource::QSRC_PARSER {
            return false;
        }
        // Continue parsing the tree
        return unsafe {
            pg_sys::query_tree_walker(
                node as *mut pg_sys::Query,
                Some(rewrite_walker),
                context_ptr,
                pg_sys::QTW_EXAMINE_RTES as i32,
            )
        };
    } else if unsafe { is_a(node, pg_sys::NodeTag::T_InsertStmt) }
        || unsafe { is_a(node, pg_sys::NodeTag::T_DeleteStmt) }
        || unsafe { is_a(node, pg_sys::NodeTag::T_UpdateStmt) }
        || unsafe { is_a(node, pg_sys::NodeTag::T_TruncateStmt) }
        || unsafe { is_a(node, pg_sys::NodeTag::T_CreateStmt) }
        || unsafe { is_a(node, pg_sys::NodeTag::T_DropStmt) }
        || unsafe { is_a(node, pg_sys::NodeTag::T_SecLabelStmt) }
    {
        error::insufficient_privilege("role is masked".to_string()).ereport();
    }

    unsafe { pg_sys::expression_tree_walker(node, Some(rewrite_walker), context_ptr) }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::fixture;
    use crate::input;
    use crate::walker::*;
    use pgrx::pg_sys::Node;
    use pgrx::pg_sys::Query;

    #[pg_test]
    fn test_has_restricted_function_null() {
        let mut walker = TreeWalker::new("".to_string());
        let null_node = pgrx::PgBox::<Query>::null();
        assert!(!unsafe { walker.has_restricted_function(&null_node) });
    }

    #[pg_test]
    fn test_has_restricted_function() {
        let mut walker = TreeWalker::empty();
        let pseudo_func = fixture::parse_select_query("SELECT anon.pseudo_city('bob'::TEXT)");
        assert!(unsafe { walker.has_restricted_function(&pseudo_func) });
        let not_a_pseudo_func = fixture::parse_select_query("SELECT anon.dummy_name()");
        assert!(!unsafe { walker.has_restricted_function(&not_a_pseudo_func) });
    }

    #[pg_test]
    fn test_has_restricted_function_unqualified() {
        fixture::set_search_path("public, anon".to_string());
        let pseudo_func = fixture::parse_select_query("SELECT pseudo_city('bob'::TEXT)");
        let mut walker = TreeWalker::empty();
        assert!(unsafe { walker.has_restricted_function(&pseudo_func) });
    }

    #[pg_test]
    fn test_is_untrusted_null() {
        let policy = "anon";
        let mut walker = TreeWalker::new(policy.to_string());
        let null_node = pgrx::PgBox::<Node>::null();
        assert!(!unsafe { walker.is_untrusted(&null_node) });
    }

    #[pg_test]
    fn test_is_unstrusted_ok() {
        let _outfit = fixture::create_masking_functions();
        let policy = "anon";
        let mask_func = input::parse_expression("outfit.mask(0)").unwrap();
        let mut walker = TreeWalker::new(policy.to_string());
        assert!(!unsafe { walker.is_untrusted(&mask_func) });
        assert_eq!(walker.reason, None);
    }

    #[pg_test]
    fn test_is_unstrusted_schema_not_trusted() {
        let _outfit = fixture::create_masking_functions();
        let policy = "anon";
        let cape_func = input::parse_expression("outfit.cape()").unwrap();
        let mut walker = TreeWalker::new(policy.to_string());
        assert!(unsafe { walker.is_untrusted(&cape_func) });
        assert_eq!(walker.reason, Some(input::Reason::SchemaNotTrusted));
    }

    #[pg_test]
    fn test_is_untrusted_function_unqualified() {
        let foo_func = input::parse_expression("foo()").unwrap();
        let mut walker = TreeWalker::new(String::from("anon"));
        assert!(unsafe { walker.is_untrusted(&foo_func) });
        assert_eq!(walker.reason, Some(input::Reason::FunctionUnqualified));
    }

    #[pg_test]
    fn test_is_untrusted_function_untrusted() {
        let _outfit = fixture::create_masking_functions();
        let policy = "anon";
        let belt_func = input::parse_expression("outfit.belt()").unwrap();
        let mut walker = TreeWalker::new(policy.to_string());
        assert!(unsafe { walker.is_untrusted(&belt_func) });
        assert_eq!(walker.reason, Some(input::Reason::FunctionUntrusted));
    }

    #[pg_test]
    fn test_rewrite_null() {
        let policy = "anon";
        let mut walker = TreeWalker::new(policy.to_string());
        let null_query = pgrx::PgBox::<Query>::null();
        assert!(!unsafe { walker.rewrite(&null_query) });
    }
}

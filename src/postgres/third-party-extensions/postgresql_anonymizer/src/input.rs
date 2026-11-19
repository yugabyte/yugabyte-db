use crate::error;
use crate::guc;
use crate::log;
use crate::macros;
use crate::walker;
use pgrx::prelude::*;
use std::ffi::CString;
use std::os::raw::c_char;

#[derive(Debug)]
enum InputError<'a> {
    Empty(),
    Invalid(&'a str),
    NotAFunction(&'a str),
    NotATrustedSchema(&'a str),
    Unqualified(&'a str),
    Untrusted(&'a str),
}

use InputError::*;

impl std::fmt::Display for InputError<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Empty() => write!(f, "Expression is empty"),

            Invalid(expr) => write!(f, "{expr} is not a valid expression"),

            NotAFunction(expr) => write!(f, "{expr} is not a valid function call"),

            NotATrustedSchema(expr) => write!(f, "{expr} does not belong in a TRUSTED schema"),

            Unqualified(expr) => write!(f, "{expr} is not qualified"),

            Untrusted(expr) => write!(f, "{expr} is UNTRUSTED"),
        }
    }
}

impl std::error::Error for InputError<'_> {}

///
/// The Reason enum describes a series of rules that will be enforces by
/// the input checks. It will be used by check functions to explain
/// why an input was rejected.
///
#[derive(PartialEq, Eq, Clone, Debug)]
pub enum Reason {
    SchemaNotTrusted,
    FunctionUntrusted,
    FunctionUnqualified,
}

/// check that an expression is a valid masking function
///
pub fn check_function(expr: &str, policy: &'static str) -> Result<(), String> {
    let Ok(func) = parse_expression(expr) else {
        return Err(NotAFunction(expr).to_string());
    };

    let valid_nodes = vec![pg_sys::NodeTag::T_FuncCall];

    if !check_node(&func, valid_nodes) {
        return Err(NotAFunction(expr).to_string());
    }

    if !guc::ANON_RESTRICT_TO_TRUSTED_SCHEMAS.get() {
        return Ok(());
    }

    // Walk through the parse tree and check that the function itself and
    // all other functions used as parameters belong to a trusted schema.
    // The goal is to block privilege escalation attacks using something like:
    //
    // `MASKED WITH FUNCTION pg_catalog.upper(public.elevate())`
    //
    let mut walker = walker::TreeWalker::new(policy.to_string());
    if unsafe { walker.is_untrusted(&func) } {
        return match walker.reason.expect("The reason should be defined") {
            Reason::SchemaNotTrusted => Err(NotATrustedSchema(expr).to_string()),
            Reason::FunctionUntrusted => Err(Untrusted(expr).to_string()),
            Reason::FunctionUnqualified => Err(Unqualified(expr).to_string()),
        };
    }
    Ok(())
}

/// Validate a tablesample expression
///
pub fn check_tablesample(expr: &str) -> Result<(), String> {
    if expr.is_empty() {
        return Err(Empty().to_string());
    }

    let query_string = format!("SELECT 1 FROM foo {expr}");
    let query_c_string = CString::new(query_string.as_str()).unwrap();

    let raw_parsetree_list = PgTryBuilder::new(|| {
        Some(unsafe {
            pg_sys::raw_parser(
                query_c_string.as_c_str().as_ptr() as *const c_char,
                pg_sys::RawParseMode::RAW_PARSE_DEFAULT,
            )
        })
    })
    .catch_others(|_| None)
    .execute();

    // Only one statement in the parsetree is allowed
    if raw_parsetree_list.is_none()
        || raw_parsetree_list.unwrap().is_null()
        || unsafe { raw_parsetree_list.unwrap().as_ref().unwrap().length > 1 }
    {
        return Err(Invalid(expr).to_string());
    }

    Ok(())
}

/// Check that a given node is one of a list of types of nodes
///
fn check_node(node: &PgBox<pg_sys::Node>, valid_nodes: Vec<pg_sys::NodeTag>) -> bool {
    for node_tag in valid_nodes {
        if unsafe { pgrx::is_a(node.as_ptr(), node_tag) } {
            return true;
        }
    }
    false
}

/// Validate a CASE WHEN expression
///
pub fn check_when(expr: &str, policy: &'static str) -> Result<(), String> {
    if expr.is_empty() {
        return Err(Empty().to_string());
    }

    let when_node = parse_expression(expr)?;
    log::debug3!("{:#}", when_node);

    let valid_nodes = vec![
        pg_sys::NodeTag::T_ColumnRef,
        pg_sys::NodeTag::T_A_Const,
        pg_sys::NodeTag::T_A_Expr,
        pg_sys::NodeTag::T_NullTest,
        pg_sys::NodeTag::T_BoolExpr,
        pg_sys::NodeTag::T_BooleanTest,
        pg_sys::NodeTag::T_FuncCall,
    ];

    if !check_node(&when_node, valid_nodes) {
        return Err(Invalid(expr).to_string());
    }

    if !guc::ANON_RESTRICT_TO_TRUSTED_SCHEMAS.get() {
        return Ok(());
    }

    // Walk through the parse tree and check that the function itself and
    // all other functions used as parameters belong to a trusted schema.
    // The goal is to block privilege escalation attacks using something like:
    //
    // `MASKED WITH FUNCTION pg_catalog.upper(public.elevate())`
    //
    let mut walker = walker::TreeWalker::new(policy.to_string());
    if unsafe { walker.is_untrusted(&when_node) } {
        return match walker.reason.expect("The reason should be defined") {
            Reason::SchemaNotTrusted => Err(NotATrustedSchema(expr).to_string()),
            Reason::FunctionUntrusted => Err(Untrusted(expr).to_string()),
            Reason::FunctionUnqualified => Err(Unqualified(expr).to_string()),
        };
    }

    Ok(())
}

/// check that an expression is a valid masking value
///
pub fn check_value(expr: &str) -> Result<(), String> {
    let value_node = parse_expression(expr)?;

    let valid_nodes = vec![pg_sys::NodeTag::T_ColumnRef, pg_sys::NodeTag::T_A_Const];

    if !check_node(&value_node, valid_nodes) {
        return Err(Invalid(expr).to_string());
    }

    Ok(())
}

/// Check that a function is trusted
///
/// A function may have multiple definitions, e.g. foo(INT) and foo(TEXT)
///
/// A function is considered as "trusted for anon" if those 2 conditions are met
///
/// - none of its definition is labeled as `UNTRUSTED`
/// - one its definitions is labeled as `TRUSTED` or it belongs to a `TRUSTED` schema
///
pub fn is_trusted_function(
    namespace_id: pg_sys::Oid,
    func_name: *const c_char,
    policy: &str,
) -> Result<(), Reason> {
    use crate::rule::function::Function;

    let mut trusted: Option<bool> = None;

    // Read the Postgres cache to get all the definitions of the function
    // see example below
    // https://github.com/postgres/postgres/blob/23c5a0e7d43bc925c6001538f04a458933a11fc1/src/backend/catalog/namespace.c#L1210
    //
    let catlist = unsafe {
        PgBox::from_pg(pg_sys::SearchSysCacheList(
            pg_sys::SysCacheIdentifier::PROCNAMEARGSNSP
                .try_into()
                .unwrap(),
            1,
            pg_sys::Datum::from(func_name),
            pg_sys::Datum::from(0),
            pg_sys::Datum::from(0),
        ))
    };

    // transform the members array into a proper rust slice
    let members = unsafe { catlist.members.as_slice(catlist.n_members as usize) };

    // Each member is a definition of the function
    for def in members {
        // if an unstrusted definition was found previously,
        // then there's no need to check the others
        if !trusted.unwrap_or(true) {
            continue;
        };

        // Fetch the tuple from the pg_proc table
        let mut catctup = unsafe { **def as pg_sys::CatCTup };
        let proctup = &mut catctup.tuple as *mut pg_sys::HeapTupleData;
        // procform is a pg_sys::FormData_pg_pro object
        let procform: pgrx::PgBox<pg_sys::FormData_pg_proc> =
            unsafe { PgBox::from_pg(pg_sys::heap_tuple_get_struct(proctup)) };

        // Skip functions from others namespaces
        if procform.pronamespace != namespace_id {
            continue;
        };

        // Check that the OID is fine
        if !macros::OidIsValid(procform.oid) {
            error::internal("Function OID is invalid").ereport();
        };

        // Get the security label for this definition
        if let Ok(rule_on_function) = Function::from_seclabel(procform.oid, policy) {
            // Found no label, skip to the next definition
            if rule_on_function.is_empty() {
                continue;
            }

            // Read the security label and check its content
            if rule_on_function.is_trusted() || rule_on_function.is_restricted() {
                trusted = Some(true);
            }
            if rule_on_function.is_untrusted() {
                trusted = Some(false);
            }
        }
    }

    // Release the cache
    unsafe {
        pg_sys::ReleaseCatCacheList(catlist.as_ptr());
    }

    if let Some(trusted) = trusted {
        if trusted {
            return Ok(());
        } else {
            return Err(Reason::FunctionUntrusted);
        }
    }

    // At this point, if we still don't know whether the function is trusted or
    // not, the last chance is to check if the schema itself is TRUSTED
    use crate::rule::schema::Schema;
    match Schema::is_trusted(namespace_id, policy) {
        true => Ok(()),
        false => Err(Reason::SchemaNotTrusted),
    }
}

/// Parse a given expression and return its raw statement
///
pub fn parse_expression(expr: &str) -> Result<PgBox<pg_sys::Node>, String> {
    if expr.is_empty() {
        return Err(Empty().to_string());
    }

    let query_string = format!("SELECT {expr}");
    let query_c_string = CString::new(query_string.as_str()).unwrap();
    let raw_parsetree_list = PgTryBuilder::new(|| {
        Some(unsafe {
            pg_sys::raw_parser(
                query_c_string.as_c_str().as_ptr() as *const c_char,
                pg_sys::RawParseMode::RAW_PARSE_DEFAULT,
            )
        })
    })
    .catch_others(|_| None)
    .execute();

    // Only one statement in the parsetree is allowed
    if raw_parsetree_list.is_none()
        || raw_parsetree_list.unwrap().is_null()
        || unsafe { raw_parsetree_list.unwrap().as_ref().unwrap().length > 1 }
    {
        return Err(Invalid(expr).to_string());
    }

    let raw_stmt = unsafe {
        // this is the equivalent of the linitial_node C macro
        // https://doxygen.postgresql.org/pg__list_8h.html#a213ac28ac83471f2a47d4e3918f720b4
        PgBox::<pg_sys::RawStmt>::from_pg(
            pg_sys::list_nth(raw_parsetree_list.unwrap(), 0) as *mut pg_sys::RawStmt
        )
    };

    let stmt =
        unsafe { PgBox::<pg_sys::SelectStmt>::from_pg(raw_stmt.stmt as *mut pg_sys::SelectStmt) };

    // Only one expression in the target is allowed
    if unsafe { stmt.targetList.as_ref().unwrap().length > 1 } {
        return Err(Invalid(expr).to_string());
    }

    let restarget = unsafe {
        PgBox::<pg_sys::ResTarget>::from_pg(
            pg_sys::list_nth(stmt.targetList, 0) as *mut pg_sys::ResTarget
        )
    };

    Ok(unsafe { PgBox::<pg_sys::Node>::from_pg(restarget.val) })
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::fixture;
    use crate::input::*;

    #[pg_test]
    fn test_check_function_ok() {
        let _outfit = fixture::create_masking_functions();
        assert!(check_function("anon.lower('A')", "anon").is_ok());
        assert!(check_function("anon.lower(anon.upper('a'))", "anon").is_ok());
        assert!(check_function("outfit.mask(0)", "anon").is_ok());
    }

    #[pg_test]
    fn test_check_function_err() {
        let _outfit = fixture::create_masking_functions();
        assert!(check_function("foo()", "anon").is_err());
        assert!(check_function("public.foo(bar())", "anon").is_err());
        assert!(check_function("pg_catalog.does_not_exist()", "anon").is_err());
        assert!(check_function("pg_catalog.pg_ls_dir('/)", "anon").is_err());
        assert!(check_function("anon.lower(pg_catalog.pg_ls_dir())", "anon").is_err());
        assert!(check_function("outfit.belt()", "anon").is_err());
        assert!(check_function("", "anon").is_err());
        assert!(check_function("foo(), bar()", "anon").is_err());
        assert!(check_function("Robert'); DROP TABLE Students;--", "anon").is_err());
    }

    #[pg_test]
    fn test_check_function_unqualified() {
        assert!(check_function("foo()", "anon").is_err());
    }

    #[pg_test]
    fn test_check_tablesample() {
        assert!(check_tablesample("TABLESAMPLE SYSTEM(10)").is_ok());
        assert!(check_tablesample("").is_err());
        assert!(check_tablesample("TABLESAMPLE SYSTEM(10); DROP TABLE Students;--").is_err());
    }

    #[pg_test]
    fn test_check_value() {
        assert!(check_value("foo()").is_err());
        assert!(check_value("CAST(0 AS INT)").is_err());
        assert!(check_value("1").is_ok());
        assert!(check_value("a").is_ok());
        assert!(check_value("NULL").is_ok());
        assert!(check_value("").is_err());
    }

    #[pg_test]
    fn test_is_trusted_function() {
        let outfit = fixture::create_masking_functions();
        let mask_cstr = CString::new("mask").unwrap();
        let mask = mask_cstr.as_ptr() as *const c_char;
        let belt_cstr = CString::new("belt").unwrap();
        let belt = belt_cstr.as_ptr() as *const c_char;
        let cape_cstr = CString::new("cape").unwrap();
        let cape = cape_cstr.as_ptr() as *const c_char;

        assert!(is_trusted_function(outfit, mask, "anon").is_ok());
        assert!(is_trusted_function(outfit, belt, "anon").is_err());
        assert!(is_trusted_function(outfit, cape, "anon").is_err());

        // Same tests but now the outfit schema is trusted
        fixture::trust_masking_functions_schema();
        assert!(is_trusted_function(outfit, mask, "anon").is_ok());
        assert!(is_trusted_function(outfit, belt, "anon").is_err());
        assert!(is_trusted_function(outfit, cape, "anon").is_ok());

        // Check that a restricted function is also trusted
        let anon_schema = Spi::get_one::<pg_sys::Oid>("SELECT 'anon'::REGNAMESPACE::OID;")
            .unwrap()
            .expect("should be an OID");
        let pseudo_city_cstr = CString::new("pseudo_city").unwrap();
        let pseudo_city = pseudo_city_cstr.as_ptr() as *const c_char;
        assert!(is_trusted_function(anon_schema, pseudo_city, "anon").is_ok());
    }

    #[pg_test]
    fn test_parse_expression() {
        assert!(parse_expression("a").is_ok());
        assert!(parse_expression("foo()").is_ok());
        assert!(parse_expression("NULL").is_ok());
        assert!(parse_expression("").is_err());
        assert!(parse_expression("a,b,c").is_err());
        assert!(parse_expression("Robert'); DROP TABLE Students;--").is_err());
    }
}

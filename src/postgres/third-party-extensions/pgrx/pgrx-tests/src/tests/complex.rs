//! Type used by various tests.
use core::ffi::CStr;
use pgrx::pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use pgrx::prelude::*;
use pgrx::stringinfo::StringInfo;

use crate::get_named_capture;

#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct Complex {
    pub r: f64,
    pub i: f64,
}

impl Eq for Complex {}
impl PartialEq for Complex {
    fn eq(&self, other: &Self) -> bool {
        self.r == other.r && self.i == other.i
    }
}

impl Complex {
    #[allow(dead_code)]
    pub fn random() -> PgBox<Complex> {
        unsafe {
            let mut c = PgBox::<Complex>::alloc0();
            c.r = rand::random();
            c.i = rand::random();
            c.into_pg_boxed()
        }
    }
}

extension_sql!(
    r#"CREATE TYPE complex;"#,
    name = "create_complex_shell_type",
    creates = [Type(Complex)]
);

unsafe impl SqlTranslatable for Complex {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("Complex"))
    }

    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("Complex")))
    }
}

#[pg_extern(immutable)]
fn complex_in(input: &CStr) -> PgBox<Complex, AllocatedByRust> {
    let input_as_str = input.to_str().unwrap();
    let re = regex::Regex::new(
        r#"(?P<x>[-+]?([0-9]*\.[0-9]+|[0-9]+)),\s*(?P<y>[-+]?([0-9]*\.[0-9]+|[0-9]+))"#,
    )
    .unwrap();
    let x = get_named_capture(&re, "x", input_as_str).unwrap();
    let y = get_named_capture(&re, "y", input_as_str).unwrap();
    let mut complex = unsafe { PgBox::<Complex>::alloc() };

    complex.r = str::parse::<f64>(&x).unwrap_or_else(|_| panic!("{x} isn't a f64"));
    complex.i = str::parse::<f64>(&y).unwrap_or_else(|_| panic!("{y} isn't a f64"));

    complex
}

#[pg_extern(immutable)]
fn complex_out(complex: PgBox<Complex>) -> &'static CStr {
    let mut sb = StringInfo::new();
    sb.push_str(&format!("{}, {}", complex.r, complex.i));
    unsafe { sb.leak_cstr() }
}

extension_sql!(
    r#"
CREATE TYPE complex (
   internallength = 16,
   input = complex_in,
   output = complex_out,
   alignment = double
);
"#,
    name = "create_complex_type",
    requires = ["create_complex_shell_type", complex_in, complex_out]
);

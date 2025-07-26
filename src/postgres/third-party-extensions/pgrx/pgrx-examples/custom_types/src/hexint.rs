//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::callconv::{ArgAbi, BoxRet};
use pgrx::datum::Datum;
use pgrx::pg_sys::Oid;
use pgrx::pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use pgrx::prelude::*;
use pgrx::{rust_regtypein, StringInfo};
use std::error::Error;
use std::ffi::CStr;
use std::fmt::{Display, Formatter};

/// A `BIGINT`-style type that is always represented as hexadecimal.
///
/// # Examples
///
/// ```sql
/// custom_types=# SELECT
///     42::hexint,
///     '0xdeadbeef'::hexint,
///     '0Xdeadbeef'::hexint::bigint,
///     2147483647::hexint,
///     9223372036854775807::hexint,
///     '0xbadc0ffee'::hexint::numeric;
///  hexint |   hexint   |    int8    |   hexint   |       hexint       |   numeric   
/// --------+------------+------------+------------+--------------------+-------------
///  0x2a   | 0xdeadbeef | 3735928559 | 0x7fffffff | 0x7fffffffffffffff | 50159747054
/// ```
#[repr(transparent)]
#[derive(
    Copy,
    Clone,
    Debug,
    Ord,
    PartialOrd,
    Eq,
    PartialEq,
    Hash,
    PostgresEq,
    PostgresOrd,
    PostgresHash
)]
struct HexInt {
    value: i64,
}

impl Display for HexInt {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        // format ourselves as a `0x`-prefixed hexadecimal string
        write!(f, "0x{:x}", self.value)
    }
}

unsafe impl SqlTranslatable for HexInt {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        // this is what the SQL type is called when used in a function argument position
        Ok(SqlMapping::As("hexint".into()))
    }

    fn return_sql() -> Result<Returns, ReturnsError> {
        // this is what the SQL type is called when used in a function return type position
        Ok(Returns::One(SqlMapping::As("hexint".into())))
    }
}

impl FromDatum for HexInt {
    unsafe fn from_polymorphic_datum(datum: pg_sys::Datum, is_null: bool, _: Oid) -> Option<Self>
    where
        Self: Sized,
    {
        if is_null {
            None
        } else {
            Some(HexInt { value: datum.value() as _ })
        }
    }
}

impl IntoDatum for HexInt {
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(self.value))
    }

    fn type_oid() -> Oid {
        rust_regtypein::<Self>()
    }
}

unsafe impl<'fcx> ArgAbi<'fcx> for HexInt
where
    Self: 'fcx,
{
    unsafe fn unbox_arg_unchecked(arg: ::pgrx::callconv::Arg<'_, 'fcx>) -> Self {
        unsafe { arg.unbox_arg_using_from_datum().unwrap() }
    }
}

unsafe impl BoxRet for HexInt {
    unsafe fn box_into<'fcx>(self, fcinfo: &mut pgrx::callconv::FcInfo<'fcx>) -> Datum<'fcx> {
        unsafe { fcinfo.return_raw_datum(pg_sys::Datum::from(self.value)) }
    }
}

/// Input function for `HexInt`.  Parses any valid "radix(16)" text string, with or without a leading
/// `0x` (or `0X`) into a `HexInt` type.  Parse errors are returned and handled by pgrx
///
/// `requires = [ "shell_type" ]` indicates that the CREATE FUNCTION SQL for this function must happen
/// *after* the SQL for the "shell_type" block.
#[pg_extern(immutable, parallel_safe, requires = [ "shell_type" ])]
fn hexint_in(input: &CStr) -> Result<HexInt, Box<dyn Error>> {
    Ok(HexInt {
        value: i64::from_str_radix(
            // ignore `0x` or `0X` prefixes
            input.to_str()?.trim_start_matches("0x").trim_start_matches("0X"),
            16,
        )?,
    })
}

/// Output function for `HexInt`.  Returns a cstring with the underlying `HexInt` value formatted
/// as hexadecimal, prefixed with `0x`.
///
/// `requires = [ "shell_type" ]` indicates that the CREATE FUNCTION SQL for this function must happen
/// *after* the SQL for the "shell_type" block.
#[pg_extern(immutable, parallel_safe, requires = [ "shell_type" ])]
fn hexint_out(value: HexInt) -> &'static CStr {
    let mut s = StringInfo::new();
    s.push_str(&value.to_string());
    // SAFETY: We just constructed this StringInfo ourselves
    unsafe { s.leak_cstr() }
}

//
// cast functions
//

#[pg_extern(immutable, parallel_safe)]
fn hexint_to_int(hexint: HexInt) -> Result<i32, Box<dyn Error>> {
    Ok(hexint.value.try_into()?)
}

#[pg_extern(immutable, parallel_safe)]
fn hexint_to_numeric(hexint: HexInt) -> Result<AnyNumeric, Box<dyn Error>> {
    Ok(hexint.value.into())
}

#[pg_extern(immutable, parallel_safe)]
fn int_to_hexint(bigint: i32) -> HexInt {
    HexInt { value: bigint as _ }
}

#[pg_extern(immutable, parallel_safe)]
fn numeric_to_hexint(bigint: AnyNumeric) -> Result<HexInt, Box<dyn Error>> {
    Ok(HexInt { value: bigint.try_into()? })
}

//
// handwritten sql
//

// creates the `hexint` shell type, which is essentially a type placeholder so that the
// input and output functions can be created
extension_sql!(
    r#"
CREATE TYPE hexint; -- shell type
"#,
    name = "shell_type",
    bootstrap // declare this extension_sql block as the "bootstrap" block so that it happens first in sql generation
);

// create the actual type, specifying the input and output functions
extension_sql!(
    r#"
CREATE TYPE hexint (
    INPUT = hexint_in,
    OUTPUT = hexint_out,
    LIKE = int8
);
"#,
    name = "concrete_type",
    creates = [Type(HexInt)],
    requires = ["shell_type", hexint_in, hexint_out], // so that we won't be created until the shell type and input and output functions have
);

// some convenient casts
extension_sql!(
    r#"
CREATE CAST (bigint AS hexint) WITHOUT FUNCTION AS IMPLICIT;
CREATE CAST (hexint AS bigint) WITHOUT FUNCTION AS IMPLICIT;
CREATE CAST (hexint AS int) WITH FUNCTION hexint_to_int AS IMPLICIT;
CREATE CAST (int AS hexint) WITH FUNCTION int_to_hexint AS IMPLICIT;
CREATE CAST (hexint AS numeric) WITH FUNCTION hexint_to_numeric AS IMPLICIT;
CREATE CAST (numeric AS hexint) WITH FUNCTION numeric_to_hexint AS IMPLICIT;
"#,
    name = "casts",
    requires = [hexint_to_int, int_to_hexint, hexint_to_numeric, numeric_to_hexint]
);

#[cfg(not(feature = "no-schema-generation"))]
#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::hexint::HexInt;
    use pgrx::prelude::*;
    use std::error::Error;

    #[pg_test]
    fn test_hexint_input_func() -> Result<(), Box<dyn Error>> {
        let value = Spi::get_one::<HexInt>("SELECT '0x2a'::hexint")?;
        assert_eq!(value, Some(HexInt { value: 0x2a }));
        Ok(())
    }

    #[pg_test]
    fn test_hexint_output_func() -> Result<(), Box<dyn Error>> {
        let value = Spi::get_one::<String>("SELECT 42::hexint::text")?;
        assert_eq!(value, Some("0x2a".into()));
        Ok(())
    }

    #[pg_test]
    fn test_hash() {
        Spi::run(
            "CREATE TABLE hexintext (
                id hexint,
                data TEXT
            );
            CREATE TABLE hexint_duo (
                id hexint,
                foo_id hexint
            );
            INSERT INTO hexintext DEFAULT VALUES;
            INSERT INTO hexint_duo DEFAULT VALUES;
            SELECT *
            FROM hexint_duo
            JOIN hexintext ON hexint_duo.id = hexintext.id;",
        )
        .unwrap();
    }

    #[pg_test]
    fn test_commutator() {
        Spi::run(
            "CREATE TABLE hexintext (
                id hexint,
                data TEXT
            );
            CREATE TABLE just_hexint (
                id hexint
            );
            SELECT *
            FROM just_hexint
            JOIN hexintext ON just_hexint.id = hexintext.id;",
        )
        .unwrap();
    }
}

//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::prelude::*;

pgrx::pg_module_magic!(name, version);

/// Demonstrates ChangeVarNodes by creating a Var node and remapping its varno.
///
/// Creates a Var referencing range table entry `old_varno`, then uses
/// ChangeVarNodes to remap it to `new_varno`. Returns the resulting varno.
#[pg_extern]
fn demo_change_var_nodes(old_varno: i32, new_varno: i32) -> i32 {
    unsafe {
        let var = pg_sys::makeVar(
            old_varno.try_into().unwrap(),
            1,
            pg_sys::INT4OID,
            -1,
            pg_sys::InvalidOid,
            0,
        );

        pg_sys::ChangeVarNodes(var as *mut pg_sys::Node, old_varno, new_varno, 0);

        (*var).varno.try_into().unwrap()
    }
}

/// Demonstrates OffsetVarNodes by creating a Var node and shifting its varno
/// by the given offset.
///
/// Creates a Var with varno=1, then offsets it. Returns the resulting varno.
#[pg_extern]
fn demo_offset_var_nodes(offset: i32) -> i32 {
    unsafe {
        let var = pg_sys::makeVar(1, 1, pg_sys::INT4OID, -1, pg_sys::InvalidOid, 0);

        pg_sys::OffsetVarNodes(var as *mut pg_sys::Node, offset, 0);

        (*var).varno.try_into().unwrap()
    }
}

/// Demonstrates rangeTableEntry_used by creating a Var node referencing a
/// specific range table index, then checking if that index is used.
#[pg_extern]
fn demo_range_table_entry_used(varno: i32, check_rt_index: i32) -> bool {
    unsafe {
        let var = pg_sys::makeVar(
            varno.try_into().unwrap(),
            1,
            pg_sys::INT4OID,
            -1,
            pg_sys::InvalidOid,
            0,
        );

        pg_sys::rangeTableEntry_used(var as *mut pg_sys::Node, check_rt_index, 0)
    }
}

/// Demonstrates IncrementVarSublevelsUp by creating a Var and incrementing
/// its varlevelsup field.
#[pg_extern]
fn demo_increment_var_sublevels_up(initial_level: i32, delta: i32) -> i32 {
    unsafe {
        let var = pg_sys::makeVar(
            1,
            1,
            pg_sys::INT4OID,
            -1,
            pg_sys::InvalidOid,
            initial_level as pg_sys::Index,
        );

        pg_sys::IncrementVarSublevelsUp(var as *mut pg_sys::Node, delta, 0);

        (*var).varlevelsup as i32
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use pgrx::prelude::*;

    #[pg_test]
    fn test_change_var_nodes() {
        let result = Spi::get_one::<i32>("SELECT rewrite_manip.demo_change_var_nodes(1, 5)")
            .expect("SPI failed")
            .expect("null result");
        assert_eq!(result, 5, "varno should be remapped from 1 to 5");
    }

    #[pg_test]
    fn test_change_var_nodes_no_match() {
        let result = Spi::get_one::<i32>("SELECT rewrite_manip.demo_change_var_nodes(1, 5)")
            .expect("SPI failed")
            .expect("null result");
        assert_eq!(result, 5);

        let result2 = Spi::get_one::<i32>("SELECT rewrite_manip.demo_change_var_nodes(2, 5)")
            .expect("SPI failed")
            .expect("null result");
        assert_eq!(result2, 5, "varno 2 should be remapped to 5");
    }

    #[pg_test]
    fn test_offset_var_nodes() {
        let result = Spi::get_one::<i32>("SELECT rewrite_manip.demo_offset_var_nodes(3)")
            .expect("SPI failed")
            .expect("null result");
        assert_eq!(result, 4, "varno should be 1 + 3 = 4");
    }

    #[pg_test]
    fn test_offset_var_nodes_zero() {
        let result = Spi::get_one::<i32>("SELECT rewrite_manip.demo_offset_var_nodes(0)")
            .expect("SPI failed")
            .expect("null result");
        assert_eq!(result, 1, "varno should remain 1 with offset 0");
    }

    #[pg_test]
    fn test_range_table_entry_used_match() {
        let result = Spi::get_one::<bool>("SELECT rewrite_manip.demo_range_table_entry_used(3, 3)")
            .expect("SPI failed")
            .expect("null result");
        assert!(result, "rt_index 3 should be found when Var has varno=3");
    }

    #[pg_test]
    fn test_range_table_entry_used_no_match() {
        let result = Spi::get_one::<bool>("SELECT rewrite_manip.demo_range_table_entry_used(3, 5)")
            .expect("SPI failed")
            .expect("null result");
        assert!(!result, "rt_index 5 should not be found when Var has varno=3");
    }

    #[pg_test]
    fn test_increment_var_sublevels_up() {
        let result =
            Spi::get_one::<i32>("SELECT rewrite_manip.demo_increment_var_sublevels_up(0, 2)")
                .expect("SPI failed")
                .expect("null result");
        assert_eq!(result, 2, "varlevelsup should be 0 + 2 = 2");
    }
}

#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {}

    pub fn postgresql_conf_options() -> Vec<&'static str> {
        vec![]
    }
}

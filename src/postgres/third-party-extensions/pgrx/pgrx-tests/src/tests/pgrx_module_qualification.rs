//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#![forbid(unsafe_op_in_unsafe_fn)]
//! If this test compiles, and assuming it includes a usage of all the things pgrx generates code for
//! then we know that pgrx has ::fully::qualified::all::its::pgrx::symbols

#[cfg(feature = "pg_test")]
mod pgrx_modqual_tests {
    // this is the trick (thanks @thomcc for the idea!).  We redefine the pgrx module as (essentially)
    // empty.  This way, if pgrx emits any code that uses `pgrx::path::to::Thing` instead of `::pgrx::path::to::Thing`
    // we'll fail to compile
    mod pgrx {
        // the code #[pg_guard] emits isn't qualified with `::pgrx`
        // This is intentional by pgrx, so need to account for it
        pub mod pg_sys {
            pub mod submodules {
                pub mod panic {
                    pub use ::pgrx::pg_sys::panic::pgrx_extern_c_guard;
                }
            }
        }
    }

    use pgrx_macros::{
        opname, pg_aggregate, pg_extern, pg_guard, pg_operator, pg_schema, pg_trigger, pgrx,
        PostgresEq, PostgresHash, PostgresOrd, PostgresType,
    };

    ::pgrx::extension_sql!("SELECT 1;", name = "pgrx_module_qualification_test");

    #[derive(
        Eq,
        Ord,
        PartialOrd,
        PartialEq,
        Hash,
        PostgresType,
        PostgresOrd,
        PostgresEq,
        PostgresHash,
        serde::Serialize,
        serde::Deserialize,
        Copy,
        Clone,
        Debug
    )]
    pub struct PgrxModuleQualificationTest {
        v: i32,
    }

    #[pg_extern]
    fn foo() {}

    #[pg_extern]
    fn foo_i32() -> i32 {
        42
    }

    #[pg_extern]
    fn foo_two_args_return_void(_a: i32, _b: String) {}

    #[pg_extern]
    fn foo_return_null() -> Option<i32> {
        None
    }

    #[pg_extern]
    fn foo_composite() -> ::pgrx::composite_type!('static, "Foo") {
        todo!()
    }

    #[pg_extern]
    fn foo_default_arg(_a: ::pgrx::default!(i64, 1)) {
        todo!()
    }

    #[pg_extern]
    fn foo_table(
    ) -> ::pgrx::iter::TableIterator<'static, (::pgrx::name!(index, i32), ::pgrx::name!(value, f64))>
    {
        todo!()
    }

    #[pg_extern]
    fn foo_set() -> ::pgrx::iter::SetOfIterator<'static, i64> {
        todo!()
    }

    #[pg_extern]
    fn foo_result_set(
    ) -> std::result::Result<::pgrx::iter::SetOfIterator<'static, i64>, Box<dyn std::error::Error>>
    {
        todo!()
    }

    #[pg_extern]
    fn foo_result_option_set(
    ) -> std::result::Result<::pgrx::iter::SetOfIterator<'static, i64>, Box<dyn std::error::Error>>
    {
        todo!()
    }

    #[pg_operator]
    #[opname(=<>=)]
    fn fake_foo_operator(_a: PgrxModuleQualificationTest, _b: PgrxModuleQualificationTest) -> bool {
        todo!()
    }

    #[allow(dead_code)]
    #[pg_guard]
    extern "C-unwind" fn extern_foo_func() {}

    #[pg_schema]
    mod foo_schema {}

    #[pg_trigger]
    fn foo_trigger<'a>(
        _trigger: &'a ::pgrx::PgTrigger<'a>,
    ) -> Result<
        Option<::pgrx::heap_tuple::PgHeapTuple<'a, ::pgrx::pgbox::AllocatedByPostgres>>,
        Box<dyn std::any::Any>,
    > {
        todo!()
    }

    #[pg_extern]
    fn err() {
        ::pgrx::error!("HERE")
    }

    #[pg_aggregate]
    impl ::pgrx::Aggregate for PgrxModuleQualificationTest {
        type State = ::pgrx::datum::PgVarlena<Self>;
        type Args = ::pgrx::name!(value, Option<i32>);
        const NAME: &'static str = "PgrxModuleQualificationTestAgg";

        const INITIAL_CONDITION: Option<&'static str> = Some(r#"{"v": 0}"#);

        #[pgrx(parallel_safe, immutable)]
        fn state(
            _current: Self::State,
            _arg: Self::Args,
            _fcinfo: ::pgrx::pg_sys::FunctionCallInfo,
        ) -> Self::State {
            todo!()
        }

        // You can skip all these:
        type Finalize = i32;

        fn finalize(
            _current: Self::State,
            _direct_args: Self::OrderedSetArgs,
            _fcinfo: ::pgrx::pg_sys::FunctionCallInfo,
        ) -> Self::Finalize {
            todo!()
        }
    }
}

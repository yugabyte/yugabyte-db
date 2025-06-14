//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;
    use std::ffi::CStr;

    use pgrx::guc::*;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_bool_guc() {
        static GUC: GucSetting<bool> = GucSetting::<bool>::new(true);
        GucRegistry::define_bool_guc(
            "test.bool",
            "test bool gucs",
            "test bool gucs",
            &GUC,
            GucContext::Userset,
            GucFlags::default(),
        );
        assert_eq!(GUC.get(), true);

        Spi::run("SET test.bool TO false;").expect("SPI failed");
        assert_eq!(GUC.get(), false);

        Spi::run("SET test.bool TO true;").expect("SPI failed");
        assert_eq!(GUC.get(), true);
    }

    #[pg_test]
    fn test_int_guc() {
        static GUC: GucSetting<i32> = GucSetting::<i32>::new(42);
        GucRegistry::define_int_guc(
            "test.int",
            "test int guc",
            "test int guc",
            &GUC,
            -1,
            42,
            GucContext::Userset,
            GucFlags::default(),
        );
        assert_eq!(GUC.get(), 42);

        Spi::run("SET test.int = -1").expect("SPI failed");
        assert_eq!(GUC.get(), -1);

        Spi::run("SET test.int = 12").expect("SPI failed");
        assert_eq!(GUC.get(), 12);
    }

    #[pg_test]
    fn test_mb_guc() {
        static GUC: GucSetting<i32> = GucSetting::<i32>::new(42);
        GucRegistry::define_int_guc(
            "test.megabytes",
            "test megabytes guc",
            "test megabytes guc",
            &GUC,
            -1,
            42000,
            GucContext::Userset,
            GucFlags::UNIT_MB,
        );
        assert_eq!(GUC.get(), 42);

        Spi::run("SET test.megabytes = '1GB'").expect("SPI failed");
        assert_eq!(GUC.get(), 1024);
    }

    #[pg_test]
    fn test_float_guc() {
        static GUC: GucSetting<f64> = GucSetting::<f64>::new(42.42);
        GucRegistry::define_float_guc(
            "test.float",
            "test float guc",
            "test float guc",
            &GUC,
            -1.0f64,
            43.0f64,
            GucContext::Userset,
            GucFlags::default(),
        );
        assert_eq!(GUC.get(), 42.42);

        Spi::run("SET test.float = -1").expect("SPI failed");
        assert_eq!(GUC.get(), -1.0);

        Spi::run("SET test.float = 12").expect("SPI failed");
        assert_eq!(GUC.get(), 12.0);

        Spi::run("SET test.float = 3.333").expect("SPI failed");
        assert_eq!(GUC.get(), 3.333);
    }

    #[pg_test]
    fn test_string_guc() {
        static GUC: GucSetting<Option<&'static CStr>> =
            GucSetting::<Option<&'static CStr>>::new(Some(c"this is a test"));
        GucRegistry::define_string_guc(
            "test.string",
            "test string guc",
            "test string guc",
            &GUC,
            GucContext::Userset,
            GucFlags::default(),
        );
        assert!(GUC.get().is_some());
        assert_eq!(GUC.get().unwrap().to_str().unwrap(), "this is a test");

        Spi::run("SET test.string = 'foo'").expect("SPI failed");
        assert_eq!(GUC.get().unwrap().to_str().unwrap(), "foo");

        Spi::run("SET test.string = DEFAULT").expect("SPI failed");
        assert_eq!(GUC.get().unwrap().to_str().unwrap(), "this is a test");
    }

    #[pg_test]
    fn test_string_guc_null_default() {
        static GUC: GucSetting<Option<&'static CStr>> =
            GucSetting::<Option<&'static CStr>>::new(None);
        GucRegistry::define_string_guc(
            "test.string",
            "test string guc",
            "test string guc",
            &GUC,
            GucContext::Userset,
            GucFlags::default(),
        );
        assert!(GUC.get().is_none());

        Spi::run("SET test.string = 'foo'").expect("SPI failed");
        assert_eq!(GUC.get().unwrap().to_str().unwrap(), "foo");

        Spi::run("SET test.string = DEFAULT").expect("SPI failed");
        assert!(GUC.get().is_none());
    }

    #[pg_test]
    fn test_enum_guc() {
        #[derive(PostgresGucEnum, Clone, Copy, PartialEq, Debug)]
        enum TestEnum {
            One,
            Two,
            Three,
        }
        static GUC: GucSetting<TestEnum> = GucSetting::<TestEnum>::new(TestEnum::Two);
        GucRegistry::define_enum_guc(
            "test.enum",
            "test enum guc",
            "test enum guc",
            &GUC,
            GucContext::Userset,
            GucFlags::default(),
        );
        assert_eq!(GUC.get(), TestEnum::Two);

        Spi::run("SET test.enum = 'One'").expect("SPI failed");
        assert_eq!(GUC.get(), TestEnum::One);

        Spi::run("SET test.enum = 'three'").expect("SPI failed");
        assert_eq!(GUC.get(), TestEnum::Three);
    }

    #[pg_test]
    fn test_guc_flags() {
        // variable ensures that GucFlags is Copy, so single name can be used when defining
        // multiple gucs
        let no_show_flag = GucFlags::NO_SHOW_ALL;
        static GUC_NO_SHOW: GucSetting<bool> = GucSetting::<bool>::new(true);
        static GUC_NO_RESET_ALL: GucSetting<bool> = GucSetting::<bool>::new(true);
        GucRegistry::define_bool_guc(
            "test.no_show",
            "test no show gucs",
            "test no show gucs",
            &GUC_NO_SHOW,
            GucContext::Userset,
            no_show_flag,
        );
        GucRegistry::define_bool_guc(
            "test.no_reset_all",
            "test no reset gucs",
            "test no reset gucs",
            &GUC_NO_RESET_ALL,
            GucContext::Userset,
            GucFlags::NO_RESET_ALL,
        );

        // change both, then check that:
        //  1. no_show does not appear in SHOW ALL while no_reset_all does
        //  2. no_reset_all is not reset by RESET ALL, while no_show is
        Spi::run("SET test.no_show TO false;").expect("SPI failed");
        Spi::run("SET test.no_reset_all TO false;").expect("SPI failed");
        assert_eq!(GUC_NO_RESET_ALL.get(), false);
        Spi::connect_mut(|client| {
            let r = client.update("SHOW ALL", None, &[]).expect("SPI failed");

            let mut no_reset_guc_in_show_all = false;
            for row in r {
                // cols of show all: name, setting, description
                let name: &str = row.get(1).unwrap().unwrap();
                assert!(!name.contains("test.no_show"));
                if name.contains("test.no_reset_all") {
                    no_reset_guc_in_show_all = true;
                }
            }
            assert!(no_reset_guc_in_show_all);

            Spi::run("RESET ALL").expect("SPI failed");
            assert_eq!(
                GUC_NO_RESET_ALL.get(),
                false,
                "'no_reset_all' should remain unchanged after 'RESET ALL'"
            );
            assert_eq!(GUC_NO_SHOW.get(), true, "'no_show' should reset after 'RESET ALL'");
        });
    }
}

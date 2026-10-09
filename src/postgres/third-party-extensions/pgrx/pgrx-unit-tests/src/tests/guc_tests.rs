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
    use crate as pgrx_unit_tests;
    use std::ffi::CString;
    use std::ffi::c_char;

    use pgrx::guc::*;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_bool_guc() {
        static GUC: GucSetting<bool> = GucSetting::<bool>::new(true);
        GucRegistry::define_bool_guc(
            c"test.bool",
            c"test bool gucs",
            c"test bool gucs",
            &GUC,
            GucContext::Userset,
            GucFlags::default(),
        );
        assert!(GUC.get());

        Spi::run("SET test.bool TO false;").expect("SPI failed");
        assert!(!GUC.get());

        Spi::run("SET test.bool TO true;").expect("SPI failed");
        assert!(GUC.get());
    }

    #[pg_test]
    fn test_int_guc() {
        static GUC: GucSetting<i32> = GucSetting::<i32>::new(42);
        GucRegistry::define_int_guc(
            c"test.int",
            c"test int guc",
            c"test int guc",
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
            c"test.megabytes",
            c"test megabytes guc",
            c"test megabytes guc",
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
            c"test.float",
            c"test float guc",
            c"test float guc",
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
        static GUC: GucSetting<Option<CString>> =
            GucSetting::<Option<CString>>::new(Some(c"this is a test"));
        GucRegistry::define_string_guc(
            c"test.string",
            c"test string guc",
            c"test string guc",
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
        static GUC: GucSetting<Option<CString>> = GucSetting::<Option<CString>>::new(None);
        GucRegistry::define_string_guc(
            c"test.string",
            c"test string guc",
            c"test string guc",
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
            #[doc = "three"]
            Three,
            #[name = c"five"]
            Four,
            #[hidden = true]
            Six,
        }
        static GUC: GucSetting<TestEnum> = GucSetting::<TestEnum>::new(TestEnum::Two);
        GucRegistry::define_enum_guc(
            c"test.enum",
            c"test enum guc",
            c"test enum guc",
            &GUC,
            GucContext::Userset,
            GucFlags::default(),
        );
        assert_eq!(GUC.get(), TestEnum::Two);

        Spi::run("SET test.enum = 'One'").expect("SPI failed");
        assert_eq!(GUC.get(), TestEnum::One);

        Spi::run("SET test.enum = 'three'").expect("SPI failed");
        assert_eq!(GUC.get(), TestEnum::Three);

        Spi::run("SET test.enum = 'five'").expect("SPI failed");
        assert_eq!(GUC.get(), TestEnum::Four);
    }

    #[pg_test]
    fn test_guc_flags() {
        // variable ensures that GucFlags is Copy, so single name can be used when defining
        // multiple gucs
        let no_show_flag = GucFlags::NO_SHOW_ALL;
        static GUC_NO_SHOW: GucSetting<bool> = GucSetting::<bool>::new(true);
        static GUC_NO_RESET_ALL: GucSetting<bool> = GucSetting::<bool>::new(true);
        GucRegistry::define_bool_guc(
            c"test.no_show",
            c"test no show gucs",
            c"test no show gucs",
            &GUC_NO_SHOW,
            GucContext::Userset,
            no_show_flag,
        );
        GucRegistry::define_bool_guc(
            c"test.no_reset_all",
            c"test no reset gucs",
            c"test no reset gucs",
            &GUC_NO_RESET_ALL,
            GucContext::Userset,
            GucFlags::NO_RESET_ALL,
        );

        // change both, then check that:
        //  1. no_show does not appear in SHOW ALL while no_reset_all does
        //  2. no_reset_all is not reset by RESET ALL, while no_show is
        Spi::run("SET test.no_show TO false;").expect("SPI failed");
        Spi::run("SET test.no_reset_all TO false;").expect("SPI failed");
        assert!(!GUC_NO_RESET_ALL.get());
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
            assert!(
                !GUC_NO_RESET_ALL.get(),
                "'no_reset_all' should remain unchanged after 'RESET ALL'"
            );
            assert!(GUC_NO_SHOW.get(), "'no_show' should reset after 'RESET ALL'");
        });
    }

    #[pg_test]
    #[should_panic(expected = "invalid value for parameter \"test.hooks\": 0")]
    fn test_guc_check_hook() {
        static SIDE_EFFECT: std::sync::RwLock<i32> = std::sync::RwLock::new(0);

        #[pg_guard]
        unsafe extern "C-unwind" fn check_hook(
            newval: *mut bool,
            _extra: *mut *mut std::ffi::c_void,
            _source: pg_sys::GucSource::Type,
        ) -> bool {
            if *newval {
                *SIDE_EFFECT.write().unwrap() += 1;
            }
            *newval
        }

        // Create and register GUC with hooks. As default is true, SIDE_EFFECT will be 1.
        static GUC: GucSetting<bool> = GucSetting::<bool>::new(true);
        unsafe {
            GucRegistry::define_bool_guc_with_hooks(
                c"test.hooks",
                c"test hooks guc",
                c"test hooks guc",
                &GUC,
                GucContext::Userset,
                GucFlags::default(),
                Some(check_hook),
                None,
                None,
            );
        }

        // Test check hook - should reject false and not initialize the GUC
        assert!(
            Spi::run("SET test.hooks TO false").is_err(),
            "Expected panic when setting test.hooks to false"
        );
        assert_eq!(*SIDE_EFFECT.read().unwrap(), 1);

        // Test check hook - should accept true and increment SIDE_EFFECT
        assert!(Spi::run("SET test.hooks TO true").is_ok());
        assert!(GUC.get());
        assert_eq!(*SIDE_EFFECT.read().unwrap(), 2);
    }

    #[pg_test]
    #[should_panic(expected = "should panic!")]
    fn test_check_hook_fail() {
        #[pg_guard]
        unsafe extern "C-unwind" fn check_hook(
            newval: *mut bool,
            _extra: *mut *mut std::ffi::c_void,
            _source: pg_sys::GucSource::Type,
        ) -> bool {
            if *newval {
                panic!("should panic!");
            }
            *newval
        }

        static GUARDED_GUC: GucSetting<bool> = GucSetting::<bool>::new(true);
        unsafe {
            GucRegistry::define_bool_guc_with_hooks(
                c"test.guarded_hooks",
                c"test guarded hooks guc",
                c"test guarded hooks guc",
                &GUARDED_GUC,
                GucContext::Userset,
                GucFlags::default(),
                Some(check_hook),
                None,
                None,
            );
        }
    }

    #[pg_test]
    fn test_assign_hook() {
        static SIDE_EFFECT: std::sync::RwLock<i32> = std::sync::RwLock::new(0);

        #[pg_guard]
        unsafe extern "C-unwind" fn assign_hook(newval: bool, _extra: *mut ::core::ffi::c_void) {
            if newval {
                *SIDE_EFFECT.write().unwrap() += 1;
            }
        }

        // Create and register GUC with hooks. As default is false, SIDE_EFFECT will be 0.
        static GUC: GucSetting<bool> = GucSetting::<bool>::new(false);
        unsafe {
            GucRegistry::define_bool_guc_with_hooks(
                c"test.hooks",
                c"test hooks guc",
                c"test hooks guc",
                &GUC,
                GucContext::Userset,
                GucFlags::default(),
                None,
                Some(assign_hook),
                None,
            );
        }

        // SIDE_EFFECT should not be updated
        Spi::run("SET test.hooks TO false").unwrap();
        assert_eq!(*SIDE_EFFECT.read().unwrap(), 0);

        // SIDE_EFFECT should be updated
        Spi::run("SET test.hooks TO true").unwrap();
        assert_eq!(*SIDE_EFFECT.read().unwrap(), 1);
    }

    #[pg_test]
    fn test_show_hook() {
        #[pg_guard]
        unsafe extern "C-unwind" fn show_hook() -> *const c_char {
            CString::new("CUSTOM_SHOW_HOOK").unwrap().into_raw() as *const c_char
        }

        // Register GUC
        static GUC: GucSetting<bool> = GucSetting::<bool>::new(false);
        unsafe {
            GucRegistry::define_bool_guc_with_hooks(
                c"test.hooks",
                c"test hooks guc",
                c"test hooks guc",
                &GUC,
                GucContext::Userset,
                GucFlags::default(),
                None,
                None,
                Some(show_hook),
            );
        }

        // Test show hook
        Spi::connect_mut(|client| {
            let r = client.update("SHOW test.hooks", None, &[]).expect("SPI failed");
            let value: &str = r.first().get_one::<&str>().unwrap().unwrap();
            assert_eq!(value, "CUSTOM_SHOW_HOOK");
        });
    }
}

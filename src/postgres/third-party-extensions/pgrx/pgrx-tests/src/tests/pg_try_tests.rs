//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use core::ptr;
use pgrx::prelude::*;

// if our Postgres ERROR and Rust panic!() handling is incorrect, this little bit of useless code
// will crash postgres.  If things are correct it'll simply raise an ERROR saying "panic in walker".
#[pg_extern]
fn crash() {
    use pgrx::{list::List, memcx};
    memcx::current_context(|current| unsafe {
        let mut list = List::downcast_ptr_in_memcx(ptr::null_mut(), current).unwrap();
        list.unstable_push_in_context(ptr::null_mut(), current);

        pg_sys::raw_expression_tree_walker(list.as_mut_ptr().cast(), Some(walker), ptr::null_mut());
    });
}

#[pg_guard]
extern "C-unwind" fn walker(_node: *mut pg_sys::Node, _void: *mut ::core::ffi::c_void) -> bool {
    panic!("panic in walker");
}

#[pg_extern]
fn get_relation_name(oid: pg_sys::Oid) -> String {
    PgTryBuilder::new(|| unsafe {
        // pg_sys::relation_open will raise a XX000 if the specified oid isn't a valid relation
        let rel = PgBox::from_pg(pg_sys::relation_open(oid, pg_sys::AccessShareLock as _));
        let pg_class_entry = PgBox::from_pg(rel.rd_rel);
        let relname = &pg_class_entry.relname;
        name_data_to_str(relname).to_string()
    })
    .catch_when(PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, |_error| {
        // so in the case the oid isn't a valid relation, just return a generic string
        format!("<{oid:?} is not a relation>")
    })
    .execute()
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;
    use std::rc::Rc;
    use std::sync::atomic::{AtomicBool, Ordering};

    use pgrx::prelude::*;

    // see: c5cd61d7bfdfb5236ef0f8b98f433b35a2444346
    #[pg_test]
    fn test_we_dont_blow_out_errdata_stack_size() {
        Spi::run("SELECT get_relation_name(x) FROM generate_series(1, 1000) x")
            .expect("SPI failed");
    }

    #[pg_test(error = "panic in walker")]
    fn test_panic_in_extern_c_fn() -> Result<(), pgrx::spi::Error> {
        Spi::get_one::<()>("SELECT crash()").map(|_| ())
    }

    #[pg_test]
    fn test_pg_try_execute_no_error_no_catch() {
        let result = PgTryBuilder::new(|| 42).execute();
        assert_eq!(42, result)
    }

    #[pg_test(error = "unwrapped a panic")]
    fn test_pg_try_execute_with_error() {
        PgTryBuilder::new(|| panic!("unwrapped a panic")).execute();
    }

    #[pg_test(error = "raised an error")]
    fn test_pg_try_execute_with_error_report() {
        PgTryBuilder::new(|| error!("raised an error")).execute();
    }

    #[pg_test(error = "panic in walker")]
    fn test_pg_try_execute_with_crash() {
        PgTryBuilder::new(|| super::crash()).execute();
    }

    #[pg_test]
    fn test_pg_try_execute_with_crash_ignore() {
        PgTryBuilder::new(|| super::crash())
            .catch_when(PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, |_| ())
            .catch_others(|e| panic!("{e:?}"))
            .execute();
    }

    #[pg_test(error = "panic in walker")]
    fn test_pg_try_execute_with_crash_rethrow() {
        PgTryBuilder::new(|| super::crash())
            .catch_when(PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, |e| e.rethrow())
            .execute();
    }

    #[pg_test]
    fn test_pg_try_execute_no_error() {
        let result = PgTryBuilder::new(|| 42).catch_others(|_| 99).execute();
        assert_eq!(42, result);
    }

    #[pg_test]
    fn test_pg_try_ignore_panic() {
        let result =
            PgTryBuilder::new(|| panic!("unwrapped a panic")).catch_others(|_| 99).execute();
        assert_eq!(99, result);
    }

    #[pg_test]
    fn test_pg_try_no_error_with_catch() {
        let result = PgTryBuilder::new(|| 42).catch_others(|_| 99).execute();
        assert_eq!(42, result);
    }

    #[pg_test(error = "panic in catch")]
    fn test_pg_try_throw_different_error() {
        PgTryBuilder::new(|| panic!("unwrapped a panic"))
            .catch_others(|_| panic!("panic in catch"))
            .execute();
    }

    #[pg_test]
    fn test_pg_try_catch_and_rethrow_no_error() {
        let result = PgTryBuilder::new(|| 42).catch_others(|e| e.rethrow()).execute();
        assert_eq!(42, result);
    }

    #[pg_test(error = "rethrow a panic")]
    fn test_pg_try_catch_and_rethrow_with_error() {
        PgTryBuilder::new(|| panic!("rethrow a panic")).catch_others(|e| e.rethrow()).execute();
    }

    #[pg_test]
    fn test_pg_try_finally() {
        let mut finally = false;
        let result = PgTryBuilder::new(|| 42).finally(|| finally = true).execute();
        assert_eq!(42, result);
        assert_eq!(true, finally);
    }

    #[pg_test]
    fn test_pg_try_finally_with_catch() {
        let mut finally = false;
        let result = PgTryBuilder::new(|| panic!("panic"))
            .catch_others(|_| 99)
            .finally(|| finally = true)
            .execute();
        assert_eq!(99, result);
        assert_eq!(true, finally);
    }

    #[pg_test]
    fn test_pg_try_finally_with_catch_rethrow() {
        let finally = Rc::new(AtomicBool::new(false));
        let result = std::panic::catch_unwind(|| {
            PgTryBuilder::new(|| panic!("panic"))
                .catch_others(|e| e.rethrow())
                .finally(|| finally.store(true, Ordering::SeqCst))
                .execute()
        });
        // finally block does run, even if a catch block throws
        assert_eq!(true, finally.load(Ordering::SeqCst));
        assert!(result.is_err());
    }

    #[pg_test]
    fn test_drop() {
        struct Foo(Rc<AtomicBool>);
        impl Drop for Foo {
            fn drop(&mut self) {
                self.0.store(true, Ordering::SeqCst);
            }
        }

        let outer = Rc::new(AtomicBool::new(false));
        let inner = Rc::new(AtomicBool::new(false));
        {
            let _outer = Foo(outer.clone());

            PgTryBuilder::new(|| {
                let _inner = Foo(inner.clone());
            })
            .execute();
        }

        assert_eq!(true, inner.load(Ordering::SeqCst));
        assert_eq!(true, outer.load(Ordering::SeqCst));
    }

    #[pg_test]
    fn test_drop_with_panic_no_catch() {
        struct Foo(Rc<AtomicBool>);
        impl Drop for Foo {
            fn drop(&mut self) {
                self.0.store(true, Ordering::SeqCst);
            }
        }

        let outer = Rc::new(AtomicBool::new(false));
        let inner = Rc::new(AtomicBool::new(false));
        {
            let _outer = Foo(outer.clone());

            std::panic::catch_unwind(|| {
                PgTryBuilder::new(|| {
                    let _inner = Foo(inner.clone());
                    panic!("get out")
                })
                .execute()
            })
            .ok();
        }
        assert_eq!(true, inner.load(Ordering::SeqCst));
        assert_eq!(true, outer.load(Ordering::SeqCst));
    }

    #[pg_test]
    fn test_drop_with_panic_catch_and_rethrow() {
        struct Foo(Rc<AtomicBool>);
        impl Drop for Foo {
            fn drop(&mut self) {
                self.0.store(true, Ordering::SeqCst);
            }
        }

        let outer = Rc::new(AtomicBool::new(false));
        let inner = Rc::new(AtomicBool::new(false));
        {
            let _outer = Foo(outer.clone());

            std::panic::catch_unwind(|| {
                PgTryBuilder::new(|| {
                    let _inner = Foo(inner.clone());
                    panic!("get out")
                })
                .catch_others(|e| e.rethrow())
                .execute()
            })
            .ok();
        }
        assert_eq!(true, inner.load(Ordering::SeqCst));
        assert_eq!(true, outer.load(Ordering::SeqCst));
    }

    #[pg_test]
    fn test_drop_with_panic_catch_and_ignore() {
        struct Foo(Rc<AtomicBool>);
        impl Drop for Foo {
            fn drop(&mut self) {
                self.0.store(true, Ordering::SeqCst);
            }
        }

        let outer = Rc::new(AtomicBool::new(false));
        let inner = Rc::new(AtomicBool::new(false));
        {
            let _outer = Foo(outer.clone());

            std::panic::catch_unwind(|| {
                PgTryBuilder::new(|| {
                    let _inner = Foo(inner.clone());
                    panic!("get out")
                })
                .catch_others(|_| ())
                .execute()
            })
            .ok();
        }
        assert_eq!(true, inner.load(Ordering::SeqCst));
        assert_eq!(true, outer.load(Ordering::SeqCst));
    }

    #[pg_test]
    fn test_drop_with_panic_and_ignore_and_finally() {
        struct Foo(Rc<AtomicBool>);
        impl Drop for Foo {
            fn drop(&mut self) {
                self.0.store(true, Ordering::SeqCst);
            }
        }

        let outer = Rc::new(AtomicBool::new(false));
        let inner = Rc::new(AtomicBool::new(false));
        let finally = Rc::new(AtomicBool::new(false));
        {
            let _outer = Foo(outer.clone());

            std::panic::catch_unwind(|| {
                PgTryBuilder::new(|| {
                    let _inner = Foo(inner.clone());
                    panic!("get out")
                })
                .catch_others(|_| ())
                .finally(|| finally.store(true, Ordering::SeqCst))
                .execute()
            })
            .ok();
        }
        assert_eq!(true, inner.load(Ordering::SeqCst));
        assert_eq!(true, outer.load(Ordering::SeqCst));

        // really just testing that the finally block ran
        assert_eq!(true, finally.load(Ordering::SeqCst));
    }
}

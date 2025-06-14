//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::pg_sys::panic::CaughtError;
use pgrx::prelude::*;

::pgrx::pg_module_magic!();

#[pg_extern]
fn is_valid_number(i: i32) -> i32 {
    let mut finished = false;
    let result = PgTryBuilder::new(|| {
        if i < 42 {
            ereport!(ERROR, PgSqlErrorCode::ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE, "number too small");
        }
        i
    })
    // force an invalid number to be 42
    .catch_when(PgSqlErrorCode::ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE, |cause| cause.rethrow())
    .finally(|| finished = true)
    .execute();

    assert!(finished);

    result
}

#[pg_extern]
fn get_relation_name(oid: pg_sys::Oid) -> String {
    PgTryBuilder::new(|| unsafe {
        // `pg_sys::relation_open()` will raise XX000 if the specified oid isn't a valid relation
        let rel = PgBox::from_pg(pg_sys::relation_open(oid, pg_sys::AccessShareLock as _));
        let pg_class_entry = PgBox::from_pg(rel.rd_rel);
        let relname = &pg_class_entry.relname;
        name_data_to_str(relname).to_string()
    })
    // in this case just return a generic string
    .catch_when(PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, |_cause| {
        format!("<{oid:?} is not a relation>")

        // NB:  It's probably not a "good idea" to catch an ERRCODE_INTERNAL_ERROR and ignore it
        // like we are here, but for demonstration purposes, this will suffice
    })
    // finally block always runs after the catch handlers finish (even if they rethrow or raise their own panic)
    .finally(|| warning!("FINALLY!"))
    .execute()
}

#[pg_extern]
fn maybe_panic(panic: bool, trap_it: bool, message: &str) {
    PgTryBuilder::new(|| {
        if panic {
            panic!("panic says: {}", message)
            // std::panic::panic_any(42)
        }
    })
    .catch_rust_panic(|cause| {
        // we can catch general Rust panics.  The `error` argument is a pg_sys::panic::CaughtError::RustPanic
        // enum variant with the payload from the originating panic
        if trap_it {
            if let CaughtError::RustPanic { ereport, payload } = &cause {
                warning!("{:#?}", ereport);
                if let Some(s) = payload.downcast_ref::<String>() {
                    // we have access to the panic!() message
                    warning!("{}", s);
                    return;
                } else {
                    // this won't happen with this example, but say the `panic_any(42)` was used
                    // instead.  Then we'd be here, and we can just raise another `panic!()`, which
                    // will be what's ultimately reported to Postgres.
                    //
                    // In this case, Postgres' LOCATION error slot will be this line, and the CONTEXT
                    // slot will show the line number of the original `panic_any(42)` above
                    panic!("panic payload not a `String`");
                }
            }
            unreachable!("internal error:  `CaughtError` not a `::RustPanic`");
        } else {
            cause.rethrow()
        }
    })
    // finally block always runs after the catch handlers finish (even if they rethrow or raise
    // their own panic, like in this case)
    .finally(|| warning!("FINALLY!"))
    .execute()
}

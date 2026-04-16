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

#[pg_guard]
#[no_mangle]
pub extern "C-unwind" fn bgworker(arg: pg_sys::Datum) {
    use pgrx::bgworkers::*;
    use std::time::Duration;
    BackgroundWorker::attach_signal_handlers(SignalWakeFlags::SIGHUP | SignalWakeFlags::SIGTERM);
    BackgroundWorker::connect_worker_to_spi(
        Some(crate::framework::get_pg_dbname()),
        Some(crate::framework::get_pg_user().as_str()),
    );

    let arg = unsafe { i32::from_datum(arg, false) }.expect("invalid arg");

    if arg > 0 {
        BackgroundWorker::transaction(|| {
            Spi::run("CREATE TABLE tests.bgworker_test (v INTEGER);")?;
            Spi::connect_mut(|client| {
                client
                    .update("INSERT INTO tests.bgworker_test VALUES ($1);", None, &[arg.into()])
                    .map(|_| ())
            })
        })
        .expect("bgworker transaction failed");
    }
    while BackgroundWorker::wait_latch(Some(Duration::from_millis(100))) {}
    if arg > 0 {
        BackgroundWorker::transaction(|| Spi::run("UPDATE tests.bgworker_test SET v = v + 1;"))
            .expect("bgworker transaction failed")
    }
}

#[pg_guard]
#[no_mangle]
/// Here we test that `BackgroundWorker::transaction` can return data from the closure
pub extern "C-unwind" fn bgworker_return_value(arg: pg_sys::Datum) {
    use pgrx::bgworkers::*;
    use std::time::Duration;
    BackgroundWorker::attach_signal_handlers(SignalWakeFlags::SIGHUP | SignalWakeFlags::SIGTERM);
    BackgroundWorker::connect_worker_to_spi(
        Some(crate::framework::get_pg_dbname()),
        Some(crate::framework::get_pg_user().as_str()),
    );

    let arg = unsafe { i32::from_datum(arg, false) }.expect("invalid arg");

    let val = if arg > 0 {
        BackgroundWorker::transaction(|| {
            Spi::run("CREATE TABLE tests.bgworker_test_return (v INTEGER);")?;
            Spi::get_one_with_args::<i32>("SELECT $1", &[arg.into()])
        })
        .expect("bgworker transaction failed")
        .unwrap()
    } else {
        0
    };
    while BackgroundWorker::wait_latch(Some(Duration::from_millis(100))) {}
    BackgroundWorker::transaction(|| {
        Spi::connect_mut(|c| {
            c.update("INSERT INTO tests.bgworker_test_return VALUES ($1)", None, &[val.into()])
                .map(|_| ())
        })
    })
    .expect("bgworker transaction failed");
}

#[pg_guard]
#[no_mangle]
/// Simple background worker that waits to be terminated; used to test behaviour in case of worker slots exhaustion
pub extern "C-unwind" fn bgworker_sleep() {
    use pgrx::bgworkers::*;
    BackgroundWorker::attach_signal_handlers(SignalWakeFlags::SIGHUP | SignalWakeFlags::SIGTERM);
    while BackgroundWorker::wait_latch(None) {}
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::bgworkers::*;
    use pgrx::prelude::*;
    use pgrx::{pg_sys, IntoDatum};
    #[pg_test]
    fn test_dynamic_bgworker() {
        // Required to avoid bgworker pool exhaustion errors, see `test_dynamic_worker_allocation_failure`
        Spi::run("SELECT pg_advisory_xact_lock_shared(42)").unwrap();
        let worker = BackgroundWorkerBuilder::new("dynamic_bgworker")
            .set_library("pgrx_tests")
            .set_function("bgworker")
            .set_argument(123i32.into_datum())
            .enable_spi_access()
            .set_notify_pid(unsafe { pg_sys::MyProcPid })
            .load_dynamic()
            .expect("Failed to start worker");
        let pid = worker.wait_for_startup().expect("no PID from the worker");
        assert!(pid > 0);
        let handle = worker.terminate();
        handle.wait_for_shutdown().expect("aborted shutdown");

        assert_eq!(Ok(Some(124)), Spi::get_one::<i32>("SELECT v FROM tests.bgworker_test;"));
    }

    #[pg_test]
    fn test_dynamic_bgworker_untracked() {
        // Required to avoid bgworker pool exhaustion errors, see `test_dynamic_worker_allocation_failure`
        Spi::run("SELECT pg_advisory_xact_lock_shared(42)").unwrap();
        let worker = BackgroundWorkerBuilder::new("dynamic_bgworker")
            .set_library("pgrx_tests")
            .set_function("bgworker")
            .set_argument(0i32.into_datum())
            .enable_spi_access()
            .load_dynamic()
            .expect("Failed to start worker");
        assert!(matches!(worker.wait_for_startup(), Err(BackgroundWorkerStatus::Untracked { .. })));
        assert!(matches!(
            worker.wait_for_shutdown(),
            Err(BackgroundWorkerStatus::Untracked { .. })
        ));
    }

    #[pg_test]
    fn test_dynamic_bgworker_untracked_termination_handle() {
        // Required to avoid bgworker pool exhaustion errors, see `test_dynamic_worker_allocation_failure`
        Spi::run("SELECT pg_advisory_xact_lock_shared(42)").unwrap();
        let worker = BackgroundWorkerBuilder::new("dynamic_bgworker")
            .set_library("pgrx_tests")
            .set_function("bgworker")
            .set_argument(0i32.into_datum())
            .enable_spi_access()
            .load_dynamic()
            .expect("Failed to start worker");
        let handle = worker.terminate();
        assert!(matches!(
            handle.wait_for_shutdown(),
            Err(BackgroundWorkerStatus::Untracked { .. })
        ));
    }

    #[pg_test]
    fn test_background_worker_transaction_return() {
        // Required to avoid bgworker pool exhaustion errors, see `test_dynamic_worker_allocation_failure`
        Spi::run("SELECT pg_advisory_xact_lock_shared(42)").unwrap();
        let worker = BackgroundWorkerBuilder::new("dynamic_bgworker")
            .set_library("pgrx_tests")
            .set_function("bgworker_return_value")
            .set_argument(123i32.into_datum())
            .enable_spi_access()
            .set_notify_pid(unsafe { pg_sys::MyProcPid })
            .load_dynamic()
            .expect("Failed to start worker");
        let pid = worker.wait_for_startup().expect("no PID from the worker");
        assert!(pid > 0);
        let handle = worker.terminate();
        handle.wait_for_shutdown().expect("aborted shutdown");

        assert_eq!(Ok(Some(123)), Spi::get_one::<i32>("SELECT v FROM tests.bgworker_test_return;"));
    }

    #[pg_test]
    fn test_dynamic_worker_allocation_failure() {
        // This test temporarily exhausts the max_worker_processes slots, so needs to be run in isolation
        // from other tests that require starting background workers to avoid spurious failures
        // We use an advisory RW lock for this
        Spi::run("SELECT pg_advisory_xact_lock(42)").unwrap();
        let max_proc = Spi::get_one::<i32>("SELECT current_setting('max_worker_processes')::int")
            .expect("failed to get max_worker_processes")
            .expect("got null for max_worker_processes");
        let available_proc = max_proc - 1; // One worker process for logical replication launcher

        let results = (0..available_proc + 1)
            .map(|_| {
                BackgroundWorkerBuilder::new("dynamic_bgworker")
                    .set_library("pgrx_tests")
                    .set_function("bgworker_sleep")
                    .enable_shmem_access(None)
                    .set_notify_pid(unsafe { pg_sys::MyProcPid })
                    .load_dynamic()
            })
            .collect::<Vec<_>>();
        let has_failures = results.iter().any(|w| w.is_err());
        for worker in results.into_iter().filter_map(|r| r.ok()) {
            let handle = worker.terminate();
            handle.wait_for_shutdown().expect("aborted shutdown");
        }
        assert!(has_failures);
    }
}

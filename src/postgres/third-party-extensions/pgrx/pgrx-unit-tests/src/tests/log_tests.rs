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
    use pgrx::prelude::*;
    use std::sync::atomic::{AtomicBool, Ordering};

    #[pg_test]
    fn test_info() {
        info!("info message");
    }

    #[pg_test]
    fn test_log() {
        log!("log message");
    }

    #[pg_test]
    fn test_warn() {
        warning!("warn message");
    }

    #[pg_test]
    fn test_notice() {
        notice!("notice message");
    }

    #[pg_test]
    fn test_debug5() {
        debug5!("debug5 message");
    }

    #[pg_test]
    fn test_debug4() {
        debug4!("debug4 message");
    }

    #[pg_test]
    fn test_debug3() {
        debug3!("debug3 message");
    }

    #[pg_test]
    fn test_debug2() {
        debug2!("debug2 message");
    }

    #[pg_test]
    fn test_debug1() {
        debug1!("debug1 message");
    }

    #[pg_test(error = "error message")]
    fn test_error() {
        error!("error message");
    }

    #[pg_test]
    fn test_check_for_interrupts() {
        check_for_interrupts!();
    }

    #[pg_test(error = "ereport error")]
    fn test_ereport() {
        pgrx::ereport!(PgLogLevel::ERROR, PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, "ereport error")
    }

    #[pg_test(error = "ereport error")]
    fn test_ereport_domain() {
        pgrx::ereport_domain!(
            PgLogLevel::ERROR,
            "test_extension_domain",
            PgSqlErrorCode::ERRCODE_INTERNAL_ERROR,
            "ereport error"
        )
    }

    #[pg_test]
    fn test_ereport_domain_value() {
        let caught = pgrx::PgTryBuilder::new(|| -> Option<pgrx::pg_sys::panic::CaughtError> {
            pgrx::ereport_domain!(
                PgLogLevel::ERROR,
                "test_extension_domain",
                PgSqlErrorCode::ERRCODE_INTERNAL_ERROR,
                "ereport error"
            );
            None
        })
        .catch_when(PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, |e| Some(e))
        .execute();

        match caught {
            Some(pgrx::pg_sys::panic::CaughtError::ErrorReport(report))
            | Some(pgrx::pg_sys::panic::CaughtError::PostgresError(report)) => {
                assert_eq!(report.sql_error_code(), PgSqlErrorCode::ERRCODE_INTERNAL_ERROR);
                assert_eq!(report.message(), "ereport error");
                assert_eq!(report.domain(), Some("test_extension_domain"));
            }
            Some(other) => panic!("unexpected error kind: {other:?}"),
            None => panic!("expected error, but code returned normally"),
        }
    }

    #[pg_test(error = "panic message")]
    fn test_panic() {
        panic!("panic message")
    }

    static FORMAT_ARG_EVALUATED: AtomicBool = AtomicBool::new(false);

    fn evaluate_format_arg() -> &'static str {
        FORMAT_ARG_EVALUATED.store(true, Ordering::SeqCst);
        "evaluated"
    }

    // With default settings (log_min_messages=WARNING, client_min_messages=NOTICE),
    // DEBUG-level messages are not interesting, so we shouldn't be evaluating format arguments
    // eagerly.
    #[pg_test]
    fn test_debug_skips_arg_evaluation() {
        FORMAT_ARG_EVALUATED.store(false, Ordering::SeqCst);
        debug5!("{}", evaluate_format_arg());
        debug4!("{}", evaluate_format_arg());
        debug3!("{}", evaluate_format_arg());
        debug2!("{}", evaluate_format_arg());
        debug1!("{}", evaluate_format_arg());
        assert!(
            !FORMAT_ARG_EVALUATED.load(Ordering::SeqCst),
            "DEBUG-level messages should not evaluate format arguments eagerly at default log level"
        );
    }

    #[pg_test]
    fn test_warning_evaluates_args() {
        // WARNING is interesting at default settings (log_min_messages=WARNING)
        FORMAT_ARG_EVALUATED.store(false, Ordering::SeqCst);
        warning!("{}", evaluate_format_arg());
        assert!(
            FORMAT_ARG_EVALUATED.load(Ordering::SeqCst),
            "warning! should evaluate format arguments at default log level"
        );
    }
}

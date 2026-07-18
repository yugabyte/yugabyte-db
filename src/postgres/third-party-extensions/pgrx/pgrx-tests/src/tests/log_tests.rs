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
    use pgrx::prelude::*;

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

    #[pg_test(error = "panic message")]
    fn test_panic() {
        panic!("panic message")
    }
}

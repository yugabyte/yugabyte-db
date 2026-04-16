//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Access to Postgres' logging system

/// Postgres' various logging levels
#[allow(dead_code)]
#[derive(Clone, Copy, Debug, Ord, PartialOrd, PartialEq, Eq)]
pub enum PgLogLevel {
    /// Debugging messages, in categories of decreasing detail
    DEBUG5 = crate::DEBUG5 as isize,

    /// Debugging messages, in categories of decreasing detail
    DEBUG4 = crate::DEBUG4 as isize,

    /// Debugging messages, in categories of decreasing detail
    DEBUG3 = crate::DEBUG3 as isize,

    /// Debugging messages, in categories of decreasing detail
    DEBUG2 = crate::DEBUG2 as isize,

    /// Debugging messages, in categories of decreasing detail
    /// NOTE:  used by GUC debug_* variables
    DEBUG1 = crate::DEBUG1 as isize,

    /// Server operational messages; sent only to server log by default.
    LOG = crate::LOG as isize,

    /// Same as LOG for server reporting, but never sent to client.
    #[allow(non_camel_case_types)]
    LOG_SERVER_ONLY = crate::LOG_SERVER_ONLY as isize,

    /// Messages specifically requested by user (eg VACUUM VERBOSE output); always sent to client
    /// regardless of client_min_messages, but by default not sent to server log.
    INFO = crate::INFO as isize,

    /// Helpful messages to users about query operation; sent to client and not to server log by default.
    NOTICE = crate::NOTICE as isize,

    /// Warnings.  \[NOTICE\] is for expected messages like implicit sequence creation by SERIAL.
    /// \[WARNING\] is for unexpected messages.
    WARNING = crate::WARNING as isize,

    /// user error - abort transaction; return to known state
    ERROR = crate::PGERROR as isize,

    /// fatal error - abort process
    FATAL = crate::FATAL as isize,

    /// take down the other backends with me
    PANIC = crate::PANIC as isize,
}

impl From<isize> for PgLogLevel {
    #[inline]
    fn from(i: isize) -> Self {
        if i == PgLogLevel::DEBUG5 as isize {
            PgLogLevel::DEBUG5
        } else if i == PgLogLevel::DEBUG4 as isize {
            PgLogLevel::DEBUG4
        } else if i == PgLogLevel::DEBUG3 as isize {
            PgLogLevel::DEBUG3
        } else if i == PgLogLevel::DEBUG2 as isize {
            PgLogLevel::DEBUG2
        } else if i == PgLogLevel::DEBUG1 as isize {
            PgLogLevel::DEBUG1
        } else if i == PgLogLevel::INFO as isize {
            PgLogLevel::INFO
        } else if i == PgLogLevel::NOTICE as isize {
            PgLogLevel::NOTICE
        } else if i == PgLogLevel::WARNING as isize {
            PgLogLevel::WARNING
        } else if i == PgLogLevel::ERROR as isize {
            PgLogLevel::ERROR
        } else if i == PgLogLevel::FATAL as isize {
            PgLogLevel::FATAL
        } else if i == PgLogLevel::PANIC as isize {
            PgLogLevel::PANIC
        } else {
            // ERROR seems like a good default
            PgLogLevel::ERROR
        }
    }
}

impl From<i32> for PgLogLevel {
    #[inline]
    fn from(i: i32) -> Self {
        (i as isize).into()
    }
}

/// Log to Postgres' `debug5` log level.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
///
/// The output these logs goes to the PostgreSQL log file at `DEBUG5` level, depending on how the
/// [PostgreSQL settings](https://www.postgresql.org/docs/current/runtime-config-logging.html) are configured.
#[macro_export]
macro_rules! debug5 {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::DEBUG5, $crate::errcodes::PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION, alloc::format!($($arg)*).as_str());
        }
    )
}

/// Log to Postgres' `debug4` log level.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
///
/// The output these logs goes to the PostgreSQL log file at `DEBUG4` level, depending on how the
/// [PostgreSQL settings](https://www.postgresql.org/docs/current/runtime-config-logging.html) are configured.
#[macro_export]
macro_rules! debug4 {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::DEBUG4, $crate::errcodes::PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION, alloc::format!($($arg)*).as_str());
        }
    )
}

/// Log to Postgres' `debug3` log level.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
///
/// The output these logs goes to the PostgreSQL log file at `DEBUG3` level, depending on how the
/// [PostgreSQL settings](https://www.postgresql.org/docs/current/runtime-config-logging.html) are configured.
#[macro_export]
macro_rules! debug3 {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::DEBUG3, $crate::errcodes::PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION, alloc::format!($($arg)*).as_str());
        }
    )
}

/// Log to Postgres' `debug2` log level.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
///
/// The output these logs goes to the PostgreSQL log file at `DEBUG2` level, depending on how the
/// [PostgreSQL settings](https://www.postgresql.org/docs/current/runtime-config-logging.html) are configured.
#[macro_export]
macro_rules! debug2 {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::DEBUG2, $crate::errcodes::PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION, alloc::format!($($arg)*).as_str());
        }
    )
}

/// Log to Postgres' `debug1` log level.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
///
/// The output these logs goes to the PostgreSQL log file at `DEBUG1` level, depending on how the
/// [PostgreSQL settings](https://www.postgresql.org/docs/current/runtime-config-logging.html) are configured.
#[macro_export]
macro_rules! debug1 {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::DEBUG1, $crate::errcodes::PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION, alloc::format!($($arg)*).as_str());
        }
    )
}

/// Log to Postgres' `log` log level.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
///
/// The output these logs goes to the PostgreSQL log file at `LOG` level, depending on how the
/// [PostgreSQL settings](https://www.postgresql.org/docs/current/runtime-config-logging.html) are configured.
#[macro_export]
macro_rules! log {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::LOG, $crate::errcodes::PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION, alloc::format!($($arg)*).as_str());
        }
    )
}

/// Log to Postgres' `info` log level.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
#[macro_export]
macro_rules! info {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::INFO, $crate::errcodes::PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION, alloc::format!($($arg)*).as_str());
        }
    )
}

/// Log to Postgres' `notice` log level.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
#[macro_export]
macro_rules! notice {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::NOTICE, $crate::errcodes::PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION, alloc::format!($($arg)*).as_str());
        }
    )
}

/// Log to Postgres' `warning` log level.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
#[macro_export]
macro_rules! warning {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::WARNING, $crate::errcodes::PgSqlErrorCode::ERRCODE_WARNING, alloc::format!($($arg)*).as_str());
        }
    )
}

/// Log to Postgres' `error` log level.  This will abort the current Postgres transaction.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
#[macro_export]
macro_rules! error {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::ERROR, $crate::errcodes::PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, alloc::format!($($arg)*).as_str());
            unreachable!()
        }
    );
}

/// Log to Postgres' `fatal` log level.  This will abort the current Postgres backend connection process.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
#[allow(non_snake_case)]
#[macro_export]
macro_rules! FATAL {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::FATAL, $crate::errcodes::PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, alloc::format!($($arg)*).as_str());
            unreachable!()
        }
    )
}

/// Log to Postgres' `panic` log level.  This will cause the entire Postgres cluster to crash.
///
/// This macro accepts arguments like the [`println`] and [`format`] macros.
/// See [`fmt`](std::fmt) for information about options.
#[allow(non_snake_case)]
#[macro_export]
macro_rules! PANIC {
    ($($arg:tt)*) => (
        {
            extern crate alloc;
            $crate::ereport!($crate::elog::PgLogLevel::PANIC, $crate::errcodes::PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, alloc::format!($($arg)*).as_str());
            unreachable!()
        }
    )
}

// shamelessly borrowed from https://docs.rs/stdext/0.2.1/src/stdext/macros.rs.html#61-72
/// This macro returns the name of the enclosing function.
/// As the internal implementation is based on the [`std::any::type_name`], this macro derives
/// all the limitations of this function.
///
/// [`std::any::type_name`]: https://doc.rust-lang.org/std/any/fn.type_name.html
#[macro_export]
macro_rules! function_name {
    () => {{
        // Okay, this is ugly, I get it. However, this is the best we can get on a stable rust.
        fn f() {}
        fn type_name_of<T>(_: T) -> &'static str {
            core::any::type_name::<T>()
        }
        let name = type_name_of(f);
        // `3` is the length of the `::f`.
        &name[..name.len() - 3]
    }};
}

/// Sends some kind of message to Postgres, and if it's a [PgLogLevel::ERROR] or greater, Postgres'
/// error handling takes over and, in the case of [PgLogLevel::ERROR], aborts the current transaction.
///
/// This macro is necessary when one needs to supply a specific SQL error code as part of their
/// error message.
///
/// The argument order is:
/// - `log_level: [PgLogLevel]`
/// - `error_code: [PgSqlErrorCode]`
/// - `message: String`
/// - (optional) `detail: String`
///
/// ## Examples
///
/// ```rust,no_run
/// # use pgrx_pg_sys::ereport;
/// # use pgrx_pg_sys::elog::PgLogLevel;
/// # use pgrx_pg_sys::errcodes::PgSqlErrorCode;
/// ereport!(PgLogLevel::ERROR, PgSqlErrorCode::ERRCODE_INTERNAL_ERROR, "oh noes!"); // abort the transaction
/// ```
///
/// ```rust,no_run
/// # use pgrx_pg_sys::ereport;
/// # use pgrx_pg_sys::elog::PgLogLevel;
/// # use pgrx_pg_sys::errcodes::PgSqlErrorCode;
/// ereport!(PgLogLevel::LOG, PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION, "this is just a message"); // log output only
/// ```
#[macro_export]
macro_rules! ereport {
    (ERROR, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::ERROR);
        unreachable!();
    };

    (PANIC, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::PANIC);
        unreachable!();
    };

    (FATAL, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::FATAL);
        unreachable!();
    };

    (WARNING, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::WARNING)
    };

    (NOTICE, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::NOTICE)
    };

    (INFO, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::INFO)
    };

    (LOG, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::LOG)
    };

    (DEBUG5, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::DEBUG5)
    };

    (DEBUG4, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::DEBUG4)
    };

    (DEBUG3, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::DEBUG3)
    };

    (DEBUG2, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::DEBUG2)
    };

    (DEBUG1, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($crate::elog::PgLogLevel::DEBUG1)
    };

    ($loglevel:expr, $errcode:expr, $message:expr $(, $detail:expr)? $(,)?) => {
        $crate::panic::ErrorReport::new($errcode, $message, $crate::function_name!())
            $(.set_detail($detail))?
            .report($loglevel);
    };
}

/// Is an interrupt pending?
#[inline]
pub fn interrupt_pending() -> bool {
    unsafe { crate::InterruptPending != 0 }
}

/// If an interrupt is pending (perhaps a user-initiated "cancel query" message to this backend),
/// this will safely abort the current transaction
#[macro_export]
macro_rules! check_for_interrupts {
    () => {
        #[allow(unused_unsafe)]
        unsafe {
            if $crate::InterruptPending != 0 {
                $crate::ProcessInterrupts();
            }
        }
    };
}

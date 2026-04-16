//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Safely create Postgres Background Workers, including with full SPI support
//!
//! See: [https://www.postgresql.org/docs/current/bgworker.html](https://www.postgresql.org/docs/current/bgworker.html)
#![allow(clippy::assign_op_pattern)]
#![allow(clippy::useless_transmute)]
use crate::pg_sys;
use pgrx_pg_sys::PgTryBuilder;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

pub static mut PREV_SHMEM_STARTUP_HOOK: Option<unsafe extern "C-unwind" fn()> = None;
static GOT_SIGHUP: AtomicBool = AtomicBool::new(false);
static GOT_SIGTERM: AtomicBool = AtomicBool::new(false);
static GOT_SIGINT: AtomicBool = AtomicBool::new(false);
static GOT_SIGCHLD: AtomicBool = AtomicBool::new(false);

bitflags! {
    struct BGWflags: i32 {
        const BGWORKER_SHMEM_ACCESS                = pg_sys::BGWORKER_SHMEM_ACCESS as i32;
        const BGWORKER_BACKEND_DATABASE_CONNECTION = pg_sys::BGWORKER_BACKEND_DATABASE_CONNECTION as i32;
    }
}

bitflags! {
    /// Flags to indicate when a BackgroundWorker should be awaken
    pub struct SignalWakeFlags: i32 {
        const SIGHUP = 0x1;
        const SIGTERM = 0x2;
        const SIGINT = 0x4;
        const SIGCHLD = 0x8;
    }
}

bitflags! {
    struct WLflags: i32 {
        const WL_LATCH_SET         = pg_sys::WL_LATCH_SET as i32;
        const WL_SOCKET_READABLE   = pg_sys::WL_SOCKET_READABLE as i32;
        const WL_SOCKET_WRITEABLE  = pg_sys::WL_SOCKET_WRITEABLE as i32;
        const WL_TIMEOUT           = pg_sys::WL_TIMEOUT as i32;
        const WL_POSTMASTER_DEATH  = pg_sys::WL_POSTMASTER_DEATH as i32;
        const WL_SOCKET_CONNECTED  = pg_sys::WL_SOCKET_WRITEABLE as i32;
        const WL_SOCKET_MASK       = (pg_sys::WL_SOCKET_READABLE | pg_sys::WL_SOCKET_WRITEABLE | pg_sys::WL_SOCKET_CONNECTED) as i32;
    }
}

/// The various points in which a BackgroundWorker can be started by Postgres
#[derive(Copy, Clone)]
pub enum BgWorkerStartTime {
    PostmasterStart = pg_sys::BgWorkerStartTime::BgWorkerStart_PostmasterStart as isize,
    ConsistentState = pg_sys::BgWorkerStartTime::BgWorkerStart_ConsistentState as isize,
    RecoveryFinished = pg_sys::BgWorkerStartTime::BgWorkerStart_RecoveryFinished as isize,
}

/// Static interface into a running Background Worker
///
/// It also provides a few helper functions as wrappers around the global `pgrx::pg_sys::MyBgworkerEntry`
pub struct BackgroundWorker {}

impl BackgroundWorker {
    /// What is our name?
    pub fn get_name() -> &'static str {
        #[cfg(any(
            feature = "pg13",
            feature = "pg14",
            feature = "pg15",
            feature = "pg16",
            feature = "pg17"
        ))]
        const LEN: usize = 96;

        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
            CStr::from_ptr(std::mem::transmute::<&[c_char; LEN], *const c_char>(
                &(*pg_sys::MyBgworkerEntry).bgw_name,
            ))
        }
        .to_str()
        .expect("should not have non UTF8")
    }

    /// Retrieve the `extra` data provided to the `BackgroundWorkerBuilder`
    pub fn get_extra() -> &'static str {
        const LEN: usize = 128;

        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
            CStr::from_ptr(std::mem::transmute::<&[c_char; LEN], *const c_char>(
                &(*pg_sys::MyBgworkerEntry).bgw_extra,
            ))
        }
        .to_str()
        .expect("'extra' is not valid UTF8")
    }

    /// Have we received a SIGHUP since previously calling this function? This resets the
    /// internal boolean that tracks if SIGHUP was received. So when calling this twice in
    /// in a row, the second time will always return false.
    #[must_use = "you aren't getting the same bool back if you call this twice"]
    pub fn sighup_received() -> bool {
        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
        }
        // toggle the bool to false, returning whatever it was
        GOT_SIGHUP.swap(false, Ordering::SeqCst)
    }

    /// Have we received a SIGTERM since previously calling this function? This resets the
    /// internal boolean that tracks if SIGTERM was received. So when calling this twice in
    /// in a row, the second time will always return false.
    #[must_use = "you aren't getting the same bool back if you call this twice"]
    pub fn sigterm_received() -> bool {
        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
        }
        // toggle the bool to false, returning whatever it was
        GOT_SIGTERM.swap(false, Ordering::SeqCst)
    }

    /// Have we received a SIGINT since previously calling this function? This resets the
    /// internal boolean that tracks if SIGINT was received. So when calling this twice in
    /// in a row, the second time will always return false.
    #[must_use = "you aren't getting the same bool back if you call this twice"]
    pub fn sigint_received() -> bool {
        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
        }
        // toggle the bool to false, returning whatever it was
        GOT_SIGINT.swap(false, Ordering::SeqCst)
    }

    /// Have we received a SIGCHLD since previously calling this function? This resets the
    /// internal boolean that tracks if SIGCHLD was received. So when calling this twice in
    /// in a row, the second time will always return false.
    #[must_use = "you aren't getting the same bool back if you call this twice"]
    pub fn sigchld_received() -> bool {
        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
        }
        // toggle the bool to false, returning whatever it was
        GOT_SIGCHLD.swap(false, Ordering::SeqCst)
    }

    /// Wait for the specified amount of time on the background worker's latch
    ///
    /// Returns true if we're still supposed to be alive and haven't received a SIGTERM
    pub fn wait_latch(timeout: Option<Duration>) -> bool {
        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
        }
        let wakeup_flags = match timeout {
            Some(t) => wait_latch(
                t.as_millis().try_into().unwrap(),
                WLflags::WL_LATCH_SET | WLflags::WL_TIMEOUT | WLflags::WL_POSTMASTER_DEATH,
            ),
            None => wait_latch(0, WLflags::WL_LATCH_SET | WLflags::WL_POSTMASTER_DEATH),
        };

        let postmaster_died = (wakeup_flags & pg_sys::WL_POSTMASTER_DEATH as i32) != 0;
        !BackgroundWorker::sigterm_received() && !postmaster_died
    }

    /// Is this `BackgroundWorker` allowed to continue?
    pub fn worker_continue() -> bool {
        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
        }
        pg_sys::WL_POSTMASTER_DEATH as i32 != 0
    }

    /// Intended to be called once to indicate the database and user to use to
    /// connect to via SPI
    pub fn connect_worker_to_spi(dbname: Option<&str>, username: Option<&str>) {
        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
        }
        let db = dbname.and_then(|rs| CString::new(rs).ok());
        let db: *const c_char = db.as_ref().map_or(std::ptr::null(), |i| i.as_ptr());

        let user = username.and_then(|rs| CString::new(rs).ok());
        let user: *const c_char = user.as_ref().map_or(std::ptr::null(), |i| i.as_ptr());

        unsafe {
            #[cfg(any(
                feature = "pg13",
                feature = "pg14",
                feature = "pg15",
                feature = "pg16",
                feature = "pg17"
            ))]
            pg_sys::BackgroundWorkerInitializeConnection(db, user, 0);
        };
    }

    /// Indicate the set of signal handlers we want to receive.
    ///
    /// You likely always want to do this:
    ///
    /// ```rust,no_run
    /// use pgrx::prelude::*;
    /// use pgrx::bgworkers::{BackgroundWorker, SignalWakeFlags};
    /// BackgroundWorker::attach_signal_handlers(SignalWakeFlags::SIGHUP | SignalWakeFlags::SIGTERM);
    /// ```
    pub fn attach_signal_handlers(wake: SignalWakeFlags) {
        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
            if wake.contains(SignalWakeFlags::SIGHUP) {
                pg_sys::pqsignal(pg_sys::SIGHUP as i32, Some(worker_spi_sighup));
            }
            if wake.contains(SignalWakeFlags::SIGTERM) {
                pg_sys::pqsignal(pg_sys::SIGTERM as i32, Some(worker_spi_sigterm));
            }
            if wake.contains(SignalWakeFlags::SIGINT) {
                pg_sys::pqsignal(pg_sys::SIGINT as i32, Some(worker_spi_sigint));
            }
            if wake.contains(SignalWakeFlags::SIGCHLD) {
                pg_sys::pqsignal(pg_sys::SIGCHLD as i32, Some(worker_spi_sigchld));
            }
            pg_sys::BackgroundWorkerUnblockSignals();
        }
    }

    /// Once connected to SPI via `connect_worker_to_spi()`, begin a transaction to
    /// use the `pgrx::Spi` interface. Returns the return value of the `F` function.
    pub fn transaction<F: FnOnce() -> R + std::panic::UnwindSafe + std::panic::RefUnwindSafe, R>(
        transaction_body: F,
    ) -> R {
        unsafe {
            assert!(!pg_sys::MyBgworkerEntry.is_null(), "BackgroundWorker associated functions can only be called from a registered background worker");
            pg_sys::SetCurrentStatementStartTimestamp();
            pg_sys::StartTransactionCommand();
            pg_sys::PushActiveSnapshot(pg_sys::GetTransactionSnapshot());
        }
        unsafe {
            let result = PgTryBuilder::new(transaction_body).execute();
            pg_sys::PopActiveSnapshot();
            pg_sys::CommitTransactionCommand();
            result
        }
    }
}

unsafe extern "C-unwind" fn worker_spi_sighup(_signal_args: i32) {
    GOT_SIGHUP.store(true, Ordering::SeqCst);
    pg_sys::ProcessConfigFile(pg_sys::GucContext::PGC_SIGHUP);
    pg_sys::SetLatch(pg_sys::MyLatch);
}

unsafe extern "C-unwind" fn worker_spi_sigterm(_signal_args: i32) {
    GOT_SIGTERM.store(true, Ordering::SeqCst);
    pg_sys::SetLatch(pg_sys::MyLatch);
}

unsafe extern "C-unwind" fn worker_spi_sigint(_signal_args: i32) {
    GOT_SIGINT.store(true, Ordering::SeqCst);
    pg_sys::SetLatch(pg_sys::MyLatch);
}

unsafe extern "C-unwind" fn worker_spi_sigchld(_signal_args: i32) {
    GOT_SIGCHLD.store(true, Ordering::SeqCst);
    pg_sys::SetLatch(pg_sys::MyLatch);
}

/// Dynamic background worker handle
pub struct DynamicBackgroundWorker {
    handle: *mut pg_sys::BackgroundWorkerHandle,
    notify_pid: pg_sys::pid_t,
}

/// PID
pub type Pid = pg_sys::pid_t;

/// Dynamic background worker status
#[derive(Debug, Clone, Copy)]
pub enum BackgroundWorkerStatus {
    Started,
    NotYetStarted,
    Stopped,
    PostmasterDied,
    /// `BackgroundWorkerBuilder.bgw_notify_pid` was not set to `pg_sys::MyProcPid`
    ///
    /// This makes worker's startup or shutdown untrackable by the current process.
    Untracked {
        /// `bgw_notify_pid` as specified in the builder
        notify_pid: pg_sys::pid_t,
    },
}

impl From<pg_sys::BgwHandleStatus::Type> for BackgroundWorkerStatus {
    fn from(s: pg_sys::BgwHandleStatus::Type) -> Self {
        use pg_sys::BgwHandleStatus::*;
        match s {
            BGWH_STARTED => BackgroundWorkerStatus::Started,
            BGWH_NOT_YET_STARTED => BackgroundWorkerStatus::NotYetStarted,
            BGWH_STOPPED => BackgroundWorkerStatus::Stopped,
            BGWH_POSTMASTER_DIED => BackgroundWorkerStatus::PostmasterDied,
            _ => unreachable!(),
        }
    }
}

impl DynamicBackgroundWorker {
    /// Return dynamic background worker's PID if the worker is successfully registered,
    /// otherwise it return worker's status as an error.
    pub fn pid(&self) -> Result<Pid, BackgroundWorkerStatus> {
        let mut pid: pg_sys::pid_t = 0;
        let status: BackgroundWorkerStatus =
            unsafe { pg_sys::GetBackgroundWorkerPid(self.handle, &mut pid) }.into();
        match status {
            BackgroundWorkerStatus::Started => Ok(pid),
            _ => Err(status),
        }
    }

    /// Causes the postmaster to send SIGTERM to the worker if it is running,
    /// and to unregister it as soon as it is not.
    pub fn terminate(self) -> TerminatingDynamicBackgroundWorker {
        unsafe {
            pg_sys::TerminateBackgroundWorker(self.handle);
        }
        TerminatingDynamicBackgroundWorker { handle: self.handle, notify_pid: self.notify_pid }
    }

    /// Block until the postmaster has attempted to start the background worker,
    /// or until the postmaster dies. If the background worker is running, the successful return value
    /// will be the worker's PID. Otherwise, the return value will be an error with the worker's status.
    ///
    /// Requires `BackgroundWorkerBuilder.bgw_notify_pid` to be set to `pg_sys::MyProcPid`, otherwise it'll
    /// return [`BackgroundWorkerStatus::Untracked`] error
    pub fn wait_for_startup(&self) -> Result<Pid, BackgroundWorkerStatus> {
        unsafe {
            if self.notify_pid != pg_sys::MyProcPid as pg_sys::pid_t {
                return Err(BackgroundWorkerStatus::Untracked { notify_pid: self.notify_pid });
            }
        }
        let mut pid: pg_sys::pid_t = 0;
        let status: BackgroundWorkerStatus =
            unsafe { pg_sys::WaitForBackgroundWorkerStartup(self.handle, &mut pid) }.into();
        match status {
            BackgroundWorkerStatus::Started => Ok(pid),
            _ => Err(status),
        }
    }

    /// Block until the background worker exits, or postmaster dies. When the background worker exits, the return value is unit,
    /// if postmaster dies it will return error with `BackgroundWorkerStatus::PostmasterDied` status
    ///
    /// Requires `BackgroundWorkerBuilder.bgw_notify_pid` to be set to `pg_sys::MyProcPid`, otherwise it'll
    /// return [`BackgroundWorkerStatus::Untracked`] error
    pub fn wait_for_shutdown(self) -> Result<(), BackgroundWorkerStatus> {
        TerminatingDynamicBackgroundWorker { handle: self.handle, notify_pid: self.notify_pid }
            .wait_for_shutdown()
    }
}

/// Handle of a dynamic background worker that is being terminated with
/// [`DynamicBackgroundWorker::terminate`]. Only allows waiting for shutdown.
pub struct TerminatingDynamicBackgroundWorker {
    handle: *mut pg_sys::BackgroundWorkerHandle,
    notify_pid: pg_sys::pid_t,
}

impl TerminatingDynamicBackgroundWorker {
    /// Block until the background worker exits, or postmaster dies. When the background worker exits, the return value is unit,
    /// if postmaster dies it will return error with `BackgroundWorkerStatus::PostmasterDied` status
    ///
    /// Requires `BackgroundWorkerBuilder.bgw_notify_pid` to be set to `pg_sys::MyProcPid`, otherwise it'll
    /// return [`BackgroundWorkerStatus::Untracked`] error
    pub fn wait_for_shutdown(self) -> Result<(), BackgroundWorkerStatus> {
        unsafe {
            if self.notify_pid != pg_sys::MyProcPid as pg_sys::pid_t {
                return Err(BackgroundWorkerStatus::Untracked { notify_pid: self.notify_pid });
            }
        }
        let status: BackgroundWorkerStatus =
            unsafe { pg_sys::WaitForBackgroundWorkerShutdown(self.handle) }.into();
        match status {
            BackgroundWorkerStatus::Stopped => Ok(()),
            _ => Err(status),
        }
    }
}

/// A builder-style interface for creating a new Background Worker
///
/// For a static background worker, this must be used from within your extension's `_PG_init()` function,
/// finishing with the `.load()` function. Dynamic background workers are loaded with `.load_dynamic()` and
/// have no restriction as to where they can be loaded.
///
/// ## Example
///
/// ```rust,no_run
/// use pgrx::prelude::*;
/// use pgrx::bgworkers::BackgroundWorkerBuilder;
///
/// #[pg_guard]
/// pub extern "C-unwind" fn _PG_init() {
///     BackgroundWorkerBuilder::new("My Example BGWorker")
///         .set_function("background_worker_main")
///         .set_library("example")
///         .enable_spi_access()
///         .load();
/// }
///
/// #[pg_guard]
/// pub extern "C-unwind" fn background_worker_main(_arg: pg_sys::Datum) {
///     // do bgworker stuff here
/// }
/// ```
pub struct BackgroundWorkerBuilder {
    bgw_name: String,
    bgw_type: String,
    bgw_flags: BGWflags,
    bgw_start_time: BgWorkerStartTime,
    bgw_restart_time: Option<Duration>,
    bgw_library_name: String,
    bgw_function_name: String,
    bgw_main_arg: pg_sys::Datum,
    bgw_extra: String,
    bgw_notify_pid: pg_sys::pid_t,
    shared_memory_startup_fn: Option<unsafe extern "C-unwind" fn()>,
    bgw_oom_score_adj: String,
}

impl BackgroundWorkerBuilder {
    /// Construct a new BackgroundWorker of the specified name
    ///
    /// By default, its `type` is also set to the specified name
    /// and it is configured to
    ///     - start at `BgWorkerStartTime::PostmasterStart`.
    ///     - never restart in the event it crashes
    pub fn new(name: &str) -> BackgroundWorkerBuilder {
        BackgroundWorkerBuilder {
            bgw_name: name.to_string(),
            bgw_type: name.to_string(),
            bgw_flags: BGWflags::empty(),
            bgw_start_time: BgWorkerStartTime::PostmasterStart,
            bgw_restart_time: None,
            bgw_library_name: name.to_string(),
            bgw_function_name: name.to_string(),
            bgw_main_arg: pg_sys::Datum::from(0),
            bgw_extra: "".to_string(),
            bgw_notify_pid: 0,
            shared_memory_startup_fn: None,
            bgw_oom_score_adj: "900".to_string(),
        }
    }

    /// What is the type of this BackgroundWorker
    pub fn set_type(mut self, input: &str) -> Self {
        self.bgw_type = input.to_string();
        self
    }

    /// Does this BackgroundWorker want Shared Memory access?
    ///
    /// `startup` allows specifying shared memory initialization startup hook. Ignored
    /// if [`BackgroundWorkerBuilder::load_dynamic`] is used.
    pub fn enable_shmem_access(mut self, startup: Option<unsafe extern "C-unwind" fn()>) -> Self {
        self.bgw_flags = self.bgw_flags | BGWflags::BGWORKER_SHMEM_ACCESS;
        self.shared_memory_startup_fn = startup;
        self
    }

    /// Does this BackgroundWorker intend to use SPI?
    ///
    /// If set, then the configured start time becomes `BgWorkerStartTIme::RecoveryFinished`
    /// as accessing SPI prior to possible database recovery is not possible
    pub fn enable_spi_access(mut self) -> Self {
        self.bgw_flags = self.bgw_flags
            | BGWflags::BGWORKER_SHMEM_ACCESS
            | BGWflags::BGWORKER_BACKEND_DATABASE_CONNECTION;
        self.bgw_start_time = BgWorkerStartTime::RecoveryFinished;
        self
    }

    /// When should this BackgroundWorker be started by Postgres?
    pub fn set_start_time(mut self, input: BgWorkerStartTime) -> Self {
        self.bgw_start_time = input;
        self
    }

    /// the interval, in seconds, that postgres should wait before restarting the process,
    /// in case it crashes. It can be `Some(any positive duration value), or
    /// `None`, indicating not to restart the process in case of a crash.
    pub fn set_restart_time(mut self, input: Option<Duration>) -> Self {
        self.bgw_restart_time = input;
        self
    }

    /// What is the library name that contains the "main" function?
    ///
    /// Typically, this will just be your extension's name
    pub fn set_library(mut self, input: &str) -> Self {
        self.bgw_library_name = input.to_string();
        self
    }

    /// What is the "main" function that should be run when the BackgroundWorker
    /// process is started?
    ///
    /// The specified function **must** be:
    ///     - `extern "C-unwind"`,
    ///     - guarded with `#[pg_guard]`,
    ///     - take 1 argument of type `pgrx::pg_sys::Datum`, and
    ///     - return "void"
    ///
    /// ## Example
    ///
    /// ```rust,no_run
    /// use pgrx::prelude::*;
    ///
    /// #[pg_guard]
    /// pub extern "C-unwind" fn background_worker_main(_arg: pg_sys::Datum) {
    /// }
    /// ```
    pub fn set_function(mut self, input: &str) -> Self {
        self.bgw_function_name = input.to_string();
        self
    }

    /// Datum argument to the background worker main function. This main function should
    /// take a single argument of type Datum and return void. bgw_main_arg will be passed
    /// as the argument. In addition, the global variable MyBgworkerEntry points to a copy
    /// of the BackgroundWorker structure passed at registration time; the worker may find
    /// it helpful to examine this structure.
    ///
    /// On Windows (and anywhere else where EXEC_BACKEND is defined) or in dynamic
    /// background workers it is not safe to pass a Datum by reference, only by value.
    /// If an argument is required, it is safest to pass an int32 or other small value and
    /// use that as an index into an array allocated in shared memory.
    ///
    /// ## Important
    /// If a value like a cstring or text is passed then the pointer won't be valid from
    /// the new background worker process.
    ///
    /// In general, this means that you should stick to primitive Rust types such as `i32`,
    /// `bool`, etc.
    ///
    /// You you use `pgrx`'s `IntoDatum` trait to make the conversion into a datum easy:
    ///
    /// ```rust,no_run
    /// use pgrx::prelude::*;
    /// use pgrx::bgworkers::BackgroundWorkerBuilder;
    ///
    /// BackgroundWorkerBuilder::new("Example")
    ///     .set_function("background_worker_main")
    ///     .set_library("example")
    ///     .set_argument(42i32.into_datum())
    ///     .load();
    /// ```
    pub fn set_argument(mut self, input: Option<pg_sys::Datum>) -> Self {
        self.bgw_main_arg = input.unwrap_or(0.into());
        self
    }

    /// extra data to be passed to the background worker. Unlike bgw_main_arg, this
    /// data is not passed as an argument to the worker's main function, but it can be
    /// accessed via the `BackgroundWorker` struct.
    pub fn set_extra(mut self, input: &str) -> Self {
        self.bgw_extra = input.to_string();
        self
    }

    /// PID of a PostgreSQL backend process to which the postmaster should send SIGUSR1
    /// when the process is started or exits. It should be 0 for workers registered at
    /// postmaster startup time, or when the backend registering the worker does not wish
    /// to wait for the worker to start up. Otherwise, it should be initialized to
    /// `pgrx::pg_sys::MyProcPid`
    pub fn set_notify_pid(mut self, input: i32) -> Self {
        self.bgw_notify_pid = input as pg_sys::pid_t;
        self
    }

    /// Once properly configured, call `load()` to get the BackgroundWorker registered and
    /// started at the proper time by Postgres.
    pub fn load(self) {
        let mut bgw: pg_sys::BackgroundWorker = (&self).into();

        unsafe {
            pg_sys::RegisterBackgroundWorker(&mut bgw);
            if self.bgw_flags.contains(BGWflags::BGWORKER_SHMEM_ACCESS)
                && self.shared_memory_startup_fn.is_some()
            {
                PREV_SHMEM_STARTUP_HOOK = pg_sys::shmem_startup_hook;
                pg_sys::shmem_startup_hook = self.shared_memory_startup_fn;
            }
        };
    }

    /// Once properly configured, call `load_dynamic()` to get the BackgroundWorker registered and started dynamically.
    /// Start up might fail, e.g. if max_worker_processes is exceeded. In that case an Err is returned.
    pub fn load_dynamic(self) -> Result<DynamicBackgroundWorker, ()> {
        let mut bgw: pg_sys::BackgroundWorker = (&self).into();
        let mut handle: *mut pg_sys::BackgroundWorkerHandle = null_mut();

        // SAFETY: bgw and handle are set just above, and postgres guarantees to set handle to a valid pointer in case of success
        let success = unsafe { pg_sys::RegisterDynamicBackgroundWorker(&mut bgw, &mut handle) };

        if !success {
            Err(())
        } else {
            Ok(DynamicBackgroundWorker { handle, notify_pid: bgw.bgw_notify_pid })
        }
    }
}

/// This conversion is useful only in limited context outside of pgrx, such as when this structure is required
/// by other libraries and the worker is not to be started by pgrx itself. In this case,
/// the builder is useful for building this structure.
impl<'a> From<&'a BackgroundWorkerBuilder> for pg_sys::BackgroundWorker {
    fn from(builder: &'a BackgroundWorkerBuilder) -> Self {
        #[cfg(any(
            feature = "pg13",
            feature = "pg14",
            feature = "pg15",
            feature = "pg16",
            feature = "pg17"
        ))]
        let bgw = pg_sys::BackgroundWorker {
            bgw_name: RpgffiChar::from(&builder.bgw_name[..]).0,
            bgw_type: RpgffiChar::from(&builder.bgw_type[..]).0,
            bgw_flags: builder.bgw_flags.bits(),
            bgw_start_time: builder.bgw_start_time as _,
            bgw_restart_time: match builder.bgw_restart_time {
                None => -1,
                Some(d) => d.as_secs() as i32,
            },
            bgw_library_name: {
                #[cfg(not(feature = "pg17"))]
                {
                    RpgffiChar::from(&builder.bgw_library_name[..]).0
                }

                #[cfg(feature = "pg17")]
                {
                    RpgffiChar1024::from(&builder.bgw_library_name[..]).0
                }
            },
            bgw_function_name: RpgffiChar::from(&builder.bgw_function_name[..]).0,
            bgw_main_arg: builder.bgw_main_arg,
            bgw_extra: RpgffiChar128::from(&builder.bgw_extra[..]).0,
            bgw_notify_pid: builder.bgw_notify_pid,
            bgw_oom_score_adj: RpgffiChar::from(&builder.bgw_oom_score_adj[..]).0,
        };

        bgw
    }
}

fn wait_latch(timeout: libc::c_long, wakeup_flags: WLflags) -> i32 {
    unsafe {
        let latch = pg_sys::WaitLatch(
            pg_sys::MyLatch,
            wakeup_flags.bits(),
            timeout,
            pg_sys::PG_WAIT_EXTENSION,
        );
        pg_sys::ResetLatch(pg_sys::MyLatch);
        pg_sys::check_for_interrupts!();

        latch
    }
}

#[cfg(any(
    feature = "pg13",
    feature = "pg14",
    feature = "pg15",
    feature = "pg16",
    feature = "pg17"
))]
type RpgffiChar = RpgffiChar96;

#[allow(dead_code)]
struct RpgffiChar64([c_char; 64]);

impl<'a> From<&'a str> for RpgffiChar64 {
    fn from(string: &str) -> Self {
        let mut r = [0; 64];
        for (dest, src) in r.iter_mut().zip(string.as_bytes()) {
            *dest = *src as c_char;
        }
        RpgffiChar64(r)
    }
}

struct RpgffiChar96([c_char; 96]);

impl<'a> From<&'a str> for RpgffiChar96 {
    fn from(string: &str) -> Self {
        let mut r = [0; 96];
        for (dest, src) in r.iter_mut().zip(string.as_bytes()) {
            *dest = *src as c_char;
        }
        RpgffiChar96(r)
    }
}

struct RpgffiChar128([c_char; 128]);

impl<'a> From<&'a str> for RpgffiChar128 {
    fn from(string: &str) -> Self {
        let mut r = [0; 128];
        for (dest, src) in r.iter_mut().zip(string.as_bytes()) {
            *dest = *src as c_char;
        }
        RpgffiChar128(r)
    }
}

#[allow(dead_code)]
struct RpgffiChar1024([c_char; 1024]);

impl<'a> From<&'a str> for RpgffiChar1024 {
    fn from(string: &str) -> Self {
        let mut r = [0; 1024];
        for (dest, src) in r.iter_mut().zip(string.as_bytes()) {
            *dest = *src as c_char;
        }
        RpgffiChar1024(r)
    }
}

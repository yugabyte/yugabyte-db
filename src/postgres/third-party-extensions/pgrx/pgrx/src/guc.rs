//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Provides a safe interface into Postgres' Configuration System (GUC)
use crate::pg_sys;
use core::ffi::CStr;
pub use pgrx_macros::PostgresGucEnum;
use std::cell::Cell;
use std::ffi::CString;

/// Defines at what level this GUC can be set
pub enum GucContext {
    /// cannot be set by the user at all, but only through
    /// internal processes ("server_version" is an example).  These are GUC
    /// variables only so they can be shown by SHOW, etc.
    Internal = pg_sys::GucContext::PGC_INTERNAL as isize,

    /// can only be set when the postmaster starts,
    /// either from the configuration file or the command line.
    Postmaster = pg_sys::GucContext::PGC_POSTMASTER as isize,

    /// can only be set at postmaster startup or by changing
    /// the configuration file and sending the HUP signal to the postmaster
    /// or a backend process. (Notice that the signal receipt will not be
    /// evaluated immediately. The postmaster and the backend check it at a
    /// certain point in their main loop. It's safer to wait than to read a
    /// file asynchronously.)
    Sighup = pg_sys::GucContext::PGC_SIGHUP as isize,

    /// can only be set at postmaster startup, from the configuration file, or by client request in
    /// the connection startup packet (e.g., from libpq's PGOPTIONS variable), but only when the
    /// user is a superuser. Furthermore, an already-started backend will ignore changes to such an
    /// option in the configuration file.  The idea is that these options are fixed for a given
    /// backend once it's started, but they can vary across backends.
    SuBackend = pg_sys::GucContext::PGC_SU_BACKEND as isize,

    /// can only be set at postmaster startup, from the configuration file, or by client request in
    /// the connection startup packet (e.g., from libpq's PGOPTIONS variable), by any user.
    /// Furthermore, an already-started backend will ignore changes to such an option in the
    /// configuration file.  The idea is that these options are fixed for a given backend once it's
    /// started, but they can vary across backends.
    Backend = pg_sys::GucContext::PGC_BACKEND as isize,

    /// can be set at postmaster startup, with the SIGHUP
    /// mechanism, or from the startup packet or SQL if you're a superuser.
    Suset = pg_sys::GucContext::PGC_SUSET as isize,

    /// can be set by anyone any time.
    Userset = pg_sys::GucContext::PGC_USERSET as isize,
}

bitflags! {
    #[derive(Default, Copy, Clone)]
    /// Flags to control special behaviour for the GUC that these are set on. See their
    /// descriptions below for their behaviour.
    pub struct GucFlags: i32 {
        /// Exclude from SHOW ALL
        const NO_SHOW_ALL = pg_sys::GUC_NO_SHOW_ALL as i32 | pg_sys::GUC_NOT_IN_SAMPLE as i32;
        /// Exclude from RESET ALL
        const NO_RESET_ALL = pg_sys::GUC_NO_RESET_ALL as i32;
        /// Auto-report changes to client
        const REPORT = pg_sys::GUC_REPORT as i32;
        /// Can't set in postgresql.conf
        const DISALLOW_IN_FILE = pg_sys::GUC_DISALLOW_IN_FILE as i32;
        /// Placeholder for custom variable
        const CUSTOM_PLACEHOLDER = pg_sys::GUC_CUSTOM_PLACEHOLDER as i32;
        /// Show only to superuser
        const SUPERUSER_ONLY = pg_sys::GUC_SUPERUSER_ONLY as i32;
        /// Limit string to `NAMEDATALEN-1`
        const IS_NAME = pg_sys::GUC_IS_NAME as i32;
        /// Can't set if security restricted
        const NOT_WHILE_SEC_REST = pg_sys::GUC_NOT_WHILE_SEC_REST as i32;
        /// Can't set in `PG_AUTOCONF_FILENAME`
        const DISALLOW_IN_AUTO_FILE = pg_sys::GUC_DISALLOW_IN_AUTO_FILE as i32;
        /// Value is in kilobytes
        const UNIT_KB = pg_sys::GUC_UNIT_KB as i32;
        /// Value is in blocks
        const UNIT_BLOCKS = pg_sys::GUC_UNIT_BLOCKS as i32;
        /// Value is in xlog blocks
        const UNIT_XBLOCKS = pg_sys::GUC_UNIT_XBLOCKS as i32;
        /// Value is in megabytes
        const UNIT_MB = pg_sys::GUC_UNIT_MB as i32;
        /// Value is in bytes
        const UNIT_BYTE = pg_sys::GUC_UNIT_BYTE as i32;
        /// Value is in milliseconds
        const UNIT_MS = pg_sys::GUC_UNIT_MS as i32;
        /// Value is in seconds
        const UNIT_S = pg_sys::GUC_UNIT_S as i32;
        /// Value is in minutes
        const UNIT_MIN = pg_sys::GUC_UNIT_MIN as i32;
        /// Include in `EXPLAIN` output
        const EXPLAIN = pg_sys::GUC_EXPLAIN as i32;
        #[cfg(any(feature = "pg15", feature = "pg16", feature = "pg17", feature = "pg18"))]
        /// `RUNTIME_COMPUTED` is intended for runtime-computed GUCs that are only available via
        /// `postgres -C` if the server is not running
        const RUNTIME_COMPUTED = pg_sys::GUC_RUNTIME_COMPUTED as i32;
    }
}

/// A trait that can be derived using [`PostgresGucEnum`] on enums, such that they can be
/// used as a GUC.
///
/// # Safety
///
/// [`GucEnum::CONFIG_ENUM_ENTRY`] must be a valid pointer to the config enum entry.
pub unsafe trait GucEnum: Copy + Send + Sync {
    fn from_ordinal(ordinal: i32) -> Self;
    fn to_ordinal(&self) -> i32;
    const CONFIG_ENUM_ENTRY: *const pg_sys::config_enum_entry;
}

/// A trait that indicates that the type can be used as a GUC value.
///
/// # Safety
///
/// [`GucValue::Raw`] must be `Send` and `Sync`, or it's a pointer type.
pub unsafe trait GucValue {
    type Raw: Copy;
    unsafe fn from_raw(raw: Self::Raw) -> Self;
    type BootVal: Copy + Send + Sync;
}

/// A safe wrapper around a global variable that can be edited through a GUC
pub struct GucSetting<T: GucValue> {
    value: Cell<T::Raw>,
    boot_val: T::BootVal,
}

unsafe impl<T: GucValue> Sync for GucSetting<T> {}

impl<T: GucValue> GucSetting<T> {
    pub fn get(&self) -> T {
        pg_sys::submodules::thread_check::check_active_thread();
        unsafe { GucValue::from_raw(self.value.get()) }
    }

    pub const fn as_ptr(&self) -> *mut T::Raw {
        self.value.as_ptr()
    }
}

unsafe impl GucValue for bool {
    type Raw = bool;
    unsafe fn from_raw(raw: Self::Raw) -> Self {
        raw
    }
    type BootVal = ();
}
impl GucSetting<bool> {
    pub const fn new(value: bool) -> Self {
        GucSetting { value: Cell::new(value), boot_val: () }
    }
}

unsafe impl GucValue for i32 {
    type Raw = i32;
    unsafe fn from_raw(raw: Self::Raw) -> Self {
        raw
    }
    type BootVal = ();
}
impl GucSetting<i32> {
    pub const fn new(value: i32) -> Self {
        GucSetting { value: Cell::new(value), boot_val: () }
    }
}

unsafe impl GucValue for f64 {
    type Raw = f64;
    unsafe fn from_raw(raw: Self::Raw) -> Self {
        raw
    }
    type BootVal = ();
}
impl GucSetting<f64> {
    pub const fn new(value: f64) -> Self {
        GucSetting { value: Cell::new(value), boot_val: () }
    }
}

unsafe impl GucValue for Option<CString> {
    type Raw = *mut std::ffi::c_char;
    unsafe fn from_raw(raw: Self::Raw) -> Self {
        if raw.is_null() { None } else { Some(CStr::from_ptr(raw).to_owned()) }
    }
    type BootVal = ();
}
impl GucSetting<Option<CString>> {
    pub const fn new(value: Option<&'static CStr>) -> Self {
        GucSetting {
            value: Cell::new(if let Some(value) = value {
                value.as_ptr().cast_mut()
            } else {
                std::ptr::null_mut()
            }),
            boot_val: (),
        }
    }
}

unsafe impl<T: GucEnum> GucValue for T {
    type Raw = i32;
    unsafe fn from_raw(raw: Self::Raw) -> Self {
        T::from_ordinal(raw)
    }
    type BootVal = T;
}
impl<T: GucEnum> GucSetting<T> {
    pub const fn new(value: T) -> Self {
        GucSetting { value: Cell::new(0), boot_val: value }
    }
}

/// A struct that has associated functions to register new GUCs
pub struct GucRegistry {}

impl GucRegistry {
    // GUC Registration functions that do not expose hooks
    pub fn define_bool_guc(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<bool>,
        context: GucContext,
        flags: GucFlags,
    ) {
        unsafe {
            pg_sys::DefineCustomBoolVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                context as isize as _,
                flags.bits(),
                None,
                None,
                None,
            );
        }
    }

    pub fn define_int_guc(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<i32>,
        min_value: i32,
        max_value: i32,
        context: GucContext,
        flags: GucFlags,
    ) {
        unsafe {
            pg_sys::DefineCustomIntVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                min_value,
                max_value,
                context as isize as _,
                flags.bits(),
                None,
                None,
                None,
            )
        }
    }

    pub fn define_string_guc(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<Option<CString>>,
        context: GucContext,
        flags: GucFlags,
    ) {
        unsafe {
            pg_sys::DefineCustomStringVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                context as isize as _,
                flags.bits(),
                None,
                None,
                None,
            );
        }
    }

    pub fn define_float_guc(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<f64>,
        min_value: f64,
        max_value: f64,
        context: GucContext,
        flags: GucFlags,
    ) {
        unsafe {
            pg_sys::DefineCustomRealVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                min_value,
                max_value,
                context as isize as _,
                flags.bits(),
                None,
                None,
                None,
            );
        }
    }

    pub fn define_enum_guc<T: GucEnum>(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<T>,
        context: GucContext,
        flags: GucFlags,
    ) {
        setting.value.set(setting.boot_val.to_ordinal());
        unsafe {
            pg_sys::DefineCustomEnumVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                T::CONFIG_ENUM_ENTRY,
                context as isize as _,
                flags.bits(),
                None,
                None,
                None,
            );
        }
    }

    /// Define a boolean GUC with custom hooks.
    ///
    /// # Hooks
    ///
    /// * `check_hook` - Validates new values. Return false to reject.
    /// * `assign_hook` - Called after value is set. Use for side effects.
    /// * `show_hook` - Returns custom display string for SHOW commands.
    ///
    /// # Safety
    ///
    /// This function is unsafe because hook functions must be properly guarded against Rust panics.
    /// Any hook function that might panic must be marked with `#[pg_guard]` to ensure proper
    /// conversion of Rust panics into PostgreSQL errors.
    ///
    pub unsafe fn define_bool_guc_with_hooks(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<bool>,
        context: GucContext,
        flags: GucFlags,
        check_hook: pg_sys::GucBoolCheckHook,
        assign_hook: pg_sys::GucBoolAssignHook,
        show_hook: pg_sys::GucShowHook,
    ) {
        unsafe {
            pg_sys::DefineCustomBoolVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                context as isize as _,
                flags.bits(),
                check_hook,
                assign_hook,
                show_hook,
            );
        }
    }

    /// Define an integer GUC with custom hooks.
    ///
    /// # Safety
    ///
    /// This function is unsafe because hook functions must be properly guarded against Rust panics.
    /// Any hook function that might panic must be marked with `#[pg_guard]` to ensure proper
    /// conversion of Rust panics into PostgreSQL errors.
    pub unsafe fn define_int_guc_with_hooks(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<i32>,
        min_value: i32,
        max_value: i32,
        context: GucContext,
        flags: GucFlags,
        check_hook: pg_sys::GucIntCheckHook,
        assign_hook: pg_sys::GucIntAssignHook,
        show_hook: pg_sys::GucShowHook,
    ) {
        unsafe {
            pg_sys::DefineCustomIntVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                min_value,
                max_value,
                context as isize as _,
                flags.bits(),
                check_hook,
                assign_hook,
                show_hook,
            )
        }
    }

    /// Define a string GUC with custom hooks.
    ///
    /// # Safety
    ///
    /// This function is unsafe because hook functions must be properly guarded against Rust panics.
    /// Any hook function that might panic must be marked with `#[pg_guard]` to ensure proper
    /// conversion of Rust panics into PostgreSQL errors.
    pub unsafe fn define_string_guc_with_hooks(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<Option<CString>>,
        context: GucContext,
        flags: GucFlags,
        check_hook: pg_sys::GucStringCheckHook,
        assign_hook: pg_sys::GucStringAssignHook,
        show_hook: pg_sys::GucShowHook,
    ) {
        unsafe {
            pg_sys::DefineCustomStringVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                context as isize as _,
                flags.bits(),
                check_hook,
                assign_hook,
                show_hook,
            );
        }
    }

    /// Define a float GUC with custom hooks.
    ///
    /// # Safety
    ///
    /// This function is unsafe because hook functions must be properly guarded against Rust panics.
    /// Any hook function that might panic must be marked with `#[pg_guard]` to ensure proper
    /// conversion of Rust panics into PostgreSQL errors.
    ///
    pub fn define_float_guc_with_hooks(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<f64>,
        min_value: f64,
        max_value: f64,
        context: GucContext,
        flags: GucFlags,
        check_hook: pg_sys::GucRealCheckHook,
        assign_hook: pg_sys::GucRealAssignHook,
        show_hook: pg_sys::GucShowHook,
    ) {
        unsafe {
            pg_sys::DefineCustomRealVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                min_value,
                max_value,
                context as isize as _,
                flags.bits(),
                check_hook,
                assign_hook,
                show_hook,
            );
        }
    }

    /// Define an enum GUC with custom hooks.
    ///
    /// # Safety
    ///
    /// This function is unsafe because hook functions must be properly guarded against Rust panics.
    /// Any hook function that might panic must be marked with `#[pg_guard]` to ensure proper
    /// conversion of Rust panics into PostgreSQL errors.
    pub unsafe fn define_enum_guc_with_hooks<T: GucEnum>(
        name: &'static CStr,
        short_description: &'static CStr,
        long_description: &'static CStr,
        setting: &'static GucSetting<T>,
        context: GucContext,
        flags: GucFlags,
        check_hook: pg_sys::GucEnumCheckHook,
        assign_hook: pg_sys::GucEnumAssignHook,
        show_hook: pg_sys::GucShowHook,
    ) {
        setting.value.set(setting.boot_val.to_ordinal());
        unsafe {
            pg_sys::DefineCustomEnumVariable(
                name.as_ptr(),
                short_description.as_ptr(),
                long_description.as_ptr(),
                setting.value.as_ptr(),
                setting.value.get(),
                T::CONFIG_ENUM_ENTRY,
                context as isize as _,
                flags.bits(),
                check_hook,
                assign_hook,
                show_hook,
            );
        }
    }
}

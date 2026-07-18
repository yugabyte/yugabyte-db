//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! A trait and registration system for hooking Postgres internal operations such as its planner and executor
#![allow(clippy::unit_arg)]
#![allow(static_mut_refs)]
#![deprecated(
    since = "0.12.1",
    note = "currently always UB, use FFI + pointers to `static UnsafeCell`"
)]
use crate as pgrx; // for #[pg_guard] support from within ourself
use crate::prelude::*;
use crate::{void_mut_ptr, PgList};
use std::ops::Deref;

#[cfg(any(feature = "pg13"))]
// JumbleState is not defined prior to postgres v14.
// This zero-sized type is here to provide an inner type for
// the option in post_parse_analyze_hook, but prior to v14
// that option will always be set to None at runtime.
pub struct JumbleState {}

#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
pub use pg_sys::JumbleState;

pub struct HookResult<T> {
    pub inner: T,
}

impl<T> HookResult<T> {
    pub fn new(value: T) -> Self {
        HookResult { inner: value }
    }
}

impl<T> Deref for HookResult<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

pub trait PgHooks {
    /// Hook before the logs are being processed by PostgreSQL itself
    fn emit_log(
        &mut self,
        error_data: PgBox<pg_sys::ErrorData>,
        prev_hook: fn(error_data: PgBox<pg_sys::ErrorData>) -> HookResult<()>,
    ) -> HookResult<()> {
        prev_hook(error_data)
    }

    /// Hook for plugins to get control in ExecutorStart()
    fn executor_start(
        &mut self,
        query_desc: PgBox<pg_sys::QueryDesc>,
        eflags: i32,
        prev_hook: fn(query_desc: PgBox<pg_sys::QueryDesc>, eflags: i32) -> HookResult<()>,
    ) -> HookResult<()> {
        prev_hook(query_desc, eflags)
    }

    /// Hook for plugins to get control in ExecutorRun()
    fn executor_run(
        &mut self,
        query_desc: PgBox<pg_sys::QueryDesc>,
        direction: pg_sys::ScanDirection::Type,
        count: u64,
        execute_once: bool,
        prev_hook: fn(
            query_desc: PgBox<pg_sys::QueryDesc>,
            direction: pg_sys::ScanDirection::Type,
            count: u64,
            execute_once: bool,
        ) -> HookResult<()>,
    ) -> HookResult<()> {
        prev_hook(query_desc, direction, count, execute_once)
    }

    /// Hook for plugins to get control in ExecutorFinish()
    fn executor_finish(
        &mut self,
        query_desc: PgBox<pg_sys::QueryDesc>,
        prev_hook: fn(query_desc: PgBox<pg_sys::QueryDesc>) -> HookResult<()>,
    ) -> HookResult<()> {
        prev_hook(query_desc)
    }

    /// Hook for plugins to get control in ExecutorEnd()
    fn executor_end(
        &mut self,
        query_desc: PgBox<pg_sys::QueryDesc>,
        prev_hook: fn(query_desc: PgBox<pg_sys::QueryDesc>) -> HookResult<()>,
    ) -> HookResult<()> {
        prev_hook(query_desc)
    }

    /// Hook for plugins to get control in ExecCheckRTPerms()
    fn executor_check_perms(
        &mut self,
        range_table: PgList<*mut pg_sys::RangeTblEntry>,
        rte_perm_infos: Option<*mut pg_sys::List>,
        ereport_on_violation: bool,
        prev_hook: fn(
            range_table: PgList<*mut pg_sys::RangeTblEntry>,
            rte_perm_infos: Option<*mut pg_sys::List>,
            ereport_on_violation: bool,
        ) -> HookResult<bool>,
    ) -> HookResult<bool> {
        prev_hook(range_table, rte_perm_infos, ereport_on_violation)
    }

    /// Hook for plugins to get control in `ProcessUtility()`
    fn process_utility_hook(
        &mut self,
        pstmt: PgBox<pg_sys::PlannedStmt>,
        query_string: &core::ffi::CStr,
        read_only_tree: Option<bool>,
        context: pg_sys::ProcessUtilityContext::Type,
        params: PgBox<pg_sys::ParamListInfoData>,
        query_env: PgBox<pg_sys::QueryEnvironment>,
        dest: PgBox<pg_sys::DestReceiver>,
        completion_tag: *mut pg_sys::QueryCompletion,
        prev_hook: fn(
            pstmt: PgBox<pg_sys::PlannedStmt>,
            query_string: &core::ffi::CStr,
            read_only_tree: Option<bool>,
            context: pg_sys::ProcessUtilityContext::Type,
            params: PgBox<pg_sys::ParamListInfoData>,
            query_env: PgBox<pg_sys::QueryEnvironment>,
            dest: PgBox<pg_sys::DestReceiver>,
            completion_tag: *mut pg_sys::QueryCompletion,
        ) -> HookResult<()>,
    ) -> HookResult<()> {
        prev_hook(
            pstmt,
            query_string,
            read_only_tree,
            context,
            params,
            query_env,
            dest,
            completion_tag,
        )
    }

    /// Hook for plugins to get control of the planner
    fn planner(
        &mut self,
        parse: PgBox<pg_sys::Query>,
        query_string: *const std::os::raw::c_char,
        cursor_options: i32,
        bound_params: PgBox<pg_sys::ParamListInfoData>,
        prev_hook: fn(
            parse: PgBox<pg_sys::Query>,
            query_string: *const std::os::raw::c_char,
            cursor_options: i32,
            bound_params: PgBox<pg_sys::ParamListInfoData>,
        ) -> HookResult<*mut pg_sys::PlannedStmt>,
    ) -> HookResult<*mut pg_sys::PlannedStmt> {
        prev_hook(parse, query_string, cursor_options, bound_params)
    }

    fn post_parse_analyze(
        &mut self,
        pstate: PgBox<pg_sys::ParseState>,
        query: PgBox<pg_sys::Query>,
        jumble_state: Option<PgBox<JumbleState>>,
        prev_hook: fn(
            pstate: PgBox<pg_sys::ParseState>,
            query: PgBox<pg_sys::Query>,
            jumble_state: Option<PgBox<JumbleState>>,
        ) -> HookResult<()>,
    ) -> HookResult<()> {
        prev_hook(pstate, query, jumble_state)
    }

    /// Called when the transaction aborts
    fn abort(&mut self) {}

    /// Called when the transaction commits
    fn commit(&mut self) {}
}

struct Hooks {
    current_hook: Box<&'static mut (dyn PgHooks)>,
    prev_emit_log_hook: pg_sys::emit_log_hook_type,
    prev_executor_start_hook: pg_sys::ExecutorStart_hook_type,
    prev_executor_run_hook: pg_sys::ExecutorRun_hook_type,
    prev_executor_finish_hook: pg_sys::ExecutorFinish_hook_type,
    prev_executor_end_hook: pg_sys::ExecutorEnd_hook_type,
    prev_executor_check_perms_hook: pg_sys::ExecutorCheckPerms_hook_type,
    prev_process_utility_hook: pg_sys::ProcessUtility_hook_type,
    prev_planner_hook: pg_sys::planner_hook_type,
    prev_post_parse_analyze_hook: pg_sys::post_parse_analyze_hook_type,
}

static mut HOOKS: Option<Hooks> = None;

/// Register a `PgHook` instance to respond to the various hook points
pub unsafe fn register_hook(hook: &'static mut (dyn PgHooks)) {
    if HOOKS.is_some() {
        panic!("PgHook instance already registered");
    }
    #[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
    let prev_executor_check_perms_hook = pg_sys::ExecutorCheckPerms_hook
        .replace(pgrx_executor_check_perms)
        .or(Some(pgrx_standard_executor_check_perms_wrapper));

    #[cfg(any(feature = "pg16", feature = "pg17"))]
    let prev_executor_check_perms_hook = pg_sys::ExecutorCheckPerms_hook
        .replace(pgrx_executor_check_perms)
        .or(Some(pgrx_standard_executor_check_perms_wrapper));

    HOOKS = Some(Hooks {
        current_hook: Box::new(hook),
        prev_executor_start_hook: pg_sys::ExecutorStart_hook
            .replace(pgrx_executor_start)
            .or(Some(pgrx_standard_executor_start_wrapper)),
        prev_executor_run_hook: pg_sys::ExecutorRun_hook
            .replace(pgrx_executor_run)
            .or(Some(pgrx_standard_executor_run_wrapper)),
        prev_executor_finish_hook: pg_sys::ExecutorFinish_hook
            .replace(pgrx_executor_finish)
            .or(Some(pgrx_standard_executor_finish_wrapper)),
        prev_executor_end_hook: pg_sys::ExecutorEnd_hook
            .replace(pgrx_executor_end)
            .or(Some(pgrx_standard_executor_end_wrapper)),
        prev_executor_check_perms_hook,
        prev_process_utility_hook: pg_sys::ProcessUtility_hook
            .replace(pgrx_process_utility)
            .or(Some(pgrx_standard_process_utility_wrapper)),
        prev_planner_hook: pg_sys::planner_hook
            .replace(pgrx_planner)
            .or(Some(pgrx_standard_planner_wrapper)),
        prev_post_parse_analyze_hook: pg_sys::post_parse_analyze_hook
            .replace(pgrx_post_parse_analyze),
        prev_emit_log_hook: pg_sys::emit_log_hook.replace(pgrx_emit_log),
    });

    #[pg_guard]
    unsafe extern "C-unwind" fn xact_callback(event: pg_sys::XactEvent::Type, _data: void_mut_ptr) {
        match event {
            pg_sys::XactEvent::XACT_EVENT_ABORT => HOOKS.as_mut().unwrap().current_hook.abort(),
            pg_sys::XactEvent::XACT_EVENT_PRE_COMMIT => {
                HOOKS.as_mut().unwrap().current_hook.commit()
            }
            _ => { /* noop */ }
        }
    }

    pg_sys::RegisterXactCallback(Some(xact_callback), std::ptr::null_mut());
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_executor_start(query_desc: *mut pg_sys::QueryDesc, eflags: i32) {
    fn prev(query_desc: PgBox<pg_sys::QueryDesc>, eflags: i32) -> HookResult<()> {
        unsafe {
            (HOOKS.as_mut().unwrap().prev_executor_start_hook.as_ref().unwrap())(
                query_desc.into_pg(),
                eflags,
            )
        }
        HookResult::new(())
    }
    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.executor_start(PgBox::from_pg(query_desc), eflags, prev);
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_executor_run(
    query_desc: *mut pg_sys::QueryDesc,
    direction: pg_sys::ScanDirection::Type,
    count: u64,
    execute_once: bool,
) {
    fn prev(
        query_desc: PgBox<pg_sys::QueryDesc>,
        direction: pg_sys::ScanDirection::Type,
        count: u64,
        execute_once: bool,
    ) -> HookResult<()> {
        unsafe {
            (HOOKS.as_mut().unwrap().prev_executor_run_hook.as_ref().unwrap())(
                query_desc.into_pg(),
                direction,
                count,
                execute_once,
            )
        }
        HookResult::new(())
    }
    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.executor_run(PgBox::from_pg(query_desc), direction, count, execute_once, prev);
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_executor_finish(query_desc: *mut pg_sys::QueryDesc) {
    fn prev(query_desc: PgBox<pg_sys::QueryDesc>) -> HookResult<()> {
        unsafe {
            (HOOKS.as_mut().unwrap().prev_executor_finish_hook.as_ref().unwrap())(
                query_desc.into_pg(),
            )
        }
        HookResult::new(())
    }
    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.executor_finish(PgBox::from_pg(query_desc), prev);
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_executor_end(query_desc: *mut pg_sys::QueryDesc) {
    fn prev(query_desc: PgBox<pg_sys::QueryDesc>) -> HookResult<()> {
        unsafe {
            (HOOKS.as_mut().unwrap().prev_executor_end_hook.as_ref().unwrap())(query_desc.into_pg())
        }
        HookResult::new(())
    }
    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.executor_end(PgBox::from_pg(query_desc), prev);
}

#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_executor_check_perms(
    range_table: *mut pg_sys::List,
    ereport_on_violation: bool,
) -> bool {
    fn prev(
        range_table: PgList<*mut pg_sys::RangeTblEntry>,
        _rte_perm_infos: Option<*mut pg_sys::List>,
        ereport_on_violation: bool,
    ) -> HookResult<bool> {
        HookResult::new(unsafe {
            (HOOKS.as_mut().unwrap().prev_executor_check_perms_hook.as_ref().unwrap())(
                range_table.into_pg(),
                ereport_on_violation,
            )
        })
    }
    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.executor_check_perms(PgList::from_pg(range_table), None, ereport_on_violation, prev).inner
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_executor_check_perms(
    range_table: *mut pg_sys::List,
    rte_perm_infos: *mut pg_sys::List,
    ereport_on_violation: bool,
) -> bool {
    fn prev(
        range_table: PgList<*mut pg_sys::RangeTblEntry>,
        rte_perm_infos: Option<*mut pg_sys::List>,
        ereport_on_violation: bool,
    ) -> HookResult<bool> {
        HookResult::new(unsafe {
            (HOOKS.as_mut().unwrap().prev_executor_check_perms_hook.as_ref().unwrap())(
                range_table.into_pg(),
                rte_perm_infos.unwrap_or(std::ptr::null_mut()),
                ereport_on_violation,
            )
        })
    }
    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.executor_check_perms(
        PgList::from_pg(range_table),
        Some(rte_perm_infos),
        ereport_on_violation,
        prev,
    )
    .inner
}

#[cfg(feature = "pg13")]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_process_utility(
    pstmt: *mut pg_sys::PlannedStmt,
    query_string: *const ::std::os::raw::c_char,
    context: pg_sys::ProcessUtilityContext::Type,
    params: pg_sys::ParamListInfo,
    query_env: *mut pg_sys::QueryEnvironment,
    dest: *mut pg_sys::DestReceiver,
    completion_tag: *mut pg_sys::QueryCompletion,
) {
    fn prev(
        pstmt: PgBox<pg_sys::PlannedStmt>,
        query_string: &core::ffi::CStr,
        _read_only_tree: Option<bool>,
        context: pg_sys::ProcessUtilityContext::Type,
        params: PgBox<pg_sys::ParamListInfoData>,
        query_env: PgBox<pg_sys::QueryEnvironment>,
        dest: PgBox<pg_sys::DestReceiver>,
        completion_tag: *mut pg_sys::QueryCompletion,
    ) -> HookResult<()> {
        HookResult::new(unsafe {
            (HOOKS.as_mut().unwrap().prev_process_utility_hook.as_ref().unwrap())(
                pstmt.into_pg(),
                query_string.as_ptr(),
                context,
                params.into_pg(),
                query_env.into_pg(),
                dest.into_pg(),
                completion_tag,
            )
        })
    }

    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.process_utility_hook(
        PgBox::from_pg(pstmt),
        core::ffi::CStr::from_ptr(query_string),
        None,
        context,
        PgBox::from_pg(params),
        PgBox::from_pg(query_env),
        PgBox::from_pg(dest),
        completion_tag,
        prev,
    )
    .inner
}
#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_process_utility(
    pstmt: *mut pg_sys::PlannedStmt,
    query_string: *const ::std::os::raw::c_char,
    read_only_tree: bool,
    context: pg_sys::ProcessUtilityContext::Type,
    params: pg_sys::ParamListInfo,
    query_env: *mut pg_sys::QueryEnvironment,
    dest: *mut pg_sys::DestReceiver,
    completion_tag: *mut pg_sys::QueryCompletion,
) {
    fn prev(
        pstmt: PgBox<pg_sys::PlannedStmt>,
        query_string: &core::ffi::CStr,
        read_only_tree: Option<bool>,
        context: pg_sys::ProcessUtilityContext::Type,
        params: PgBox<pg_sys::ParamListInfoData>,
        query_env: PgBox<pg_sys::QueryEnvironment>,
        dest: PgBox<pg_sys::DestReceiver>,
        completion_tag: *mut pg_sys::QueryCompletion,
    ) -> HookResult<()> {
        HookResult::new(unsafe {
            (HOOKS.as_mut().unwrap().prev_process_utility_hook.as_ref().unwrap())(
                pstmt.into_pg(),
                query_string.as_ptr(),
                read_only_tree.unwrap(),
                context,
                params.into_pg(),
                query_env.into_pg(),
                dest.into_pg(),
                completion_tag,
            )
        })
    }

    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.process_utility_hook(
        PgBox::from_pg(pstmt),
        core::ffi::CStr::from_ptr(query_string),
        Some(read_only_tree),
        context,
        PgBox::from_pg(params),
        PgBox::from_pg(query_env),
        PgBox::from_pg(dest),
        completion_tag,
        prev,
    )
    .inner
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_planner(
    parse: *mut pg_sys::Query,
    query_string: *const ::std::os::raw::c_char,
    cursor_options: i32,
    bound_params: pg_sys::ParamListInfo,
) -> *mut pg_sys::PlannedStmt {
    pgrx_planner_impl(parse, query_string, cursor_options, bound_params)
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_planner_impl(
    parse: *mut pg_sys::Query,
    query_string: *const ::std::os::raw::c_char,
    cursor_options: i32,
    bound_params: pg_sys::ParamListInfo,
) -> *mut pg_sys::PlannedStmt {
    fn prev(
        parse: PgBox<pg_sys::Query>,
        #[allow(unused_variables)] query_string: *const ::std::os::raw::c_char,
        cursor_options: i32,
        bound_params: PgBox<pg_sys::ParamListInfoData>,
    ) -> HookResult<*mut pg_sys::PlannedStmt> {
        HookResult::new(unsafe {
            (HOOKS.as_mut().unwrap().prev_planner_hook.as_ref().unwrap())(
                parse.into_pg(),
                query_string,
                cursor_options,
                bound_params.into_pg(),
            )
        })
    }
    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.planner(
        PgBox::from_pg(parse),
        query_string,
        cursor_options,
        PgBox::from_pg(bound_params),
        prev,
    )
    .inner
}

#[cfg(feature = "pg13")]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_post_parse_analyze(
    parse_state: *mut pg_sys::ParseState,
    query: *mut pg_sys::Query,
) {
    fn prev(
        parse_state: PgBox<pg_sys::ParseState>,
        query: PgBox<pg_sys::Query>,
        _jumble_state: Option<PgBox<JumbleState>>,
    ) -> HookResult<()> {
        HookResult::new(unsafe {
            match HOOKS.as_mut().unwrap().prev_post_parse_analyze_hook.as_ref() {
                None => (),
                Some(f) => (f)(parse_state.as_ptr(), query.as_ptr()),
            }
        })
    }

    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.post_parse_analyze(PgBox::from_pg(parse_state), PgBox::from_pg(query), None, prev).inner
}

#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_post_parse_analyze(
    parse_state: *mut pg_sys::ParseState,
    query: *mut pg_sys::Query,
    jumble_state: *mut JumbleState,
) {
    fn prev(
        parse_state: PgBox<pg_sys::ParseState>,
        query: PgBox<pg_sys::Query>,
        jumble_state: Option<PgBox<JumbleState>>,
    ) -> HookResult<()> {
        HookResult::new(unsafe {
            match HOOKS.as_mut().unwrap().prev_post_parse_analyze_hook.as_ref() {
                None => (),
                Some(f) => {
                    (f)(parse_state.as_ptr(), query.as_ptr(), jumble_state.unwrap().as_ptr())
                }
            }
        })
    }

    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.post_parse_analyze(
        PgBox::from_pg(parse_state),
        PgBox::from_pg(query),
        Some(PgBox::from_pg(jumble_state)),
        prev,
    )
    .inner
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_emit_log(error_data: *mut pg_sys::ErrorData) {
    fn prev(error_data: PgBox<pg_sys::ErrorData>) -> HookResult<()> {
        HookResult::new(unsafe {
            match HOOKS.as_mut().unwrap().prev_emit_log_hook.as_ref() {
                None => (),
                Some(f) => (f)(error_data.as_ptr()),
            }
        })
    }

    let hook = &mut HOOKS.as_mut().unwrap().current_hook;
    hook.emit_log(PgBox::from_pg(error_data), prev).inner
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_standard_executor_start_wrapper(
    query_desc: *mut pg_sys::QueryDesc,
    eflags: i32,
) {
    pg_sys::standard_ExecutorStart(query_desc, eflags)
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_standard_executor_run_wrapper(
    query_desc: *mut pg_sys::QueryDesc,
    direction: pg_sys::ScanDirection::Type,
    count: u64,
    execute_once: bool,
) {
    pg_sys::standard_ExecutorRun(query_desc, direction, count, execute_once)
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_standard_executor_finish_wrapper(
    query_desc: *mut pg_sys::QueryDesc,
) {
    pg_sys::standard_ExecutorFinish(query_desc)
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_standard_executor_end_wrapper(query_desc: *mut pg_sys::QueryDesc) {
    pg_sys::standard_ExecutorEnd(query_desc)
}

#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_standard_executor_check_perms_wrapper(
    _range_table: *mut pg_sys::List,
    _ereport_on_violation: bool,
) -> bool {
    true
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_standard_executor_check_perms_wrapper(
    _range_table: *mut pg_sys::List,
    _rte_perm_infos: *mut pg_sys::List,
    _ereport_on_violation: bool,
) -> bool {
    true
}

#[cfg(feature = "pg13")]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_standard_process_utility_wrapper(
    pstmt: *mut pg_sys::PlannedStmt,
    query_string: *const ::std::os::raw::c_char,
    context: pg_sys::ProcessUtilityContext::Type,
    params: pg_sys::ParamListInfo,
    query_env: *mut pg_sys::QueryEnvironment,
    dest: *mut pg_sys::DestReceiver,
    completion_tag: *mut pg_sys::QueryCompletion,
) {
    pg_sys::standard_ProcessUtility(
        pstmt,
        query_string,
        context,
        params,
        query_env,
        dest,
        completion_tag,
    )
}

#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
#[pg_guard]
unsafe extern "C-unwind" fn pgrx_standard_process_utility_wrapper(
    pstmt: *mut pg_sys::PlannedStmt,
    query_string: *const ::std::os::raw::c_char,
    read_only_tree: bool,
    context: pg_sys::ProcessUtilityContext::Type,
    params: pg_sys::ParamListInfo,
    query_env: *mut pg_sys::QueryEnvironment,
    dest: *mut pg_sys::DestReceiver,
    completion_tag: *mut pg_sys::QueryCompletion,
) {
    pg_sys::standard_ProcessUtility(
        pstmt,
        query_string,
        read_only_tree,
        context,
        params,
        query_env,
        dest,
        completion_tag,
    )
}

#[pg_guard]
unsafe extern "C-unwind" fn pgrx_standard_planner_wrapper(
    parse: *mut pg_sys::Query,
    query_string: *const ::std::os::raw::c_char,
    cursor_options: i32,
    bound_params: pg_sys::ParamListInfo,
) -> *mut pg_sys::PlannedStmt {
    pg_sys::standard_planner(parse, query_string, cursor_options, bound_params)
}

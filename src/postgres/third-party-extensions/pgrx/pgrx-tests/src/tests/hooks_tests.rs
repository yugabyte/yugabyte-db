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

    use pgrx::hooks::*;
    use pgrx::prelude::*;
    use pgrx::PgList;

    #[pg_test]
    unsafe fn test_callbacks() {
        use pgrx::pg_sys::*;

        struct TestHook {
            events: u32,
        }
        impl PgHooks for TestHook {
            /// Hook before the logs are being processed by PostgreSQL itself
            fn emit_log(
                &mut self,
                error_data: PgBox<pg_sys::ErrorData>,
                prev_hook: fn(error_data: PgBox<pg_sys::ErrorData>) -> HookResult<()>,
            ) -> HookResult<()> {
                self.events += 1;
                prev_hook(error_data)
            }

            fn executor_start(
                &mut self,
                query_desc: PgBox<QueryDesc>,
                eflags: i32,
                prev_hook: fn(PgBox<QueryDesc>, i32) -> HookResult<()>,
            ) -> HookResult<()> {
                self.events += 1;
                prev_hook(query_desc, eflags)
            }

            fn executor_run(
                &mut self,
                query_desc: PgBox<QueryDesc>,
                direction: i32,
                count: u64,
                execute_once: bool,
                prev_hook: fn(PgBox<QueryDesc>, i32, u64, bool) -> HookResult<()>,
            ) -> HookResult<()> {
                self.events += 1;
                prev_hook(query_desc, direction, count, execute_once)
            }

            fn executor_finish(
                &mut self,
                query_desc: PgBox<QueryDesc>,
                prev_hook: fn(PgBox<QueryDesc>) -> HookResult<()>,
            ) -> HookResult<()> {
                self.events += 1;
                prev_hook(query_desc)
            }

            fn executor_end(
                &mut self,
                query_desc: PgBox<QueryDesc>,
                prev_hook: fn(PgBox<QueryDesc>) -> HookResult<()>,
            ) -> HookResult<()> {
                self.events += 1;
                prev_hook(query_desc)
            }

            fn executor_check_perms(
                &mut self,
                range_table: PgList<*mut RangeTblEntry>,
                rte_perm_infos: Option<*mut pg_sys::List>,
                ereport_on_violation: bool,
                prev_hook: fn(
                    PgList<*mut RangeTblEntry>,
                    Option<*mut pg_sys::List>,
                    bool,
                ) -> HookResult<bool>,
            ) -> HookResult<bool> {
                self.events += 1;
                prev_hook(range_table, rte_perm_infos, ereport_on_violation)
            }

            fn planner(
                &mut self,
                parse: PgBox<Query>,
                query_string: *const std::os::raw::c_char,
                cursor_options: i32,
                bound_params: PgBox<ParamListInfoData>,
                prev_hook: fn(
                    PgBox<Query>,
                    query_string: *const std::os::raw::c_char,
                    i32,
                    PgBox<ParamListInfoData>,
                ) -> HookResult<*mut PlannedStmt>,
            ) -> HookResult<*mut PlannedStmt> {
                self.events += 1;
                prev_hook(parse, query_string, cursor_options, bound_params)
            }

            fn post_parse_analyze(
                &mut self,
                parse_state: PgBox<pg_sys::ParseState>,
                query: PgBox<pg_sys::Query>,
                jumble_state: Option<PgBox<JumbleState>>,
                prev_hook: fn(
                    parse_state: PgBox<pg_sys::ParseState>,
                    query: PgBox<pg_sys::Query>,
                    jumble_state: Option<PgBox<JumbleState>>,
                ) -> HookResult<()>,
            ) -> HookResult<()> {
                self.events += 1;
                prev_hook(parse_state, query, jumble_state)
            }
        }

        static mut HOOK: TestHook = TestHook { events: 0 };
        pgrx::hooks::register_hook(&mut HOOK);
        // To trigger the emit_log hook, we need something to log.
        // We therefore ensure the select statement will be logged.
        Spi::run("SET local log_statement to 'all'; SELECT 1").expect("SPI failed");
        assert_eq!(8, HOOK.events);

        // TODO:  it'd be nice to also test that .commit() and .abort() also get called
        //    but I don't see how to do that since we're running *inside* a transaction here
    }
}

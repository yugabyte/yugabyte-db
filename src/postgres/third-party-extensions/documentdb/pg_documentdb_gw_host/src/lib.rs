use pgrx::prelude::*;

mod bgworker;
mod gucs;

::pgrx::pg_module_magic!();

#[pg_guard]
pub extern "C-unwind" fn _PG_init() {
    crate::gucs::init();
    crate::bgworker::init();
}

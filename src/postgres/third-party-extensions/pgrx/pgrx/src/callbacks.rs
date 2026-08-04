//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Provides safe wrappers around Postgres' "Transaction" and "Sub Transaction" hook system
#![allow(static_mut_refs)]

use crate as pgrx; // for #[pg_guard] support from within ourself
use crate::pg_sys;
use crate::prelude::*;
use enum_map::{Enum, EnumMap};
use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

/// Postgres Transaction (Xact) Callback Events
#[derive(Hash, Eq, PartialEq, Clone, Copy, Debug, Enum)]
pub enum PgXactCallbackEvent {
    /// Fired when a transaction is aborted.  It is mutually exclusive with `PgrxactCallbackEvent::Commit`
    ///
    /// ## Safety
    ///
    /// Any kind of Rust `panic!()` or Postgres `ereport(ERROR)` while this event is firing will
    /// cause the Postgres backend to abort.
    Abort,

    /// Fired when a transaction is committed.  It is mutually exclusive with `PgrxactCallbackEvent::Abort`
    ///
    /// ## Safety
    ///
    /// Any kind of Rust `panic!()` or Postgres `ereport(ERROR)` while this event is firing will
    /// cause the Postgres backend to abort.
    Commit,

    /// Fired immediately before the transaction is committed.  This is your last chance to cleanly
    /// abort the current transaction via a Rust `panic!()` or Postgres `ereport(ERROR)`
    PreCommit,

    /// Same as `::Abort`, but for parallel workers
    ParallelAbort,

    /// Same as `::Commit`, but for parallel workers
    ParallelCommit,

    /// Same as `::PreCommit`, but for parallel workers
    ParallelPreCommit,

    /// Same as `::Commit`, but for committing a prepared transaction
    Prepare,

    /// Same as `::PreCommit`, but for committing a prepared transaction
    PrePrepare,
}

impl PgXactCallbackEvent {
    fn translate_pg_event(pg_event: pg_sys::XactEvent::Type) -> Self {
        use pg_sys::XactEvent::*;
        match pg_event {
            XACT_EVENT_ABORT => PgXactCallbackEvent::Abort,
            XACT_EVENT_COMMIT => PgXactCallbackEvent::Commit,
            XACT_EVENT_PARALLEL_ABORT => PgXactCallbackEvent::ParallelAbort,
            XACT_EVENT_PARALLEL_COMMIT => PgXactCallbackEvent::ParallelCommit,
            XACT_EVENT_PARALLEL_PRE_COMMIT => PgXactCallbackEvent::ParallelPreCommit,
            XACT_EVENT_PREPARE => PgXactCallbackEvent::Prepare,
            XACT_EVENT_PRE_COMMIT => PgXactCallbackEvent::PreCommit,
            XACT_EVENT_PRE_PREPARE => PgXactCallbackEvent::PrePrepare,
            unknown => panic!("Unrecognized XactEvent: {unknown}"),
        }
    }
}

/// Registering a transaction event callback returns a `XactCallbackReceipt` that can be used
/// to unregister the callback if it later (within the confines of the current transaction)
/// becomes unnecessary
pub struct XactCallbackReceipt(Rc<RefCell<Option<XactCallbackWrapper>>>);

impl XactCallbackReceipt {
    /// Consumes this `XactCallbackReceipt` and unregisters the registered callback it represents
    ///
    /// ## Examples
    ///
    /// ```rust,no_run
    /// use pgrx::*;
    ///
    /// let receipt = register_xact_callback(PgXactCallbackEvent::Commit, || info!("called after commit"));
    ///
    /// let no_longer_necessary = true;
    ///
    /// if no_longer_necessary {
    ///     receipt.unregister_callback();
    /// }
    /// ```
    pub fn unregister_callback(self) {
        self.0.replace(None);
    }
}

/// An internal wrapper for a callback closure
struct XactCallbackWrapper(
    Box<dyn FnOnce() + std::panic::UnwindSafe + std::panic::RefUnwindSafe + 'static>,
);

/// Shorthand for the type representing the map of callbacks
type CallbackMap =
    EnumMap<PgXactCallbackEvent, Option<Vec<Rc<RefCell<Option<XactCallbackWrapper>>>>>>;

/// Register a closure to be called during one of the `PgrxactCallbackEvent` events.  Multiple
/// closures can be registered per event (one at a time), and they are called in the order in which
/// they were registered.
///
/// Registered callbacks only remain registered for the life of a single transaction.  Registration
/// of permanent callbacks should be done through the unsafe `pg_sys::RegisterXactCallback()` function.
///
///
/// ## Examples
///
/// Register a number of events for pre-commit and commit:
///
/// ```rust,no_run
/// use pgrx::*;
///
/// register_xact_callback(PgXactCallbackEvent::PreCommit, || info!("pre-commit #1"));
/// register_xact_callback(PgXactCallbackEvent::PreCommit, || info!("pre-commit #2"));
/// register_xact_callback(PgXactCallbackEvent::PreCommit, || info!("pre-commit #3"));
/// register_xact_callback(PgXactCallbackEvent::Commit, || info!("called after commit"));
/// ```
///
/// Register an event, do some work, and then decide the callback isn't actually necessary anymore:
///
/// ```rust,no_run
/// use pgrx::*;
///
/// // ... do some initialization work ...
///
/// let receipt = register_xact_callback(PgXactCallbackEvent::Abort, || { /* do cleanup if xact aborts */});
///
/// // ... do work that might abort the transaction ...
///
/// // if we got here, the transaction did not abort, so we no longer need to care about cleanup
/// receipt.unregister_callback();
/// ```
///
/// ## Safety
///
/// Any kind of Rust `panic!()` or Postgres `ereport(ERROR)` while executing a `PgrxactCallbackEvent::Commit`
/// or `PgrxactCallbackEvent::Abort` event will immediately cause the Postgres backend to abort and
/// the entire cluster to restart.
///
/// As the Postgres internal documentation says:  
///
/// At transaction end, the callback occurs post-commit or post-abort, so the callback
/// functions can only do noncritical cleanup.
pub fn register_xact_callback<F>(which_event: PgXactCallbackEvent, f: F) -> XactCallbackReceipt
where
    F: FnOnce() + std::panic::UnwindSafe + std::panic::RefUnwindSafe + 'static,
{
    // our map of xact callbacks.  It starts as None and gets initialized below in maybe_initialize()
    static mut XACT_HOOKS: Option<CallbackMap> = None;

    // internal function that we register as an XactCallback
    #[pg_guard]
    unsafe extern "C-unwind" fn callback(
        event: pg_sys::XactEvent::Type,
        _arg: *mut ::std::os::raw::c_void,
    ) {
        let which_event = PgXactCallbackEvent::translate_pg_event(event);

        let hooks = match which_event {
            // pgrx's XactCallbacks are per-transaction, so when the transaction is over
            // (that's either Commit or Abort, which are mutually exclusive), we replace our
            // const XACT_HOOKS with a new, empty Map so that subsequent transactions won't accidentally run
            // these hooks again.
            //
            // Note that we still run any hooks that are registered for these events in this xact
            PgXactCallbackEvent::Commit
            | PgXactCallbackEvent::Abort
            | PgXactCallbackEvent::ParallelCommit
            | PgXactCallbackEvent::ParallelAbort => XACT_HOOKS
                .replace(CallbackMap::default())
                .expect("XACT_HOOKS was None during Commit/Abort")[which_event]
                .take(),

            // not in a transaction-end event, so just borrow our map
            _ => XACT_HOOKS.as_mut().expect("XACT_HOOKS was None")[which_event].take(),
        };

        // if we have a vec of hooks for this event they're consumed here and executed
        // in the order they were registered
        if let Some(hooks) = hooks {
            for hook in hooks.into_iter() {
                // TODO:  do we need to catch panics and do something with them?  They'll cause
                //  the Postgres backend to Abort() if we're handling XactEvent::Commit/Abort events

                // effectively 'take' the hook from the internal RefCell
                if let Some(hook) = hook.replace(None) {
                    // and execute it under guard for proper panic/elog(ERROR) handling
                    hook.0();
                }
            }
        }
    }

    // internal function to manage initialization of our transaction callback
    fn maybe_initialize<'a>() -> &'a mut CallbackMap {
        unsafe {
            // if this is our first time here since the Postgres backend started, XACT_HOOKS will be None
            if XACT_HOOKS.is_none() {
                // so lets swap it out with a new HashMap, which will live for the duration of the backend
                XACT_HOOKS.replace(Default::default());

                // and register our single callback function (internally defined above)
                pg_sys::RegisterXactCallback(Some(callback), std::ptr::null_mut());
            }

            XACT_HOOKS.as_mut().expect("XACT_HOOKS was None during maybe_initialize")
            // this should never happen
        }
    }

    // get a mutable reference to XACT_HOOKS
    let hooks = maybe_initialize();

    // wrap the user-provided closure as an optional, reference counted cell
    let wrapped_func = Rc::new(RefCell::new(Some(XactCallbackWrapper(Box::new(f)))));

    // find (or create) the map Entry for the specified event and add our wrapped hook to it
    let entry = hooks[which_event].get_or_insert_with(Default::default);
    entry.push(Rc::clone(&wrapped_func));

    // give the user the ability to unregister
    XactCallbackReceipt(wrapped_func)
}

#[derive(Hash, Eq, PartialEq, Clone, Debug)]
pub enum PgSubXactCallbackEvent {
    /// Fired when a subtransaction is aborted.  While Rust `panic!()`s and Postgres `ereport(ERROR)`s
    /// can occur here, it's not recommended
    AbortSub,

    /// Fired when a subtransaction is committed.  While Rust `panic!()`s and Postgres `ereport(ERROR)`s
    /// can occur here, it's not recommended
    CommitSub,

    /// Fired immediately before a subtransaction is committed.  This is your last chance to instead
    /// abort the subtransaction before it really commits
    PreCommitSub,

    /// Fired when a subtransaction is first created
    StartSub,
}

impl PgSubXactCallbackEvent {
    fn translate_pg_event(event: pg_sys::SubXactEvent::Type) -> Self {
        use pg_sys::SubXactEvent::*;
        match event {
            SUBXACT_EVENT_ABORT_SUB => PgSubXactCallbackEvent::AbortSub,
            SUBXACT_EVENT_COMMIT_SUB => PgSubXactCallbackEvent::CommitSub,
            SUBXACT_EVENT_PRE_COMMIT_SUB => PgSubXactCallbackEvent::PreCommitSub,
            SUBXACT_EVENT_START_SUB => PgSubXactCallbackEvent::StartSub,
            _ => panic!("Unrecognized SubXactEvent: {event}"),
        }
    }
}

/// Registering a sub-transaction event callback returns a `XactCallbackReceipt` that can be used
/// to unregister the callback if it later (within the confines of the current transaction)
/// becomes unnecessary
pub struct SubXactCallbackReceipt(Rc<RefCell<Option<SubXactCallbackWrapper>>>);

impl SubXactCallbackReceipt {
    /// Consumes this `SubXactCallbackReceipt` and unregisters the registered callback it represents
    ///
    /// ## Examples
    ///
    /// ```rust,no_run
    /// use pgrx::*;
    ///
    /// let receipt = register_subxact_callback(PgSubXactCallbackEvent::CommitSub, |my_subid, parent_subid| info!("called after commit-sub: {my_subid} {parent_subid}"));
    ///
    /// let no_longer_necessary = true;
    ///
    /// if no_longer_necessary {
    ///     receipt.unregister_callback();
    /// }
    /// ```
    pub fn unregister_callback(self) {
        self.0.replace(None);
    }
}

struct SubXactCallbackWrapper(
    Box<
        dyn Fn(pg_sys::SubTransactionId, pg_sys::SubTransactionId)
            + std::panic::UnwindSafe
            + std::panic::RefUnwindSafe
            + 'static,
    >,
);

type SubCallbackMap =
    HashMap<PgSubXactCallbackEvent, Vec<Rc<RefCell<Option<SubXactCallbackWrapper>>>>>;

pub fn register_subxact_callback<F>(
    which_event: PgSubXactCallbackEvent,
    f: F,
) -> SubXactCallbackReceipt
where
    F: Fn(pg_sys::SubTransactionId, pg_sys::SubTransactionId)
        + std::panic::UnwindSafe
        + std::panic::RefUnwindSafe
        + 'static,
{
    static mut SUB_HOOKS: Option<SubCallbackMap> = None;

    #[pg_guard]
    unsafe extern "C-unwind" fn callback(
        event: pg_sys::SubXactEvent::Type,
        my_subid: pg_sys::SubTransactionId,
        parent_subid: pg_sys::SubTransactionId,
        _arg: *mut ::std::os::raw::c_void,
    ) {
        let which_event = PgSubXactCallbackEvent::translate_pg_event(event);

        let hooks = SUB_HOOKS.as_mut();

        // if we have a vec of hooks for this event they're consumed here and executed
        // in the order they were registered
        if let Some(hooks) = hooks {
            let hooks = hooks.get(&which_event);
            if let Some(hooks) = hooks {
                for hook in hooks.iter() {
                    let hook = hook.borrow();
                    if let Some(hook) = hook.as_ref() {
                        (hook.0)(my_subid, parent_subid)
                    }
                }
            }
        }
    }

    fn maybe_initialize<'a>() -> &'a mut SubCallbackMap {
        unsafe {
            if SUB_HOOKS.is_none() {
                SUB_HOOKS.replace(HashMap::new());

                // unregister previous callback registration.  It's okay if this is the first time
                pg_sys::UnregisterSubXactCallback(Some(callback), std::ptr::null_mut());

                // register our new callback
                pg_sys::RegisterSubXactCallback(Some(callback), std::ptr::null_mut());

                // register transaction callbacks so we can clear our hooks when the transaction ends
                // this is necessary b/c it's possible for the user to register sub transaction callbacks
                // within a transaction but a subtransaction never actually occurs
                register_xact_callback(PgXactCallbackEvent::Commit, || {
                    // reset SUB_HOOKS to None on outer transaction COMMIT
                    SUB_HOOKS.take();
                });
                register_xact_callback(PgXactCallbackEvent::Abort, || {
                    // reset SUB_HOOKS to None on outer transaction ABORT
                    SUB_HOOKS.take();
                });
            }

            SUB_HOOKS.as_mut().expect("SUB_HOOKS was None during maybe_initialize()")
            // this should never happen
        }
    }

    let hooks = maybe_initialize();
    let entry = hooks.entry(which_event).or_default();
    let wrapped_func = Rc::new(RefCell::new(Some(SubXactCallbackWrapper(Box::new(f)))));
    entry.push(wrapped_func.clone());

    SubXactCallbackReceipt(wrapped_func)
}

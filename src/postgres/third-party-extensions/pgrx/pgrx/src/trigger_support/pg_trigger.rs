//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::heap_tuple::PgHeapTuple;
use crate::pg_sys;
use crate::pgbox::AllocatedByPostgres;
use crate::rel::PgRelation;
use crate::trigger_support::{
    called_as_trigger, PgTriggerError, PgTriggerLevel, PgTriggerOperation, PgTriggerWhen,
    TriggerEvent, TriggerTuple,
};
use std::ffi::c_char;

/**
The datatype accepted by a trigger

A safe structure providing an API similar to the constants provided in a PL/pgSQL function.

Usage examples exist in the module level docs.
*/
pub struct PgTrigger<'a> {
    trigger: &'a pg_sys::Trigger,
    trigger_data: &'a pgrx_pg_sys::TriggerData,
}

impl<'a> PgTrigger<'a> {
    /// Construct a new [`PgTrigger`] from a [`FunctionCallInfo`][pg_sys::FunctionCallInfo]
    ///
    /// Generally this would be automatically done for the user in a [`#[pg_trigger]`][crate::pg_trigger].
    ///
    /// # Safety
    ///
    /// This constructor attempts to do some checks for validity, but it is ultimately unsafe
    /// because it must dereference several raw pointers.
    ///
    /// Users should ensure the provided `fcinfo` is:
    ///
    /// * one provided by PostgreSQL during a trigger invocation,
    /// * references a relation that has at least a [`pg_sys::AccessShareLock`],
    /// * unharmed (the user has not mutated it since PostgreSQL provided it),
    ///
    /// If any of these conditions are untrue, this or any other function on this type is
    /// undefined behavior, hopefully panicking.
    ///
    /// # Notes
    ///
    /// This function needs to be public as it is used by the `#[pg_trigger]` macro code generation.
    /// It is not intended to be used directly by users as its `fcinfo` argument needs to be setup
    /// by Postgres, not to mention all the various trigger-related state Postgres sets up before
    /// even calling a trigger function.
    ///
    /// Marking this function `unsafe` allows us to assume that the provided `fcinfo` argument all
    /// surrounding Postgres state is correct for the usage context, and as such, allows us to provide
    /// a safe API to the internal trigger data.
    #[doc(hidden)]
    pub unsafe fn from_fcinfo(
        fcinfo: &'a pg_sys::FunctionCallInfoBaseData,
    ) -> Result<Self, PgTriggerError> {
        if !called_as_trigger(fcinfo as *const _ as *mut _) {
            return Err(PgTriggerError::NotTrigger);
        }

        let trigger_data = (fcinfo.context as *mut pg_sys::TriggerData)
            .as_ref()
            .ok_or(PgTriggerError::NullTriggerData)?;
        let trigger = trigger_data.tg_trigger.as_ref().ok_or(PgTriggerError::NullTrigger)?;

        Ok(Self { trigger, trigger_data })
    }

    /// Returns the new database row for INSERT/UPDATE operations in row-level triggers.
    ///
    /// Returns `None` in statement-level triggers and DELETE operations.
    // Derived from `pgrx_pg_sys::TriggerData.tg_newtuple` and `pgrx_pg_sys::TriggerData.tg_newslot.tts_tupleDescriptor`
    pub fn new(&self) -> Option<PgHeapTuple<'_, AllocatedByPostgres>> {
        // Safety: Given that we have a known good `FunctionCallInfo`, which PostgreSQL has checked is indeed a trigger,
        // containing a known good `TriggerData` which also contains a known good `Trigger`... and the user agreed to
        // our `unsafe` constructor safety rules, we choose to trust this is indeed a valid pointer offered to us by
        // PostgreSQL, and that it trusts it.
        unsafe { PgHeapTuple::from_trigger_data(self.trigger_data, TriggerTuple::New) }
    }

    /// Returns the old database row for UPDATE/DELETE operations in row-level triggers.
    ///
    /// Returns `None` in statement-level triggers and INSERT operations.
    // Derived from `pgrx_pg_sys::TriggerData.tg_trigtuple` and `pgrx_pg_sys::TriggerData.tg_newslot.tts_tupleDescriptor`
    pub fn old(&self) -> Option<PgHeapTuple<'_, AllocatedByPostgres>> {
        // Safety: Given that we have a known good `FunctionCallInfo`, which PostgreSQL has checked is indeed a trigger,
        // containing a known good `TriggerData` which also contains a known good `Trigger`... and the user agreed to
        // our `unsafe` constructor safety rules, we choose to trust this is indeed a valid pointer offered to us by
        // PostgreSQL, and that it trusts it.
        unsafe { PgHeapTuple::from_trigger_data(self.trigger_data, TriggerTuple::Old) }
    }

    /// Variable that contains the name of the trigger actually fired
    pub fn name(&self) -> Result<&str, PgTriggerError> {
        let name_ptr = self.trigger.tgname as *mut c_char;
        // Safety: Given that we have a known good `FunctionCallInfo`, which PostgreSQL has checked is indeed a trigger,
        // containing a known good `TriggerData` which also contains a known good `Trigger`... and the user agreed to
        // our `unsafe` constructor safety rules, we choose to trust this is indeed a valid pointer offered to us by
        // PostgreSQL, and that it trusts it.
        let name_cstr = unsafe { core::ffi::CStr::from_ptr(name_ptr) };
        let name_str = name_cstr.to_str()?;
        Ok(name_str)
    }

    /// The raw Postgres event that caused this trigger to fire
    pub fn event(&self) -> TriggerEvent {
        TriggerEvent(self.trigger_data.tg_event)
    }

    /// When the trigger was triggered (`BEFORE`, `AFTER`, `INSTEAD OF`)
    // Derived from `pgrx_pg_sys::TriggerData.tg_event`
    pub fn when(&self) -> Result<PgTriggerWhen, PgTriggerError> {
        PgTriggerWhen::try_from(TriggerEvent(self.trigger_data.tg_event))
    }

    /// The level, from the trigger definition (`ROW`, `STATEMENT`)
    // Derived from `pgrx_pg_sys::TriggerData.tg_event`
    pub fn level(&self) -> PgTriggerLevel {
        PgTriggerLevel::from(TriggerEvent(self.trigger_data.tg_event))
    }

    /// The operation for which the trigger was fired
    // Derived from `pgrx_pg_sys::TriggerData.tg_event`
    pub fn op(&self) -> Result<PgTriggerOperation, PgTriggerError> {
        PgTriggerOperation::try_from(TriggerEvent(self.trigger_data.tg_event))
    }

    /// the object ID of the table that caused the trigger invocation
    // Derived from `pgrx_pg_sys::TriggerData.tg_relation.rd_id`
    pub fn relid(&self) -> Result<pg_sys::Oid, PgTriggerError> {
        Ok(self.relation()?.oid())
    }

    /// The name of the old transition table of this trigger invocation
    // Derived from `pgrx_pg_sys::TriggerData.trigger.tgoldtable`
    pub fn old_transition_table_name(&self) -> Result<Option<&str>, PgTriggerError> {
        let tgoldtable = self.trigger.tgoldtable;
        if !tgoldtable.is_null() {
            // Safety: Given that we have a known good `FunctionCallInfo`, which PostgreSQL has checked is indeed a trigger,
            // containing a known good `TriggerData` which also contains a known good `Trigger`... and the user agreed to
            // our `unsafe` constructor safety rules, we choose to trust this is indeed a valid pointer offered to us by
            // PostgreSQL, and that it trusts it.
            let table_name_cstr = unsafe { core::ffi::CStr::from_ptr(tgoldtable) };
            let table_name_str = table_name_cstr.to_str()?;
            Ok(Some(table_name_str))
        } else {
            Ok(None)
        }
    }

    /// The name of the new transition table of this trigger invocation
    // Derived from `pgrx_pg_sys::TriggerData.trigger.tgoldtable`
    pub fn new_transition_table_name(&self) -> Result<Option<&str>, PgTriggerError> {
        let tgnewtable = self.trigger.tgnewtable;
        if !tgnewtable.is_null() {
            // Safety: Given that we have a known good `FunctionCallInfo`, which PostgreSQL has checked is indeed a trigger,
            // containing a known good `TriggerData` which also contains a known good `Trigger`... and the user agreed to
            // our `unsafe` constructor safety rules, we choose to trust this is indeed a valid pointer offered to us by
            // PostgreSQL, and that it trusts it.
            let table_name_cstr = unsafe { core::ffi::CStr::from_ptr(tgnewtable) };
            let table_name_str = table_name_cstr.to_str()?;
            Ok(Some(table_name_str))
        } else {
            Ok(None)
        }
    }

    /// The `PgRelation` corresponding to the trigger.
    pub fn relation(&self) -> Result<crate::PgRelation, PgTriggerError> {
        // SAFETY:  The creator of this PgTrigger asserted they used a correctly initialized
        // "fcinfo" structures that represent a trigger and that Postgres was in the proper
        // state to call a trigger.  This includes that the relation is already open with at
        // least an AccessShareLock
        unsafe { Ok(PgRelation::from_pg(self.trigger_data.tg_relation)) }
    }

    /// The name of the schema of the table that caused the trigger invocation
    pub fn table_name(&self) -> Result<String, PgTriggerError> {
        let relation = self.relation()?;
        Ok(relation.name().to_string())
    }

    /// The name of the schema of the table that caused the trigger invocation
    pub fn table_schema(&self) -> Result<String, PgTriggerError> {
        let relation = self.relation()?;
        Ok(relation.namespace().to_string())
    }

    /// The arguments from the CREATE TRIGGER statement
    // Derived from `pgrx_pg_sys::TriggerData.trigger.tgargs`
    pub fn extra_args(&self) -> Result<Vec<String>, PgTriggerError> {
        let tgargs = self.trigger.tgargs;
        let tgnargs = self.trigger.tgnargs;
        // Safety: Given that we have a known good `FunctionCallInfo`, which PostgreSQL has checked is indeed a trigger,
        // containing a known good `TriggerData` which also contains a known good `Trigger`... and the user agreed to
        // our `unsafe` constructor safety rules, we choose to trust this is indeed a valid pointer offered to us by
        // PostgreSQL, and that it trusts it.
        let slice: &[*mut c_char] =
            unsafe { core::slice::from_raw_parts(tgargs, tgnargs.try_into()?) };
        let args = slice
            .iter()
            .map(|v| {
                // Safety: Given that we have a known good `FunctionCallInfo`, which PostgreSQL has checked is indeed a trigger,
                // containing a known good `TriggerData` which also contains a known good `Trigger`... and the user agreed to
                // our `unsafe` constructor safety rules, we choose to trust this is indeed a valid pointer offered to us by
                // PostgreSQL, and that it trusts it.
                unsafe { core::ffi::CStr::from_ptr(*v) }.to_str().map(ToString::to_string)
            })
            .collect::<Result<_, core::str::Utf8Error>>()?;
        Ok(args)
    }

    /// A reference to the underlying [`Trigger`][pgrx_pg_sys::Trigger]
    pub fn trigger(&self) -> &'a pgrx_pg_sys::Trigger {
        self.trigger
    }

    /// A reference to the underlying [`TriggerData`][pgrx_pg_sys::TriggerData]
    pub fn trigger_data(&self) -> &'a pgrx_pg_sys::TriggerData {
        self.trigger_data
    }
}

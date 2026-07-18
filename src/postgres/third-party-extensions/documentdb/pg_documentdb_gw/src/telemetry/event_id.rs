/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/telemetry/event_id.rs
 *
 *-------------------------------------------------------------------------
 */

#[repr(u64)]
#[derive(Copy, Clone, Debug)]
pub enum EventId {
    Default = 1,
    Probe = 2000,
    RequestTrace = 2001,
    ConnectionPool = 2002,
}

impl EventId {
    pub const fn code(self) -> u64 {
        self as u64
    }
}

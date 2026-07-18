/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/context/request.rs
 *
 *-------------------------------------------------------------------------
 */

use crate::requests::{request_tracker::RequestTracker, Request, RequestInfo};

pub struct RequestContext<'a> {
    pub activity_id: &'a str,
    pub payload: &'a Request<'a>,
    pub info: &'a RequestInfo<'a>,
    pub tracker: &'a mut RequestTracker,
}

impl<'a> RequestContext<'a> {
    pub fn get_components(&mut self) -> (&Request<'a>, &RequestInfo<'a>, &mut RequestTracker) {
        (self.payload, self.info, self.tracker)
    }
}

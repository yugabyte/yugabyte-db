/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/telemetry/mod.rs
 *
 *-------------------------------------------------------------------------
 */

pub mod client_info;
pub mod event_id;

use crate::{
    context::ConnectionContext,
    error::ErrorCode,
    protocol::header::Header,
    requests::{request_tracker::RequestTracker, Request},
    responses::{CommandError, Response},
};
use async_trait::async_trait;
use dyn_clone::{clone_trait_object, DynClone};
use either::Either;

// TelemetryProvider takes care of emitting events and metrics
// for tracking the gateway.
#[expect(clippy::too_many_arguments)]
#[async_trait]
pub trait TelemetryProvider: Send + Sync + DynClone {
    // Emits an event for every CRUD request dispatched to backend
    async fn emit_request_event(
        &self,
        _: &ConnectionContext,
        _: &Header,
        _: Option<&Request<'_>>,
        _: Either<&Response, (&CommandError, usize)>,
        _: String,
        _: &RequestTracker,
        _: &str,
        _: &str,
    );
}

clone_trait_object!(TelemetryProvider);

pub fn error_code_to_status_code(error: i32) -> u16 {
    match ErrorCode::from_i32(error) {
        Some(ErrorCode::Ok) => 200,
        Some(ErrorCode::AuthenticationFailed | ErrorCode::Unauthorized) => 401,
        Some(ErrorCode::InternalError) => 500,
        Some(ErrorCode::ExceededTimeLimit) => 408,
        Some(ErrorCode::DuplicateKey) => 409,
        _ => 400,
    }
}

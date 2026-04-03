/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/requests/request_tracker.rs
 *
 *-------------------------------------------------------------------------
 */

use tokio::time::Instant;

#[derive(Debug)]
pub enum RequestIntervalKind {
    /// Interval kind for reading stream from request body. BufferRead + HandleRequest is the full duration of a request spent in the Gateway.
    BufferRead,

    /// Interval kind for the overall request processing duration, which includes FormatRequest, FormatResponse, and ProcessRequest via backend.
    HandleRequest,

    /// Time spent formatting and parsing the incoming request.
    FormatRequest,

    /// Time spent processing the response before sending it back to the client.
    FormatResponse,

    /// Time spent in network transport and Postgres processing.
    ProcessRequest,

    /// Time spent beginning a Postgres transaction.
    PostgresBeginTransaction,

    /// Time spent setting statement timeout parameters in Postgres.
    PostgresSetStatementTimeout,

    /// Time spent committing a Postgres transaction.
    PostgresTransactionCommit,

    /// Special value used to define the size of the metrics array.
    MaxUnused,
}

#[derive(Debug, Default)]
pub struct RequestTracker {
    pub request_interval_metrics_array: [i64; RequestIntervalKind::MaxUnused as usize],
}

impl RequestTracker {
    pub fn new() -> Self {
        RequestTracker {
            request_interval_metrics_array: [0; RequestIntervalKind::MaxUnused as usize],
        }
    }

    pub fn start_timer(&self) -> Instant {
        Instant::now()
    }

    pub fn record_duration(&mut self, interval: RequestIntervalKind, start_time: Instant) {
        let elapsed = start_time.elapsed();
        self.request_interval_metrics_array[interval as usize] += elapsed.as_nanos() as i64;
    }

    pub fn get_interval_elapsed_time(&self, interval: RequestIntervalKind) -> i64 {
        self.request_interval_metrics_array[interval as usize]
    }
}

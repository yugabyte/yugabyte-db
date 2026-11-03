// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

use std::future::Future;
use std::time::Duration;

use rand::Rng;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};

/// Exponential backoff with full jitter, bounded by attempts and an overall deadline. The
/// defaults are the C++ master's (`RetryingRpcTask`); unlike it, the jitter is random, so retries
/// from many tasks spread out.
#[derive(Clone, Copy, Debug)]
pub struct RetryPolicy {
    /// The cap of the first delay; doubles per attempt.
    pub base_delay: Duration,
    /// `retrying_ts_rpc_max_delay_ms`.
    pub max_delay: Duration,
    /// `unresponsive_ts_rpc_retry_limit`.
    pub max_attempts: u32,
    /// `unresponsive_ts_rpc_timeout_ms`.
    pub deadline: Duration,
}

impl Default for RetryPolicy {
    fn default() -> Self {
        RetryPolicy {
            base_delay: Duration::from_millis(16),
            max_delay: Duration::from_secs(60),
            max_attempts: 20,
            deadline: Duration::from_secs(15 * 60),
        }
    }
}

/// Whether a failed attempt may be retried.
#[derive(Debug)]
pub enum Attempt {
    /// Retry after a backoff.
    Retry(Status),
    /// Give up with this status.
    Fail(Status),
}

impl RetryPolicy {
    /// The delay before retry number `attempt` (0-based): uniform in [0, min(max, base * 2^n)].
    pub fn delay(&self, attempt: u32) -> Duration {
        let cap = self
            .base_delay
            .saturating_mul(1u32.checked_shl(attempt.min(31)).unwrap_or(u32::MAX))
            .min(self.max_delay);
        let millis = cap.as_millis() as u64;
        Duration::from_millis(rand::rng().random_range(0..=millis))
    }

    /// Calls `attempt` until it succeeds, fails for good, or the attempts or deadline run out.
    pub async fn run<T, F, Fut>(&self, mut attempt: F) -> Result<T, Status>
    where
        F: FnMut(u32) -> Fut,
        Fut: Future<Output = Result<T, Attempt>>,
    {
        let deadline = Instant::now() + self.deadline;
        let mut last = Status::new(StatusCode::TimedOut, "no attempt made");
        for n in 0..self.max_attempts.max(1) {
            match attempt(n).await {
                Ok(value) => return Ok(value),
                Err(Attempt::Fail(status)) => return Err(status),
                Err(Attempt::Retry(status)) => last = status,
            }
            let wake = Instant::now() + self.delay(n);
            if wake >= deadline {
                break;
            }
            tokio::time::sleep_until(wake).await;
        }
        Err(Status::new(
            last.code,
            format!("giving up after retries: {}", last.message),
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn delay_is_capped_and_jittered() {
        let policy = RetryPolicy {
            base_delay: Duration::from_millis(10),
            max_delay: Duration::from_millis(100),
            ..Default::default()
        };
        let mut seen = std::collections::BTreeSet::new();
        for _ in 0..200 {
            let d = policy.delay(10);
            assert!(d <= Duration::from_millis(100));
            seen.insert(d);
        }
        // Real jitter, not a constant (the C++ seed is fixed).
        assert!(seen.len() > 10);
        assert!(policy.delay(0) <= Duration::from_millis(10));
        assert!(policy.delay(1000) <= Duration::from_millis(100));
    }

    #[tokio::test(start_paused = true)]
    async fn run_retries_until_success_or_failure() {
        let policy = RetryPolicy::default();
        let result = policy
            .run(|n| async move {
                if n < 3 {
                    Err(Attempt::Retry(Status::new(StatusCode::TryAgain, "busy")))
                } else {
                    Ok(n)
                }
            })
            .await;
        assert_eq!(result.unwrap(), 3);

        let result: Result<(), _> = policy
            .run(|_| async { Err(Attempt::Fail(Status::new(StatusCode::NotFound, "gone"))) })
            .await;
        assert_eq!(result.unwrap_err().code, StatusCode::NotFound);

        let limited = RetryPolicy {
            max_attempts: 4,
            ..Default::default()
        };
        let mut calls = 0;
        let result: Result<(), _> = limited
            .run(|_| {
                calls += 1;
                async { Err(Attempt::Retry(Status::new(StatusCode::TryAgain, "busy"))) }
            })
            .await;
        assert!(result.is_err());
        assert_eq!(calls, 4);
    }
}

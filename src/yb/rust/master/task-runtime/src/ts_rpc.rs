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

//! Paced master-to-tserver RPCs (K2): one FIFO per tserver and RPC kind, with a cap on calls in
//! flight.
//!
//! Waiting for a slot is queueing, not failure. C++ (`AsyncTaskThrottler`) counts a throttled
//! send as a failed attempt and backs off for up to a minute, so dropping a table with many
//! tablets runs out of attempts or time while most deletes sit in backoff (#24013). Here the
//! retry policy sees only sends that failed, and its overall deadline starts with the first send.

use std::collections::{HashMap, VecDeque};
use std::future::Future;
use std::sync::{Arc, Mutex};

use tokio::sync::oneshot;
use yb_host_api::Status;

use crate::{Attempt, RetryPolicy};

/// A tserver's uuid and an RPC kind.
type LaneKey = (Vec<u8>, &'static str);

/// Runs RPCs to tservers, at most `limit(kind)` in flight per tserver and kind.
pub struct TsRpcExecutor {
    lanes: Mutex<HashMap<LaneKey, Arc<Lane>>>,
    limit: fn(&str) -> usize,
}

struct Lane {
    kind: &'static str,
    limit: fn(&str) -> usize,
    state: Mutex<LaneState>,
}

#[derive(Default)]
struct LaneState {
    in_flight: usize,
    waiters: VecDeque<oneshot::Sender<Permit>>,
}

/// A slot in a lane, released on drop.
struct Permit {
    /// `None` once the slot was taken back from a waiter that gave up.
    lane: Option<Arc<Lane>>,
}

impl Lane {
    fn limit(&self) -> usize {
        (self.limit)(self.kind).max(1)
    }

    async fn acquire(self: &Arc<Lane>) -> Permit {
        let limit = self.limit();
        let receiver = {
            let mut state = self.state.lock().unwrap();
            if state.waiters.is_empty() && state.in_flight < limit {
                state.in_flight += 1;
                return Permit {
                    lane: Some(self.clone()),
                };
            }
            let (sender, receiver) = oneshot::channel();
            state.waiters.push_back(sender);
            receiver
        };
        // The limit may have been raised since the last call finished.
        self.grant();
        // The sender is only dropped together with the lane, which `self` keeps alive.
        receiver.await.expect("a lane outlives its waiters")
    }

    fn release(self: &Arc<Lane>) {
        self.state.lock().unwrap().in_flight -= 1;
        self.grant();
    }

    /// Hands slots to the oldest waiters while fewer than `limit()` calls are in flight, so a
    /// raised limit admits waiters at the next call or completion, and a lowered one takes effect
    /// as calls finish.
    fn grant(self: &Arc<Lane>) {
        loop {
            let limit = self.limit();
            let sender = {
                let mut state = self.state.lock().unwrap();
                if state.in_flight >= limit {
                    return;
                }
                let Some(sender) = state.waiters.pop_front() else {
                    return;
                };
                state.in_flight += 1;
                sender
            };
            let permit = Permit {
                lane: Some(self.clone()),
            };
            if let Err(mut permit) = sender.send(permit) {
                // The waiter gave up (its task was cancelled): take the slot back for the next.
                permit.lane = None;
                self.state.lock().unwrap().in_flight -= 1;
            }
        }
    }
}

impl Drop for Permit {
    fn drop(&mut self) {
        if let Some(lane) = self.lane.take() {
            lane.release();
        }
    }
}

impl TsRpcExecutor {
    /// An executor whose per-tserver cap for an RPC kind is `limit(kind)`, read on every send so
    /// runtime flag changes apply. Kinds are the RPC method names, e.g. `"DeleteTablet"`.
    pub fn new(limit: fn(&str) -> usize) -> Arc<TsRpcExecutor> {
        Arc::new(TsRpcExecutor {
            lanes: Mutex::default(),
            limit,
        })
    }

    fn lane(&self, ts_uuid: &[u8], kind: &'static str) -> Arc<Lane> {
        self.lanes
            .lock()
            .unwrap()
            .entry((ts_uuid.to_vec(), kind))
            .or_insert_with(|| {
                Arc::new(Lane {
                    kind,
                    limit: self.limit,
                    state: Mutex::default(),
                })
            })
            .clone()
    }

    /// Calls `send` in the tserver's lane for `kind` until it succeeds, fails for good, or
    /// `policy` runs out. `send` gets the 0-based attempt number. Each attempt holds a slot only
    /// while it runs; backoff happens outside the lane. The deadline starts with the first send,
    /// and no attempt starts after it, however long the wait for a slot was.
    pub async fn run<T, F, Fut>(
        &self,
        ts_uuid: &[u8],
        kind: &'static str,
        policy: &RetryPolicy,
        send: F,
    ) -> Result<T, Status>
    where
        F: FnMut(u32) -> Fut,
        Fut: Future<Output = Result<T, Attempt>>,
    {
        let lane = self.lane(ts_uuid, kind);
        policy.run_gated(|| lane.acquire(), send).await
    }

    /// Calls in flight and waiting in the tserver's lane for `kind`.
    pub fn load(&self, ts_uuid: &[u8], kind: &'static str) -> (usize, usize) {
        let lane = self.lane(ts_uuid, kind);
        let state = lane.state.lock().unwrap();
        (state.in_flight, state.waiters.len())
    }
}

#[cfg(test)]
mod tests {
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::Duration;

    use super::*;
    use yb_host_api::StatusCode;

    fn two(_: &str) -> usize {
        2
    }

    #[tokio::test(start_paused = true)]
    async fn caps_calls_in_flight_and_serves_in_order() {
        let executor = TsRpcExecutor::new(two);
        let running = Arc::new(AtomicUsize::new(0));
        let peak = Arc::new(AtomicUsize::new(0));
        let order = Arc::new(Mutex::new(Vec::new()));
        let mut tasks = Vec::new();
        for i in 0..10 {
            let (executor, running, peak, order) = (
                executor.clone(),
                running.clone(),
                peak.clone(),
                order.clone(),
            );
            tasks.push(tokio::spawn(async move {
                executor
                    .run(b"ts", "DeleteTablet", &RetryPolicy::default(), |_| {
                        let (running, peak, order) = (running.clone(), peak.clone(), order.clone());
                        async move {
                            order.lock().unwrap().push(i);
                            let now = running.fetch_add(1, Ordering::SeqCst) + 1;
                            peak.fetch_max(now, Ordering::SeqCst);
                            tokio::time::sleep(Duration::from_millis(10)).await;
                            running.fetch_sub(1, Ordering::SeqCst);
                            Ok::<_, Attempt>(())
                        }
                    })
                    .await
            }));
            // Queue them in a known order.
            tokio::task::yield_now().await;
        }
        for task in tasks {
            task.await.unwrap().unwrap();
        }
        assert_eq!(peak.load(Ordering::SeqCst), 2);
        assert_eq!(*order.lock().unwrap(), (0..10).collect::<Vec<_>>());
        assert_eq!(executor.load(b"ts", "DeleteTablet"), (0, 0));
    }

    #[tokio::test(start_paused = true)]
    async fn waiting_for_a_slot_is_not_an_attempt() {
        fn one(_: &str) -> usize {
            1
        }
        let executor = TsRpcExecutor::new(one);
        // Two attempts, and an overall deadline shorter than the time the second call waits.
        let policy = RetryPolicy {
            max_attempts: 2,
            deadline: Duration::from_secs(1),
            ..Default::default()
        };
        let slow = {
            let executor = executor.clone();
            tokio::spawn(async move {
                executor
                    .run(b"ts", "DeleteTablet", &policy, |_| async {
                        tokio::time::sleep(Duration::from_secs(30)).await;
                        Ok::<_, Attempt>(())
                    })
                    .await
            })
        };
        tokio::task::yield_now().await;
        let attempts = AtomicUsize::new(0);
        let result = executor
            .run(b"ts", "DeleteTablet", &policy, |_| {
                attempts.fetch_add(1, Ordering::SeqCst);
                async { Ok::<_, Attempt>(()) }
            })
            .await;
        assert!(result.is_ok());
        assert_eq!(attempts.load(Ordering::SeqCst), 1);
        slow.await.unwrap().unwrap();
    }

    #[tokio::test(start_paused = true)]
    async fn lanes_are_per_tserver_and_kind_and_cancelled_waiters_pass_the_slot_on() {
        fn one(_: &str) -> usize {
            1
        }
        let executor = TsRpcExecutor::new(one);
        let hold = |ts: &'static [u8], kind: &'static str| {
            let executor = executor.clone();
            tokio::spawn(async move {
                executor
                    .run(ts, kind, &RetryPolicy::default(), |_| async {
                        tokio::time::sleep(Duration::from_secs(5)).await;
                        Ok::<_, Attempt>(())
                    })
                    .await
            })
        };
        let a = hold(b"a", "DeleteTablet");
        let b = hold(b"b", "DeleteTablet");
        let c = hold(b"a", "AlterSchema");
        tokio::task::yield_now().await;
        assert_eq!(executor.load(b"a", "DeleteTablet"), (1, 0));
        assert_eq!(executor.load(b"b", "DeleteTablet"), (1, 0));
        assert_eq!(executor.load(b"a", "AlterSchema"), (1, 0));

        // A waiter that is cancelled before its turn does not take the slot with it.
        let cancelled = hold(b"a", "DeleteTablet");
        tokio::task::yield_now().await;
        let next = hold(b"a", "DeleteTablet");
        tokio::task::yield_now().await;
        assert_eq!(executor.load(b"a", "DeleteTablet"), (1, 2));
        cancelled.abort();
        for task in [a, b, c, next] {
            task.await.unwrap().unwrap();
        }
        assert_eq!(executor.load(b"a", "DeleteTablet"), (0, 0));
    }

    #[tokio::test(start_paused = true)]
    async fn failed_sends_back_off_outside_the_lane() {
        fn one(_: &str) -> usize {
            1
        }
        let executor = TsRpcExecutor::new(one);
        let policy = RetryPolicy {
            max_attempts: 3,
            ..Default::default()
        };
        let result: Result<(), _> = executor
            .run(b"ts", "DeleteTablet", &policy, |n| async move {
                if n == 0 {
                    Err(Attempt::Retry(Status::new(
                        StatusCode::NetworkError,
                        "down",
                    )))
                } else {
                    Err(Attempt::Fail(Status::new(StatusCode::NotFound, "gone")))
                }
            })
            .await;
        assert_eq!(result.unwrap_err().code, StatusCode::NotFound);
        assert_eq!(executor.load(b"ts", "DeleteTablet"), (0, 0));
    }

    #[tokio::test(start_paused = true)]
    async fn a_raised_limit_admits_waiters() {
        static LIMIT: AtomicUsize = AtomicUsize::new(1);
        fn limit(_: &str) -> usize {
            LIMIT.load(Ordering::SeqCst)
        }
        let executor = TsRpcExecutor::new(limit);
        let hold = || {
            let executor = executor.clone();
            tokio::spawn(async move {
                executor
                    .run(b"ts", "DeleteTablet", &RetryPolicy::default(), |_| async {
                        tokio::time::sleep(Duration::from_secs(5)).await;
                        Ok::<_, Attempt>(())
                    })
                    .await
            })
        };
        let mut tasks: Vec<_> = (0..4).map(|_| hold()).collect();
        tokio::task::yield_now().await;
        assert_eq!(executor.load(b"ts", "DeleteTablet"), (1, 3));

        // The next call admits the queued ones up to the new limit, oldest first.
        LIMIT.store(4, Ordering::SeqCst);
        tasks.push(hold());
        tokio::task::yield_now().await;
        assert_eq!(executor.load(b"ts", "DeleteTablet"), (4, 1));
        for task in tasks {
            task.await.unwrap().unwrap();
        }
        assert_eq!(executor.load(b"ts", "DeleteTablet"), (0, 0));
    }

    #[tokio::test(start_paused = true)]
    async fn no_attempt_starts_after_the_deadline() {
        fn one(_: &str) -> usize {
            1
        }
        let executor = TsRpcExecutor::new(one);
        let policy = RetryPolicy {
            max_attempts: 5,
            deadline: Duration::from_secs(1),
            ..Default::default()
        };
        let attempts = AtomicUsize::new(0);
        let mut holder = None;
        let result: Result<(), _> = executor
            .run(b"ts", "DeleteTablet", &policy, |_| {
                attempts.fetch_add(1, Ordering::SeqCst);
                // Another call queues behind this attempt, then holds the slot for longer than
                // this call's deadline.
                if holder.is_none() {
                    let executor = executor.clone();
                    holder = Some(tokio::spawn(async move {
                        executor
                            .run(b"ts", "DeleteTablet", &RetryPolicy::default(), |_| async {
                                tokio::time::sleep(Duration::from_secs(30)).await;
                                Ok::<_, Attempt>(())
                            })
                            .await
                    }));
                }
                async {
                    tokio::task::yield_now().await;
                    Err(Attempt::Retry(Status::new(
                        StatusCode::NetworkError,
                        "down",
                    )))
                }
            })
            .await;
        assert_eq!(result.unwrap_err().code, StatusCode::NetworkError);
        assert_eq!(attempts.load(Ordering::SeqCst), 1);
        holder.unwrap().await.unwrap().unwrap();
    }
}

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

//! Heartbeats of one tserver are processed one at a time, in the order they arrived.
//!
//! A tserver normally has one heartbeat outstanding, but a retry after a timeout can overlap the
//! one still being processed, and the older one must not overwrite what the newer one reported.
//! The handler takes a [`Ticket`] on the RPC thread, before it hands the call to the async
//! runtime, so the order is the arrival order rather than the order tasks happen to be polled.

use std::collections::{BTreeSet, HashMap};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use tokio::sync::Notify;
use yb_host_api::{Status, StatusCode};

/// Hands out per-tserver tickets.
#[derive(Default)]
pub struct HeartbeatOrder {
    queues: Mutex<HashMap<Vec<u8>, Arc<Queue>>>,
}

#[derive(Default)]
struct Queue {
    state: Mutex<QueueState>,
    turn: Notify,
}

#[derive(Default)]
struct QueueState {
    next: u64,
    serving: u64,
    /// Tickets dropped before their turn; skipped when the turn reaches them.
    abandoned: BTreeSet<u64>,
}

/// A place in one tserver's queue. Dropping it, served or not, lets the next heartbeat go.
pub struct Ticket {
    queue: Arc<Queue>,
    number: u64,
}

impl HeartbeatOrder {
    /// The next ticket for the tserver with `uuid`.
    pub fn ticket(&self, uuid: &[u8]) -> Ticket {
        let queue = self
            .queues
            .lock()
            .unwrap()
            .entry(uuid.to_vec())
            .or_default()
            .clone();
        let number = {
            let mut state = queue.state.lock().unwrap();
            state.next += 1;
            state.next - 1
        };
        Ticket { queue, number }
    }
}

impl Ticket {
    /// Waits for this ticket's turn, at most `timeout`: the time left to the call's deadline.
    /// After a timeout the call has expired and must not be processed.
    pub async fn wait(&self, timeout: Duration) -> Result<(), Status> {
        let wait = async {
            loop {
                let turn = self.queue.turn.notified();
                tokio::pin!(turn);
                // Registers interest before checking, so a turn change in between is not missed.
                turn.as_mut().enable();
                if self.queue.state.lock().unwrap().serving == self.number {
                    return;
                }
                turn.await;
            }
        };
        tokio::time::timeout(timeout, wait).await.map_err(|_| {
            Status::new(
                StatusCode::TimedOut,
                "Heartbeat expired while an earlier heartbeat of the same tserver was processed",
            )
        })
    }
}

impl Drop for Ticket {
    fn drop(&mut self) {
        let mut state = self.queue.state.lock().unwrap();
        if state.serving == self.number {
            state.serving += 1;
            loop {
                let serving = state.serving;
                if !state.abandoned.remove(&serving) {
                    break;
                }
                state.serving += 1;
            }
        } else {
            state.abandoned.insert(self.number);
        }
        drop(state);
        self.queue.turn.notify_waiters();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const LONG: Duration = Duration::from_secs(10);

    #[tokio::test]
    async fn served_in_ticket_order() {
        let order = Arc::new(HeartbeatOrder::default());
        let tickets: Vec<Ticket> = (0..5).map(|_| order.ticket(b"ts")).collect();
        let (done, mut seen) = tokio::sync::mpsc::unbounded_channel();
        let mut tasks = Vec::new();
        // Started in reverse, so polling order alone would serve the last ticket first.
        for (i, ticket) in tickets.into_iter().enumerate().rev() {
            let done = done.clone();
            tasks.push(tokio::spawn(async move {
                ticket.wait(LONG).await.unwrap();
                tokio::task::yield_now().await;
                done.send(i).unwrap();
            }));
        }
        for task in tasks {
            task.await.unwrap();
        }
        let mut order_seen = Vec::new();
        while let Ok(i) = seen.try_recv() {
            order_seen.push(i);
        }
        assert_eq!(order_seen, vec![0, 1, 2, 3, 4]);
    }

    #[tokio::test]
    async fn tservers_do_not_wait_for_each_other() {
        let order = HeartbeatOrder::default();
        let _held = order.ticket(b"a");
        order
            .ticket(b"b")
            .wait(Duration::from_millis(100))
            .await
            .unwrap();
    }

    #[tokio::test]
    async fn expired_and_abandoned_tickets_do_not_block_later_ones() {
        let order = HeartbeatOrder::default();
        let first = order.ticket(b"ts");
        let second = order.ticket(b"ts");
        let third = order.ticket(b"ts");
        let fourth = order.ticket(b"ts");
        // The second expires behind the first.
        let status = second.wait(Duration::from_millis(20)).await.unwrap_err();
        assert_eq!(status.code, StatusCode::TimedOut);
        drop(second);
        // The third is dropped unanswered before its turn.
        drop(third);
        drop(first);
        fourth.wait(Duration::from_millis(100)).await.unwrap();
    }
}

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

//! How the master runs background work. See docs/task-runtime.md.
//!
//! - [`TermScope`]: everything a leader starts in one term. Cancelling it (a term change, a PITR
//!   restore) cancels every task in it before a token for the next state exists.
//! - [`EntityScope`]: a child scope for one entity, so dropping a table cancels its tasks.
//! - [`Controller`]: reconciles observed state with the catalog; re-run on change and on a timer.
//! - [`RetryPolicy`]: exponential backoff with full jitter.
//! - [`BlockingPool`]: fixed threads and a bounded queue for blocking calls; a full queue is an
//!   error, never a hidden wait.

mod blocking;
mod retry;
mod ts_rpc;

pub use blocking::BlockingPool;
pub use retry::{Attempt, RetryPolicy};
pub use ts_rpc::TsRpcExecutor;

use std::collections::BTreeMap;
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use tokio::sync::watch;
use tokio_util::sync::CancellationToken;
use tokio_util::task::TaskTracker;

/// A boxed future, for trait objects.
pub type BoxFuture<'a, T> = Pin<Box<dyn Future<Output = T> + Send + 'a>>;

/// Counts of running tasks by name, for the /tasks page and tests.
#[derive(Default)]
pub struct TaskRegistry {
    running: Mutex<BTreeMap<&'static str, usize>>,
}

impl TaskRegistry {
    /// Running tasks by name.
    pub fn running(&self) -> BTreeMap<&'static str, usize> {
        self.running.lock().unwrap().clone()
    }

    fn start(self: &Arc<Self>, name: &'static str) -> RunningGuard {
        *self.running.lock().unwrap().entry(name).or_default() += 1;
        RunningGuard {
            registry: self.clone(),
            name,
        }
    }
}

struct RunningGuard {
    registry: Arc<TaskRegistry>,
    name: &'static str,
}

impl Drop for RunningGuard {
    fn drop(&mut self) {
        let mut running = self.registry.running.lock().unwrap();
        if let Some(count) = running.get_mut(self.name) {
            *count -= 1;
            if *count == 0 {
                running.remove(self.name);
            }
        }
    }
}

/// The work a leader runs in one term.
#[derive(Clone)]
pub struct TermScope {
    term: i64,
    token: CancellationToken,
    tracker: TaskTracker,
    registry: Arc<TaskRegistry>,
}

impl TermScope {
    /// A new scope for `term`.
    pub fn new(term: i64, registry: Arc<TaskRegistry>) -> TermScope {
        TermScope {
            term,
            token: CancellationToken::new(),
            tracker: TaskTracker::new(),
            registry,
        }
    }

    /// The leader term.
    pub fn term(&self) -> i64 {
        self.term
    }

    /// Runs `future` until it finishes or the scope is cancelled.
    pub fn spawn(&self, name: &'static str, future: impl Future<Output = ()> + Send + 'static) {
        spawn_in(&self.token, &self.tracker, &self.registry, name, future);
    }

    /// A child scope for one entity.
    pub fn entity(&self) -> EntityScope {
        EntityScope {
            token: self.token.child_token(),
            tracker: self.tracker.clone(),
            registry: self.registry.clone(),
        }
    }

    /// Cancels every task in the scope and its entity scopes.
    pub fn cancel(&self) {
        self.token.cancel();
        self.tracker.close();
    }

    /// Whether [`TermScope::cancel`] was called.
    pub fn is_cancelled(&self) -> bool {
        self.token.is_cancelled()
    }

    /// Resolves once the scope is cancelled.
    pub async fn cancelled(&self) {
        self.token.cancelled().await
    }

    /// Resolves once the scope is cancelled and all its tasks have stopped.
    pub async fn wait_stopped(&self) {
        self.tracker.close();
        self.tracker.wait().await
    }
}

/// The work for one entity within a term.
#[derive(Clone)]
pub struct EntityScope {
    token: CancellationToken,
    tracker: TaskTracker,
    registry: Arc<TaskRegistry>,
}

impl EntityScope {
    /// Runs `future` until it finishes or the scope (or its term) is cancelled.
    pub fn spawn(&self, name: &'static str, future: impl Future<Output = ()> + Send + 'static) {
        spawn_in(&self.token, &self.tracker, &self.registry, name, future);
    }

    /// Cancels this entity's tasks.
    pub fn cancel(&self) {
        self.token.cancel();
    }

    /// Whether the scope or its term was cancelled.
    pub fn is_cancelled(&self) -> bool {
        self.token.is_cancelled()
    }
}

fn spawn_in(
    token: &CancellationToken,
    tracker: &TaskTracker,
    registry: &Arc<TaskRegistry>,
    name: &'static str,
    future: impl Future<Output = ()> + Send + 'static,
) {
    if token.is_cancelled() {
        return;
    }
    let token = token.clone();
    let guard = registry.start(name);
    tracker.spawn(async move {
        let _guard = guard;
        tokio::select! {
            _ = token.cancelled() => {}
            _ = future => {}
        }
    });
}

/// Brings observed state in line with the catalog. Re-run whenever its trigger changes and at
/// least every [`Controller::interval`]; each run must be idempotent.
pub trait Controller: Send + Sync + 'static {
    /// For logs and the task registry.
    fn name(&self) -> &'static str;
    /// The longest time between runs.
    fn interval(&self) -> Duration;
    /// One pass.
    fn reconcile<'a>(&'a self, scope: &'a TermScope) -> BoxFuture<'a, ()>;
}

/// Runs `controller` in `scope`: once at start, then on every change of `trigger` and every
/// interval, until the scope is cancelled.
pub fn run_controller(
    scope: &TermScope,
    controller: Arc<dyn Controller>,
    mut trigger: watch::Receiver<u64>,
) {
    let run_scope = scope.clone();
    scope.spawn(controller.name(), async move {
        loop {
            controller.reconcile(&run_scope).await;
            tokio::select! {
                changed = trigger.changed() => {
                    if changed.is_err() {
                        // The trigger is gone; fall back to the timer alone.
                        tokio::time::sleep(controller.interval()).await;
                    }
                }
                _ = tokio::time::sleep(controller.interval()) => {}
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    #[tokio::test]
    async fn cancelling_a_term_stops_entity_tasks() {
        let registry = Arc::new(TaskRegistry::default());
        let scope = TermScope::new(3, registry.clone());
        let entity = scope.entity();
        entity.spawn("forever", std::future::pending());
        scope.spawn("forever", std::future::pending());
        assert_eq!(registry.running().get("forever"), Some(&2));
        scope.cancel();
        scope.wait_stopped().await;
        assert!(registry.running().is_empty());
        assert!(entity.is_cancelled());
        // Nothing starts in a cancelled scope.
        scope.spawn("late", async {});
        assert!(registry.running().is_empty());
    }

    #[tokio::test]
    async fn entity_cancel_leaves_the_term_running() {
        let registry = Arc::new(TaskRegistry::default());
        let scope = TermScope::new(1, registry.clone());
        let entity = scope.entity();
        entity.spawn("a", std::future::pending());
        scope.spawn("b", std::future::pending());
        entity.cancel();
        tokio::task::yield_now().await;
        tokio::time::sleep(Duration::from_millis(10)).await;
        assert_eq!(registry.running().get("a"), None);
        assert_eq!(registry.running().get("b"), Some(&1));
        scope.cancel();
    }

    struct Counting(Arc<AtomicUsize>);

    impl Controller for Counting {
        fn name(&self) -> &'static str {
            "counting"
        }
        fn interval(&self) -> Duration {
            Duration::from_secs(3600)
        }
        fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
            Box::pin(async move {
                self.0.fetch_add(1, Ordering::SeqCst);
            })
        }
    }

    #[tokio::test]
    async fn controller_reruns_on_trigger() {
        let scope = TermScope::new(1, Arc::default());
        let runs = Arc::new(AtomicUsize::new(0));
        let (sender, receiver) = watch::channel(0);
        run_controller(&scope, Arc::new(Counting(runs.clone())), receiver);
        for _ in 0..100 {
            if runs.load(Ordering::SeqCst) == 1 {
                break;
            }
            tokio::time::sleep(Duration::from_millis(1)).await;
        }
        sender.send(1).unwrap();
        for _ in 0..100 {
            if runs.load(Ordering::SeqCst) == 2 {
                break;
            }
            tokio::time::sleep(Duration::from_millis(1)).await;
        }
        assert_eq!(runs.load(Ordering::SeqCst), 2);
        scope.cancel();
    }
}

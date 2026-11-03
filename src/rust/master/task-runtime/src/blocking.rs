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

use std::sync::mpsc::{sync_channel, SyncSender, TrySendError};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;

use tokio::sync::oneshot;
use yb_host_api::{Status, StatusCode};

type Job = Box<dyn FnOnce() + Send>;

/// Fixed threads for one kind of blocking call, with a bounded queue. Each subsystem that blocks
/// gets its own pool, so work queued behind a blocked call can never be the work it waits for.
pub struct BlockingPool {
    name: &'static str,
    sender: Mutex<Option<SyncSender<Job>>>,
    threads: Mutex<Vec<JoinHandle<()>>>,
}

impl BlockingPool {
    /// Starts `threads` threads sharing a queue of `queue_limit` jobs.
    pub fn new(name: &'static str, threads: usize, queue_limit: usize) -> Arc<BlockingPool> {
        let (sender, receiver) = sync_channel::<Job>(queue_limit);
        let receiver = Arc::new(Mutex::new(receiver));
        let handles = (0..threads.max(1))
            .map(|i| {
                let receiver = receiver.clone();
                std::thread::Builder::new()
                    .name(format!("{name}-{i}"))
                    .spawn(move || loop {
                        let job = receiver.lock().unwrap().recv();
                        match job {
                            Ok(job) => job(),
                            Err(_) => return,
                        }
                    })
                    .expect("spawning a blocking pool thread")
            })
            .collect();
        Arc::new(BlockingPool {
            name,
            sender: Mutex::new(Some(sender)),
            threads: Mutex::new(handles),
        })
    }

    /// Queues `f`; `Busy` when the queue is full, `Aborted` after shutdown.
    pub fn submit<T: Send + 'static>(
        &self,
        f: impl FnOnce() -> T + Send + 'static,
    ) -> Result<oneshot::Receiver<T>, Status> {
        let (sender, receiver) = oneshot::channel();
        let job: Job = Box::new(move || {
            let _ = sender.send(f());
        });
        let guard = self.sender.lock().unwrap();
        let Some(queue) = guard.as_ref() else {
            return Err(Status::new(
                StatusCode::Aborted,
                format!("{} is shut down", self.name),
            ));
        };
        match queue.try_send(job) {
            Ok(()) => Ok(receiver),
            Err(TrySendError::Full(_)) => Err(Status::new(
                StatusCode::Busy,
                format!("{} queue is full", self.name),
            )),
            Err(TrySendError::Disconnected(_)) => Err(Status::new(
                StatusCode::Aborted,
                format!("{} is shut down", self.name),
            )),
        }
    }

    /// Runs `f` on the pool and waits for it.
    pub async fn run<T: Send + 'static>(
        &self,
        f: impl FnOnce() -> T + Send + 'static,
    ) -> Result<T, Status> {
        self.submit(f)?.await.map_err(|_| {
            Status::new(
                StatusCode::Aborted,
                format!("{} dropped the job", self.name),
            )
        })
    }

    /// Lets queued jobs finish and joins the threads.
    pub fn shutdown(&self) {
        self.sender.lock().unwrap().take();
        for handle in self.threads.lock().unwrap().drain(..) {
            let _ = handle.join();
        }
    }
}

impl Drop for BlockingPool {
    fn drop(&mut self) {
        self.sender.lock().unwrap().take();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Barrier;

    #[tokio::test]
    async fn full_queue_is_an_error() {
        let pool = BlockingPool::new("test-pool", 1, 1);
        let gate = Arc::new(Barrier::new(2));
        let g = gate.clone();
        let first = pool.submit(move || {
            g.wait();
            1
        });
        assert!(first.is_ok());
        // The thread may not have taken the first job yet; fill until Busy.
        let mut busy = false;
        let mut queued = vec![];
        for _ in 0..3 {
            match pool.submit(|| 2) {
                Ok(r) => queued.push(r),
                Err(status) => {
                    assert_eq!(status.code, StatusCode::Busy);
                    busy = true;
                }
            }
        }
        assert!(busy);
        gate.wait();
        assert_eq!(first.unwrap().await.unwrap(), 1);
        for r in queued {
            assert_eq!(r.await.unwrap(), 2);
        }
        assert_eq!(pool.run(|| 5).await.unwrap(), 5);
        pool.shutdown();
        assert_eq!(pool.submit(|| 1).unwrap_err().code, StatusCode::Aborted);
    }
}

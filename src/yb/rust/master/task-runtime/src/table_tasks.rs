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

//! The tserver RPC tasks in flight for each table, as C++ `TableInfo::GetTasks` lists them on the
//! table page.

use std::collections::{BTreeMap, HashMap};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Instant;

/// One task in flight.
#[derive(Debug)]
pub struct TableTask {
    /// `MonitoredTask::type_name`, e.g. "Create Tablet".
    pub type_name: &'static str,
    /// `MonitoredTask::description`.
    pub description: String,
    /// When it started.
    pub started: Instant,
}

/// One table's tasks, by the order they started.
type Tasks = BTreeMap<u64, Arc<TableTask>>;

/// The tasks in flight, by table id.
#[derive(Debug, Default)]
pub struct TableTasks {
    next_id: AtomicU64,
    tasks: Mutex<HashMap<Vec<u8>, Tasks>>,
}

/// Keeps a task listed until dropped.
#[must_use]
pub struct TableTaskGuard {
    registry: Arc<TableTasks>,
    table_id: Vec<u8>,
    id: u64,
}

impl TableTasks {
    /// Lists a task of `table_id` until the guard is dropped.
    pub fn track(
        self: &Arc<Self>,
        table_id: &[u8],
        type_name: &'static str,
        description: String,
    ) -> TableTaskGuard {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        self.tasks
            .lock()
            .unwrap()
            .entry(table_id.to_vec())
            .or_default()
            .insert(
                id,
                Arc::new(TableTask {
                    type_name,
                    description,
                    started: Instant::now(),
                }),
            );
        TableTaskGuard {
            registry: self.clone(),
            table_id: table_id.to_vec(),
            id,
        }
    }

    /// The table's tasks in flight, oldest first.
    pub fn of(&self, table_id: &[u8]) -> Vec<Arc<TableTask>> {
        self.tasks
            .lock()
            .unwrap()
            .get(table_id)
            .map(|t| t.values().cloned().collect())
            .unwrap_or_default()
    }
}

impl Drop for TableTaskGuard {
    fn drop(&mut self) {
        let mut tasks = self.registry.tasks.lock().unwrap();
        if let Some(table) = tasks.get_mut(&self.table_id) {
            table.remove(&self.id);
            if table.is_empty() {
                tasks.remove(&self.table_id);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_task_is_listed_until_its_guard_drops() {
        let tasks = Arc::new(TableTasks::default());
        let first = tasks.track(b"t", "Create Tablet", "one".into());
        let second = tasks.track(b"t", "Delete Tablet", "two".into());
        assert_eq!(
            tasks
                .of(b"t")
                .iter()
                .map(|t| t.description.as_str())
                .collect::<Vec<_>>(),
            ["one", "two"]
        );
        drop(first);
        assert_eq!(tasks.of(b"t").len(), 1);
        drop(second);
        assert!(tasks.of(b"t").is_empty());
        assert!(tasks.tasks.lock().unwrap().is_empty());
    }
}

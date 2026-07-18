/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/context/cursor.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{
    collections::HashMap,
    sync::Arc,
    time::{Duration, Instant},
};

use bson::RawDocumentBuf;
use tokio::{sync::RwLock, task::JoinHandle};

use crate::{configuration::DynamicConfiguration, postgres::Connection};

#[derive(Debug)]
pub struct Cursor {
    pub continuation: RawDocumentBuf,
    pub cursor_id: i64,
}

pub struct CursorStoreEntry {
    pub conn: Option<Arc<Connection>>,
    pub cursor: Cursor,
    pub db: String,
    pub collection: String,
    pub timestamp: Instant,
    pub cursor_timeout: Duration,
    pub session_id: Option<Vec<u8>>,
}

// Maps CursorId, Username -> Connection, Cursor
pub struct CursorStore {
    cursors: Arc<RwLock<HashMap<(i64, String), CursorStoreEntry>>>,
    _reaper: Option<JoinHandle<()>>,
}

impl CursorStore {
    pub fn new(config: Arc<dyn DynamicConfiguration>, use_reaper: bool) -> Self {
        let cursors: Arc<RwLock<HashMap<(i64, String), CursorStoreEntry>>> =
            Arc::new(RwLock::new(HashMap::new()));
        let cursors_clone = cursors.clone();
        let reaper = if use_reaper {
            Some(tokio::spawn(async move {
                let mut cursor_timeout_resolution =
                    Duration::from_secs(config.cursor_resolution_interval().await);
                let mut interval = tokio::time::interval(cursor_timeout_resolution);
                loop {
                    interval.tick().await;
                    let mut cursors = cursors_clone.write().await;
                    cursors.retain(|_, v| v.timestamp.elapsed() < v.cursor_timeout);

                    let new_timeout_interval =
                        Duration::from_secs(config.cursor_resolution_interval().await);
                    if new_timeout_interval != cursor_timeout_resolution {
                        cursor_timeout_resolution = new_timeout_interval;
                        interval = tokio::time::interval(cursor_timeout_resolution);
                    }
                }
            }))
        } else {
            None
        };

        CursorStore {
            cursors,
            _reaper: reaper,
        }
    }

    pub async fn add_cursor(&self, k: (i64, String), v: CursorStoreEntry) {
        let mut cursors = self.cursors.write().await;
        cursors.insert(k, v);
    }

    pub async fn get_cursor(&self, k: (i64, String)) -> Option<CursorStoreEntry> {
        let mut cursors = self.cursors.write().await;
        cursors.remove(&k)
    }

    pub async fn invalidate_cursors_by_collection(&self, db: &str, collection: &str) {
        let mut cursors = self.cursors.write().await;
        cursors.retain(|_, v| !(v.collection == collection && v.db == db))
    }

    pub async fn invalidate_cursors_by_database(&self, db: &str) {
        let mut cursors = self.cursors.write().await;
        cursors.retain(|_, v| v.db != db)
    }

    pub async fn invalidate_cursors_by_session(&self, session: &[u8]) {
        let mut cursors = self.cursors.write().await;
        cursors.retain(|_, v| v.session_id.as_deref() != Some(session))
    }

    pub async fn kill_cursors(&self, user: String, cursors: &[i64]) -> (Vec<i64>, Vec<i64>) {
        let mut removed_cursors = Vec::new();
        let mut missing_cursors = Vec::new();

        let mut cursor_store = self.cursors.write().await;
        for cursor in cursors.iter() {
            if cursor_store.remove(&(*cursor, user.clone())).is_some() {
                removed_cursors.push(*cursor);
            } else {
                missing_cursors.push(*cursor);
            }
        }
        (removed_cursors, missing_cursors)
    }
}

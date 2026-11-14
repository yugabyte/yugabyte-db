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

//! `RunXReplBgTasks` on a Rust leader.

use std::sync::Arc;
use std::time::Duration;

use master_task_runtime::{BoxFuture, Controller, TermScope};

use crate::Xrepl;

/// Runs [`Xrepl`]'s background steps each term.
pub struct XreplController {
    xrepl: Arc<Xrepl>,
}

impl XreplController {
    /// A controller for the term that just began.
    pub fn new(xrepl: Arc<Xrepl>) -> Arc<XreplController> {
        Arc::new(XreplController { xrepl })
    }
}

impl Controller for XreplController {
    fn name(&self) -> &'static str {
        "xrepl"
    }

    fn interval(&self) -> Duration {
        (self.xrepl.options.bg_task_interval)()
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            let xrepl = &self.xrepl;
            let Ok(token) = xrepl.leadership.token() else {
                return;
            };
            if !(xrepl.options.disable_deleted_stream_cleanup)() {
                if let Err(status) = xrepl.clean_up_deleted_streams(&token).await {
                    tracing::warn!("Failed Cleaning Deleted XRepl Streams: {status}");
                }
            }
            if !(xrepl.options.disable_drop_table_cleanup)() {
                if let Err(status) = xrepl.clean_up_cdcsdk_streams_metadata(&token).await {
                    tracing::warn!("Failed Cleanup CDCSDK Streams Metadata: {status}");
                }
            }
        })
    }
}

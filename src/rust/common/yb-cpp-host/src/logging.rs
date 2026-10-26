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

//! Routes Rust `tracing` events and panics to glog, so Rust lines land in `yb-master.INFO` in the
//! same format as C++ lines and a panic dies like a failed `CHECK`.

use std::fmt::Write;
use std::sync::Once;

use tracing::field::{Field, Visit};
use tracing::{Event, Level, Metadata, Subscriber};
use tracing_subscriber::layer::{Context, Layer, SubscriberExt};

use crate::ffi::log::ffi;

// google::LogSeverity values.
const GLOG_INFO: i32 = 0;
const GLOG_WARNING: i32 = 1;
const GLOG_ERROR: i32 = 2;
const GLOG_FATAL: i32 = 3;

/// Maps a tracing level to (glog severity, required `--v` level). DEBUG and TRACE behave like
/// `VLOG(1)` and `VLOG(2)`.
fn severity_for(level: &Level) -> (i32, i32) {
    match *level {
        Level::ERROR => (GLOG_ERROR, 0),
        Level::WARN => (GLOG_WARNING, 0),
        Level::INFO => (GLOG_INFO, 0),
        Level::DEBUG => (GLOG_INFO, 1),
        Level::TRACE => (GLOG_INFO, 2),
    }
}

/// Formats an event's fields as `message k1=v1 k2=v2`.
#[derive(Default)]
struct MessageVisitor {
    message: String,
    fields: String,
}

impl Visit for MessageVisitor {
    fn record_debug(&mut self, field: &Field, value: &dyn std::fmt::Debug) {
        if field.name() == "message" {
            let _ = write!(self.message, "{value:?}");
        } else {
            let _ = write!(self.fields, " {}={value:?}", field.name());
        }
    }

    fn record_str(&mut self, field: &Field, value: &str) {
        if field.name() == "message" {
            self.message.push_str(value);
        } else {
            let _ = write!(self.fields, " {}={value}", field.name());
        }
    }
}

struct GlogLayer;

impl<S: Subscriber> Layer<S> for GlogLayer {
    fn enabled(&self, metadata: &Metadata<'_>, _ctx: Context<'_, S>) -> bool {
        let (_, vlevel) = severity_for(metadata.level());
        vlevel == 0 || ffi::vlog_is_on(vlevel)
    }

    fn on_event(&self, event: &Event<'_>, _ctx: Context<'_, S>) {
        let metadata = event.metadata();
        let (severity, _) = severity_for(metadata.level());
        let mut visitor = MessageVisitor::default();
        event.record(&mut visitor);
        visitor.message.push_str(&visitor.fields);
        ffi::log_message(
            severity,
            metadata.file().unwrap_or("<rust>"),
            metadata.line().unwrap_or(0),
            &visitor.message,
        );
    }
}

/// Installs the glog tracing subscriber and the panic hook. Idempotent; call once flags are parsed
/// and glog is initialized.
pub fn init() {
    static INIT: Once = Once::new();
    INIT.call_once(|| {
        let subscriber = tracing_subscriber::registry().with(GlogLayer);
        tracing::subscriber::set_global_default(subscriber)
            .expect("a global tracing subscriber was already set");
        std::panic::set_hook(Box::new(|info| {
            let (file, line) = info
                .location()
                .map(|l| (l.file(), l.line()))
                .unwrap_or(("<rust>", 0));
            ffi::log_message(GLOG_FATAL, file, line, &format!("Rust panic: {info}"));
        }));
    });
}

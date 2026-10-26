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

//! [`yb_flags::FlagBackend`] on the process's gflags registry.

use std::sync::Mutex;

use cxx::UniquePtr;
use yb_flags::{FlagBackend, FlagInfo};

use crate::ffi::flags::ffi;

/// Receives a watched flag's new values.
pub struct FlagWatcher(Box<dyn Fn(&str) + Send + Sync>);

pub(crate) fn flag_changed(watcher: &FlagWatcher, value: &str) {
    (watcher.0)(value);
}

struct Subscription(#[allow(dead_code)] UniquePtr<ffi::FlagSubscription>);

// SAFETY: the subscription is only dropped, which deregisters the callback under gflags' lock.
unsafe impl Send for Subscription {}

/// The gflags registry of the C++ process.
#[derive(Default)]
pub struct CppFlagBackend {
    // Watches last as long as the process; kept here so they are never deregistered early.
    subscriptions: Mutex<Vec<Subscription>>,
}

impl FlagBackend for CppFlagBackend {
    fn get(&self, name: &str) -> Option<String> {
        ffi::get_flag_value(name).ok()
    }

    fn define(&self, def: &FlagInfo) -> Result<(), String> {
        let tags: Vec<String> = def.tags.iter().map(|t| t.to_string()).collect();
        ffi::define_flag(
            def.type_name,
            def.name,
            &def.default_value,
            def.help,
            def.file,
            &tags,
        )
        .map_err(|e| e.what().to_string())
    }

    fn watch(&self, name: &str, on_change: Box<dyn Fn(&str) + Send + Sync>) -> Result<(), String> {
        let subscription = ffi::watch_flag(name, Box::new(FlagWatcher(on_change)))
            .map_err(|e| e.what().to_string())?;
        self.subscriptions
            .lock()
            .unwrap()
            .push(Subscription(subscription));
        Ok(())
    }
}

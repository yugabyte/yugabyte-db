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

//! gflags access (C++ side: src/yb/rust_bridge/flags_bridge.h).

use crate::flags::{flag_changed, FlagWatcher};

/// Bridge declarations; use [`crate::flags::CppFlagBackend`] rather than calling these directly.
#[cxx::bridge(namespace = "yb::rust_bridge")]
pub mod ffi {
    extern "Rust" {
        type FlagWatcher;

        /// Called with the flag's new value in gflags' string form.
        fn flag_changed(watcher: &FlagWatcher, value: &str);
    }

    unsafe extern "C++" {
        include!("yb/rust_bridge/flags_bridge.h");

        /// Keeps a flag watch registered while alive.
        type FlagSubscription;

        /// The flag's current value; fails if there is no such flag.
        #[cxx_name = "GetFlagValue"]
        fn get_flag_value(name: &str) -> Result<String>;

        /// Registers a new flag with gflags and the yb tag registry.
        #[cxx_name = "DefineFlag"]
        fn define_flag(
            type_name: &str,
            name: &str,
            default_value: &str,
            help: &str,
            file: &str,
            tags: &Vec<String>,
        ) -> Result<()>;

        /// Calls `flag_changed` on every change of the flag, and once now.
        #[cxx_name = "WatchFlag"]
        fn watch_flag(name: &str, watcher: Box<FlagWatcher>)
            -> Result<UniquePtr<FlagSubscription>>;
    }
}

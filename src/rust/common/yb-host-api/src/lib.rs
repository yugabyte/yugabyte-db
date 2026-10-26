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

//! Interfaces a C++ server process provides to the Rust code it hosts.
//!
//! Every crate except `yb-cpp-host` and the static-library crates depends on these traits rather
//! than on the cxx bridges, so `cargo test -p <crate>` runs without linking any C++. The in-memory fakes
//! next to each trait are what those tests use.

pub mod rpc;
pub mod status;

pub use status::{Status, StatusCode};

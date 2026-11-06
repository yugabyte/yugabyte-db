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

//! Fake tablet servers for loading a master: many tservers in one process, each registering,
//! heartbeating, sending tablet reports and answering the master's tablet RPCs from in-memory
//! state, with no storage behind them. The `master-tserver-sim` executable
//! (`master-tserver-sim-main`) runs them on the C++ RPC bridge; tests run them on fakes.

mod service;
mod stats;
mod tserver;
mod workload;

pub use service::{SimCluster, SimService};
pub use stats::Latencies;
pub use tserver::{SentReport, SimTserver};
pub use workload::{run, Report, WorkloadConfig};

#[cfg(test)]
mod tests;

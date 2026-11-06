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

//! The metrics bridge (C++ side: src/yb/rust_bridge/metrics_bridge.h).

/// Bridge declarations; use [`crate::metrics::CppMetricRegistry`] rather than calling these
/// directly.
#[cxx::bridge(namespace = "yb::rust_bridge")]
pub mod ffi {
    unsafe extern "C++" {
        include!("yb/rust_bridge/metrics_bridge.h");

        /// The host's RPC server, whose metric entity the registry uses.
        #[namespace = "yb::rust_bridge"]
        type RpcServerHandle = crate::ffi::rpc::ffi::RpcServerHandle;

        /// Creates metrics in one metric entity.
        type RustMetricRegistry;
        /// A counter in the host's registry.
        type RustCounter;
        /// An int64 gauge in the host's registry.
        type RustGauge;
        /// A histogram in the host's registry.
        type RustHistogram;

        /// The registry of the server's metric entity.
        #[cxx_name = "MakeMetricRegistry"]
        fn make_metric_registry(server: &RpcServerHandle) -> UniquePtr<RustMetricRegistry>;

        /// A counter; fails on an unknown unit or a name used by another kind of metric.
        #[cxx_name = "AddCounter"]
        fn add_counter(
            self: &RustMetricRegistry,
            name: &str,
            label: &str,
            description: &str,
            unit: &str,
        ) -> Result<UniquePtr<RustCounter>>;
        /// A gauge; fails as `add_counter` does.
        #[cxx_name = "AddGauge"]
        fn add_gauge(
            self: &RustMetricRegistry,
            name: &str,
            label: &str,
            description: &str,
            unit: &str,
        ) -> Result<UniquePtr<RustGauge>>;
        /// A histogram of values up to `max_value`; fails as `add_counter` does.
        #[cxx_name = "AddHistogram"]
        fn add_histogram(
            self: &RustMetricRegistry,
            name: &str,
            label: &str,
            description: &str,
            unit: &str,
            max_value: u64,
        ) -> Result<UniquePtr<RustHistogram>>;

        /// Adds `amount` to the counter.
        #[cxx_name = "IncrementBy"]
        fn increment_by(self: &RustCounter, amount: u64);
        /// Sets the gauge.
        #[cxx_name = "Set"]
        fn set(self: &RustGauge, value: i64);
        /// Records one value.
        #[cxx_name = "Record"]
        fn record(self: &RustHistogram, value: u64);
    }
}

// SAFETY: the C++ metric objects are internally synchronized (atomics and the histogram's own
// locking), and the registry only reads its entity pointer and takes the prototype mutex.
unsafe impl Send for ffi::RustMetricRegistry {}
// SAFETY: as above.
unsafe impl Sync for ffi::RustMetricRegistry {}
// SAFETY: as above.
unsafe impl Send for ffi::RustCounter {}
// SAFETY: as above.
unsafe impl Sync for ffi::RustCounter {}
// SAFETY: as above.
unsafe impl Send for ffi::RustGauge {}
// SAFETY: as above.
unsafe impl Sync for ffi::RustGauge {}
// SAFETY: as above.
unsafe impl Send for ffi::RustHistogram {}
// SAFETY: as above.
unsafe impl Sync for ffi::RustHistogram {}

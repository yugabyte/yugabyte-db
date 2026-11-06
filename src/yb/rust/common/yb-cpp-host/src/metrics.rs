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

//! [`MetricRegistry`] over the host's C++ metric registry.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use cxx::UniquePtr;
use yb_host_api::metrics::{Counter, Gauge, Histogram, MetricDef, MetricRegistry};

use crate::ffi::metrics::ffi;
use crate::rpc::RpcServerHandle;

/// Metrics in the server's metric entity.
pub struct CppMetricRegistry {
    registry: UniquePtr<ffi::RustMetricRegistry>,
    /// Metrics handed out, so a second request for a name returns the same one without a
    /// prototype lookup.
    created: Mutex<HashMap<&'static str, Created>>,
}

#[derive(Clone)]
enum Created {
    Counter(Arc<CppCounter>),
    Gauge(Arc<CppGauge>),
    Histogram(Arc<CppHistogram>),
}

impl CppMetricRegistry {
    /// The registry of `server`'s metric entity.
    pub fn new(server: &RpcServerHandle) -> Arc<CppMetricRegistry> {
        Arc::new(CppMetricRegistry {
            registry: ffi::make_metric_registry(server),
            created: Mutex::default(),
        })
    }

    fn get_or_create(&self, def: &MetricDef, create: impl FnOnce() -> Created) -> Created {
        let mut created = self.created.lock().unwrap();
        created.entry(def.name).or_insert_with(create).clone()
    }
}

/// Metric definitions are code, so a bad one is a bug, as a bad METRIC_DEFINE_* is in C++.
fn defined<T>(def: &MetricDef, result: Result<UniquePtr<T>, cxx::Exception>) -> UniquePtr<T>
where
    T: cxx::memory::UniquePtrTarget,
{
    result.unwrap_or_else(|e| panic!("Defining metric {}: {e}", def.name))
}

impl MetricRegistry for CppMetricRegistry {
    fn counter(&self, def: &MetricDef) -> Arc<dyn Counter> {
        let created = self.get_or_create(def, || {
            let counter =
                self.registry
                    .add_counter(def.name, def.label, def.description, def.unit.as_str());
            Created::Counter(Arc::new(CppCounter(defined(def, counter))))
        });
        match created {
            Created::Counter(c) => c,
            _ => panic!("Metric {} is already defined as another kind", def.name),
        }
    }

    fn gauge(&self, def: &MetricDef) -> Arc<dyn Gauge> {
        let created = self.get_or_create(def, || {
            let gauge =
                self.registry
                    .add_gauge(def.name, def.label, def.description, def.unit.as_str());
            Created::Gauge(Arc::new(CppGauge(defined(def, gauge))))
        });
        match created {
            Created::Gauge(g) => g,
            _ => panic!("Metric {} is already defined as another kind", def.name),
        }
    }

    fn histogram(&self, def: &MetricDef, max_value: u64) -> Arc<dyn Histogram> {
        let created = self.get_or_create(def, || {
            let histogram = self.registry.add_histogram(
                def.name,
                def.label,
                def.description,
                def.unit.as_str(),
                max_value,
            );
            Created::Histogram(Arc::new(CppHistogram(defined(def, histogram))))
        });
        match created {
            Created::Histogram(h) => h,
            _ => panic!("Metric {} is already defined as another kind", def.name),
        }
    }
}

struct CppCounter(UniquePtr<ffi::RustCounter>);

impl Counter for CppCounter {
    fn increment_by(&self, n: u64) {
        self.0.increment_by(n);
    }
}

struct CppGauge(UniquePtr<ffi::RustGauge>);

impl Gauge for CppGauge {
    fn set(&self, value: i64) {
        self.0.set(value);
    }
}

struct CppHistogram(UniquePtr<ffi::RustHistogram>);

impl Histogram for CppHistogram {
    fn record(&self, value: u64) {
        self.0.record(value);
    }
}

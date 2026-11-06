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

//! Metrics in the host's metric registry, so they appear in `/metrics` and `/prometheus-metrics`
//! with the C++ metrics of the same server entity.

use std::collections::HashMap;
use std::sync::atomic::{AtomicI64, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

/// The unit a metric is reported in; the host's `MetricUnit`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MetricUnit {
    /// `kMicroseconds`.
    Microseconds,
    /// `kOperations`.
    Operations,
    /// `kEntries`.
    Entries,
    /// `kRequests`.
    Requests,
    /// `kBytes`.
    Bytes,
    /// `kUnits`.
    Units,
}

impl MetricUnit {
    /// The name the host's bridge maps to its unit.
    pub fn as_str(self) -> &'static str {
        match self {
            MetricUnit::Microseconds => "microseconds",
            MetricUnit::Operations => "operations",
            MetricUnit::Entries => "entries",
            MetricUnit::Requests => "requests",
            MetricUnit::Bytes => "bytes",
            MetricUnit::Units => "units",
        }
    }
}

/// A metric's definition: its name in the server entity, label, description and unit.
#[derive(Clone, Copy, Debug)]
pub struct MetricDef {
    /// The exported name, e.g. `rust_master_catalog_writer_queue_wait_us`.
    pub name: &'static str,
    /// A short human-readable label.
    pub label: &'static str,
    /// What it measures.
    pub description: &'static str,
    /// Its unit.
    pub unit: MetricUnit,
}

/// A monotonically increasing count.
pub trait Counter: Send + Sync {
    /// Adds `n`.
    fn increment_by(&self, n: u64);
    /// Adds one.
    fn increment(&self) {
        self.increment_by(1);
    }
}

/// A value that goes up and down.
pub trait Gauge: Send + Sync {
    /// Sets the value.
    fn set(&self, value: i64);
}

/// A distribution of values, exported with percentiles.
pub trait Histogram: Send + Sync {
    /// Records one value.
    fn record(&self, value: u64);
    /// Records a duration in microseconds.
    fn record_duration(&self, duration: Duration) {
        self.record(duration.as_micros().min(u64::MAX as u128) as u64);
    }
}

/// Creates metrics in one metric entity. Creating a metric twice by name returns the same metric.
pub trait MetricRegistry: Send + Sync {
    /// A counter.
    fn counter(&self, def: &MetricDef) -> Arc<dyn Counter>;
    /// A gauge.
    fn gauge(&self, def: &MetricDef) -> Arc<dyn Gauge>;
    /// A histogram of values up to `max_value`.
    fn histogram(&self, def: &MetricDef, max_value: u64) -> Arc<dyn Histogram>;
}

/// An in-memory registry for tests: every metric keeps its values for inspection.
#[derive(Default)]
pub struct InMemoryMetrics {
    counters: Mutex<HashMap<&'static str, Arc<InMemoryCounter>>>,
    gauges: Mutex<HashMap<&'static str, Arc<InMemoryGauge>>>,
    histograms: Mutex<HashMap<&'static str, Arc<InMemoryHistogram>>>,
}

impl InMemoryMetrics {
    /// A new, empty registry.
    pub fn new() -> Arc<InMemoryMetrics> {
        Arc::default()
    }

    /// A counter's value; 0 if it was never created.
    pub fn counter_value(&self, name: &str) -> u64 {
        self.counters
            .lock()
            .unwrap()
            .get(name)
            .map_or(0, |c| c.0.load(Ordering::Relaxed))
    }

    /// A gauge's value; 0 if it was never created.
    pub fn gauge_value(&self, name: &str) -> i64 {
        self.gauges
            .lock()
            .unwrap()
            .get(name)
            .map_or(0, |g| g.0.load(Ordering::Relaxed))
    }

    /// The values recorded in a histogram, in order; empty if it was never created.
    pub fn histogram_values(&self, name: &str) -> Vec<u64> {
        self.histograms
            .lock()
            .unwrap()
            .get(name)
            .map_or_else(Vec::new, |h| h.0.lock().unwrap().clone())
    }
}

impl MetricRegistry for InMemoryMetrics {
    fn counter(&self, def: &MetricDef) -> Arc<dyn Counter> {
        self.counters
            .lock()
            .unwrap()
            .entry(def.name)
            .or_default()
            .clone()
    }

    fn gauge(&self, def: &MetricDef) -> Arc<dyn Gauge> {
        self.gauges
            .lock()
            .unwrap()
            .entry(def.name)
            .or_default()
            .clone()
    }

    fn histogram(&self, def: &MetricDef, _max_value: u64) -> Arc<dyn Histogram> {
        self.histograms
            .lock()
            .unwrap()
            .entry(def.name)
            .or_default()
            .clone()
    }
}

#[derive(Default)]
struct InMemoryCounter(AtomicU64);

impl Counter for InMemoryCounter {
    fn increment_by(&self, n: u64) {
        self.0.fetch_add(n, Ordering::Relaxed);
    }
}

#[derive(Default)]
struct InMemoryGauge(AtomicI64);

impl Gauge for InMemoryGauge {
    fn set(&self, value: i64) {
        self.0.store(value, Ordering::Relaxed);
    }
}

#[derive(Default)]
struct InMemoryHistogram(Mutex<Vec<u64>>);

impl Histogram for InMemoryHistogram {
    fn record(&self, value: u64) {
        self.0.lock().unwrap().push(value);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const DEF: MetricDef = MetricDef {
        name: "m",
        label: "M",
        description: "A test metric",
        unit: MetricUnit::Units,
    };

    #[test]
    fn same_name_is_same_metric() {
        let metrics = InMemoryMetrics::new();
        metrics.counter(&DEF).increment();
        metrics.counter(&DEF).increment_by(2);
        assert_eq!(metrics.counter_value("m"), 3);
        metrics.gauge(&DEF).set(-4);
        assert_eq!(metrics.gauge_value("m"), -4);
        metrics
            .histogram(&DEF, 10)
            .record_duration(Duration::from_millis(2));
        assert_eq!(metrics.histogram_values("m"), vec![2000]);
    }
}

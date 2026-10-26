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

//! Latency samples.

use std::time::Duration;

/// Every sample of one measurement.
#[derive(Debug, Default)]
pub struct Latencies {
    samples: Vec<Duration>,
}

impl Latencies {
    /// Adds a sample.
    pub fn record(&mut self, sample: Duration) {
        self.samples.push(sample);
    }

    /// Number of samples.
    pub fn count(&self) -> usize {
        self.samples.len()
    }

    /// The nearest-rank percentile `p` (0 to 100), or zero without samples.
    pub fn percentile(&self, p: f64) -> Duration {
        if self.samples.is_empty() {
            return Duration::ZERO;
        }
        let mut sorted = self.samples.clone();
        sorted.sort();
        let rank = ((p / 100.0) * sorted.len() as f64).ceil() as usize;
        sorted[rank.clamp(1, sorted.len()) - 1]
    }

    /// `{"count", "p50", "p99", "max"}`, durations in seconds times `scale`.
    pub fn to_json(&self, scale: f64) -> String {
        let value = |d: Duration| d.as_secs_f64() * scale;
        format!(
            "{{\"count\": {}, \"p50\": {:.1}, \"p99\": {:.1}, \"max\": {:.1}}}",
            self.count(),
            value(self.percentile(50.0)),
            value(self.percentile(99.0)),
            value(self.percentile(100.0)),
        )
    }
}

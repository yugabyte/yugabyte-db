//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

pub mod pgrx;

use crate::pgrx::{
    BenchArtifact, BenchComparison, BenchComparisonEstimate, BenchConfig, BenchDefinition,
    BenchEstimate, BenchResult, BenchSample, BenchStatus, BenchThroughput, CriterionBenchmark,
    Runtime, TransactionMode,
};
use criterion::{Criterion, measurement::WallTime};
use oorandom::Rand64;
use serde::Deserialize;
use serde_json::Value;
use std::any::Any;
use std::cell::RefCell;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

/// Re-export of `std::hint::black_box`, which helps keep the optimizer from removing the work
/// you intend to measure.
///
/// ```ignore
/// use pgrx::prelude::*;
/// use pgrx_bench::{Bencher, black_box};
///
/// #[pg_bench]
/// fn bench_parse_uuid(b: &mut Bencher) {
///     let input = "550e8400-e29b-41d4-a716-446655440000";
///     b.iter(|| crate::parse_uuid(black_box(input)));
/// }
/// ```
pub use std::hint::black_box;

/// Re-export of Criterion's batching strategy enum for `Bencher::iter_batched`.
///
/// ```ignore
/// use pgrx_bench::{BatchSize, Bencher};
///
/// #[pg_bench]
/// fn bench_transform_rows(b: &mut Bencher) {
///     b.iter_batched(
///         || (0..100).collect::<Vec<i32>>(),
///         |rows| rows.into_iter().map(|value| value * 2).collect::<Vec<_>>(),
///         BatchSize::SmallInput,
///     );
/// }
/// ```
pub use criterion::BatchSize;

const DEFAULT_SAMPLE_SIZE: usize = 100;
const DEFAULT_MEASUREMENT_TIME_MS: u64 = 5_000;
const DEFAULT_WARM_UP_TIME_MS: u64 = 3_000;
const DEFAULT_NRESAMPLES: usize = 100_000;
const DEFAULT_NOISE_THRESHOLD: f64 = 0.01;
const DEFAULT_SIGNIFICANCE_LEVEL: f64 = 0.05;

const ARTIFACT_KIND_BENCHMARK_JSON: &str = "criterion_benchmark_json";
const ARTIFACT_KIND_ESTIMATES_JSON: &str = "criterion_estimates_json";
const ARTIFACT_KIND_SAMPLE_JSON: &str = "criterion_sample_json";
const ARTIFACT_KIND_TUKEY_JSON: &str = "criterion_tukey_json";
const ARTIFACT_KIND_CHANGE_ESTIMATES_JSON: &str = "criterion_change_estimates_json";

/// Timing harness passed to a `#[pg_bench]` benchmark function.
///
/// Bench functions register exactly one timing loop with either `Bencher::iter` or
/// `Bencher::iter_batched`.
///
/// ```ignore
/// use pgrx_bench::{Bencher, black_box};
///
/// #[pg_bench]
/// fn bench_normalize_phrase(b: &mut Bencher) {
///     let phrase = "the quick brown fox";
///     b.iter(|| crate::normalize_phrase(black_box(phrase)));
/// }
/// ```
pub struct Bencher<'a> {
    routine: Option<Routine<'a>>,
}

enum Routine<'a> {
    Iter(Box<dyn FnMut() + 'a>),
    IterBatched {
        setup: Box<dyn FnMut() -> Box<dyn Any> + 'a>,
        routine: Box<dyn FnMut(Box<dyn Any>) + 'a>,
        batch_size: BatchSize,
    },
}

impl<'a> Bencher<'a> {
    #[doc(hidden)]
    /// Internal constructor used by `#[pg_bench]` wrappers.
    pub fn new(transaction_mode: TransactionMode) -> Self {
        let _ = transaction_mode;
        Self { routine: None }
    }

    /// Registers a simple timing loop for a benchmark.
    ///
    /// Use this when the benchmark body can reuse the same captured inputs for each iteration.
    ///
    /// ```ignore
    /// use pgrx_bench::{Bencher, black_box};
    ///
    /// #[pg_bench]
    /// fn bench_parse_uuid(b: &mut Bencher) {
    ///     let input = "550e8400-e29b-41d4-a716-446655440000";
    ///     b.iter(|| crate::parse_uuid(black_box(input)));
    /// }
    /// ```
    pub fn iter<R, F>(&mut self, mut routine: F)
    where
        F: FnMut() -> R + 'a,
    {
        self.set_routine(Routine::Iter(Box::new(move || {
            let _ = routine();
        })));
    }

    /// Registers a timing loop that performs per-batch setup outside the measured routine body.
    ///
    /// Use this when each timing sample needs fresh inputs or temporary state.
    ///
    /// ```ignore
    /// use pgrx_bench::{BatchSize, Bencher};
    ///
    /// #[pg_bench]
    /// fn bench_transform_rows(b: &mut Bencher) {
    ///     b.iter_batched(
    ///         || (0..100).collect::<Vec<i32>>(),
    ///         |rows| rows.into_iter().map(|value| value * 2).collect::<Vec<_>>(),
    ///         BatchSize::SmallInput,
    ///     );
    /// }
    /// ```
    pub fn iter_batched<I, R, S, F>(&mut self, mut setup: S, mut routine: F, batch_size: BatchSize)
    where
        I: 'static,
        S: FnMut() -> I + 'a,
        F: FnMut(I) -> R + 'a,
    {
        self.set_routine(Routine::IterBatched {
            setup: Box::new(move || Box::new(setup()) as Box<dyn Any>),
            routine: Box::new(move |input| {
                let input = *input
                    .downcast::<I>()
                    .expect("pgrx_bench internal type mismatch for iter_batched input");
                let _ = routine(input);
            }),
            batch_size,
        });
    }

    fn set_routine(&mut self, routine: Routine<'a>) {
        if self.routine.is_some() {
            panic!("only one bencher timing loop may be declared per #[pg_bench] function");
        }
        self.routine = Some(routine);
    }

    fn into_routine(self) -> Result<Routine<'a>, String> {
        self.routine.ok_or_else(|| {
            "benchmark function did not register a timing loop; call b.iter(...) or b.iter_batched(...)"
                .to_string()
        })
    }
}

fn build_criterion(
    config: &BenchConfig,
    output_directory: &Path,
    has_baseline: bool,
) -> Criterion<WallTime> {
    let criterion = Criterion::default()
        .without_plots()
        .output_directory(output_directory)
        .sample_size(config.sample_size)
        .measurement_time(Duration::from_millis(config.measurement_time_ms))
        .warm_up_time(Duration::from_millis(config.warm_up_time_ms))
        .nresamples(config.nresamples)
        .noise_threshold(config.noise_threshold)
        .significance_level(config.significance_level);

    if has_baseline {
        criterion.retain_baseline("base".to_string(), false)
    } else {
        criterion.save_baseline("base".to_string())
    }
}

fn run_routine<R: Runtime>(
    criterion_bencher: &mut criterion::Bencher<'_, WallTime>,
    routine: &mut Routine<'_>,
    transaction_mode: TransactionMode,
    runtime: &R,
) {
    match routine {
        Routine::Iter(routine) => match transaction_mode {
            TransactionMode::Shared => criterion_bencher.iter(routine),
            TransactionMode::SubtransactionPerBatch
            | TransactionMode::SubtransactionPerIteration => {
                criterion_bencher.iter_custom(|iters| {
                    let started = Instant::now();
                    for _ in 0..iters {
                        runtime
                            .with_subtransaction(|| routine())
                            .unwrap_or_else(|error| panic!("{error}"));
                    }
                    started.elapsed()
                });
            }
        },
        Routine::IterBatched { setup, routine, batch_size } => {
            criterion_bencher.iter_custom(|iters| {
                let started = Instant::now();
                let mut remaining = iters;
                let per_batch = iterations_per_batch(*batch_size, iters).max(1);

                while remaining > 0 {
                    let current_batch = remaining.min(per_batch);
                    match transaction_mode {
                        TransactionMode::Shared => {
                            for _ in 0..current_batch {
                                let input = setup();
                                routine(input);
                            }
                        }
                        TransactionMode::SubtransactionPerBatch => {
                            runtime
                                .with_subtransaction(|| {
                                    for _ in 0..current_batch {
                                        let input = setup();
                                        routine(input);
                                    }
                                })
                                .unwrap_or_else(|error| panic!("{error}"));
                        }
                        TransactionMode::SubtransactionPerIteration => {
                            for _ in 0..current_batch {
                                runtime
                                    .with_subtransaction(|| {
                                        let input = setup();
                                        routine(input);
                                    })
                                    .unwrap_or_else(|error| panic!("{error}"));
                            }
                        }
                    }
                    remaining -= current_batch;
                }

                started.elapsed()
            });
        }
    }
}

fn iterations_per_batch(batch_size: BatchSize, iters: u64) -> u64 {
    match batch_size {
        BatchSize::SmallInput => (iters + 10 - 1) / 10,
        BatchSize::LargeInput => (iters + 1000 - 1) / 1000,
        BatchSize::PerIteration => 1,
        BatchSize::NumBatches(batches) => (iters + batches - 1) / batches,
        BatchSize::NumIterations(size) => size,
        BatchSize::__NonExhaustive => panic!("invalid BatchSize"),
    }
}

fn parse_benchmark_output(
    definition: BenchDefinition,
    root: &Path,
    baseline_artifacts: Option<&[BenchArtifact]>,
) -> Result<BenchResult, String> {
    let report_dir = find_new_report_dir(root)
        .ok_or_else(|| "criterion did not emit benchmark output".to_string())?;
    let benchmark_path = report_dir.join("benchmark.json");
    let estimates_path = report_dir.join("estimates.json");
    let sample_path = report_dir.join("sample.json");

    let benchmark_json = read_json_value(&benchmark_path)?;
    let estimates_json = read_json_value(&estimates_path)?;
    let sample_json = read_json_value(&sample_path)?;

    let benchmark = serde_json::from_value::<CriterionBenchmarkJson>(benchmark_json.clone())
        .map_err(|e| format!("failed to parse {}: {e}", benchmark_path.display()))?;
    let estimates = serde_json::from_value::<CriterionEstimatesJson>(estimates_json.clone())
        .map_err(|e| format!("failed to parse {}: {e}", estimates_path.display()))?;
    let samples = serde_json::from_value::<CriterionSampleJson>(sample_json.clone())
        .map_err(|e| format!("failed to parse {}: {e}", sample_path.display()))?;
    let comparison = parse_comparison(
        report_dir.parent().expect("criterion report dir should always have a parent"),
        baseline_artifacts,
        &samples,
        &definition.config,
    )?;
    let artifacts = collect_artifacts(
        report_dir.parent().expect("criterion report dir should always have a parent"),
        &benchmark_json,
        &estimates_json,
        &sample_json,
    )?;

    Ok(BenchResult {
        schema_name: definition.schema_name.to_string(),
        bench_name: definition.bench_name.to_string(),
        function_name: definition.function_name.to_string(),
        setup_function: definition.setup_function.map(str::to_string),
        transaction_mode: definition.transaction_mode,
        source_file: definition.source_file.to_string(),
        source_line: definition.source_line,
        criterion_config: definition.config,
        status: BenchStatus::Ok,
        error_text: None,
        benchmark: Some(CriterionBenchmark {
            group_id: benchmark.group_id,
            function_id: benchmark.function_id,
            value_str: benchmark.value_str,
            full_id: benchmark.full_id,
            directory_name: benchmark.directory_name,
            title: benchmark.title,
        }),
        estimates: estimates.into_estimates(),
        samples: samples.into_samples()?,
        throughput: benchmark.throughput.and_then(parse_throughput),
        comparison,
        artifacts,
    })
}

fn read_json_file<T>(path: &Path) -> Result<T, String>
where
    T: for<'de> Deserialize<'de>,
{
    let raw =
        fs::read_to_string(path).map_err(|e| format!("failed to read {}: {e}", path.display()))?;
    serde_json::from_str(&raw).map_err(|e| format!("failed to parse {}: {e}", path.display()))
}

fn read_json_value(path: &Path) -> Result<Value, String> {
    read_json_file(path)
}

fn write_json_value(path: &Path, value: &Value) -> Result<(), String> {
    let raw = serde_json::to_vec_pretty(value)
        .map_err(|error| format!("failed to serialize {}: {error}", path.display()))?;
    fs::write(path, raw).map_err(|error| format!("failed to write {}: {error}", path.display()))
}

fn collect_artifacts(
    benchmark_root: &Path,
    benchmark_json: &Value,
    estimates_json: &Value,
    sample_json: &Value,
) -> Result<Vec<BenchArtifact>, String> {
    let mut artifacts = Vec::new();
    push_json_artifact(&mut artifacts, ARTIFACT_KIND_BENCHMARK_JSON, benchmark_json.clone());
    push_json_artifact(&mut artifacts, ARTIFACT_KIND_ESTIMATES_JSON, estimates_json.clone());
    push_json_artifact(&mut artifacts, ARTIFACT_KIND_SAMPLE_JSON, sample_json.clone());

    let tukey_path = benchmark_root.join("new").join("tukey.json");
    if tukey_path.exists() {
        push_json_artifact(&mut artifacts, ARTIFACT_KIND_TUKEY_JSON, read_json_value(&tukey_path)?);
    }

    let change_estimates_path = benchmark_root.join("change").join("estimates.json");
    if change_estimates_path.exists() {
        push_json_artifact(
            &mut artifacts,
            ARTIFACT_KIND_CHANGE_ESTIMATES_JSON,
            read_json_value(&change_estimates_path)?,
        );
    }

    Ok(artifacts)
}

fn push_json_artifact(
    artifacts: &mut Vec<BenchArtifact>,
    artifact_kind: &str,
    payload_json: Value,
) {
    artifacts.push(BenchArtifact {
        artifact_kind: artifact_kind.to_string(),
        media_type: "application/json".to_string(),
        payload_json,
    });
}

fn materialize_baseline_artifacts(
    output_directory: &Path,
    baseline_artifacts: &[BenchArtifact],
) -> Result<(), String> {
    let benchmark_json = find_artifact(baseline_artifacts, ARTIFACT_KIND_BENCHMARK_JSON)
        .ok_or_else(|| "persisted Criterion baseline is missing benchmark.json".to_string())?;
    let estimates_json = find_artifact(baseline_artifacts, ARTIFACT_KIND_ESTIMATES_JSON)
        .ok_or_else(|| "persisted Criterion baseline is missing estimates.json".to_string())?;
    let sample_json = find_artifact(baseline_artifacts, ARTIFACT_KIND_SAMPLE_JSON)
        .ok_or_else(|| "persisted Criterion baseline is missing sample.json".to_string())?;

    let directory_name = baseline_directory_name(benchmark_json)?;
    let baseline_dir = output_directory.join(directory_name).join("base");
    fs::create_dir_all(&baseline_dir)
        .map_err(|error| format!("failed to create {}: {error}", baseline_dir.display()))?;

    write_json_value(&baseline_dir.join("benchmark.json"), benchmark_json)?;
    write_json_value(&baseline_dir.join("estimates.json"), estimates_json)?;
    write_json_value(&baseline_dir.join("sample.json"), sample_json)?;

    if let Some(tukey_json) = find_artifact(baseline_artifacts, ARTIFACT_KIND_TUKEY_JSON) {
        write_json_value(&baseline_dir.join("tukey.json"), tukey_json)?;
    }

    Ok(())
}

fn find_artifact<'a>(artifacts: &'a [BenchArtifact], artifact_kind: &str) -> Option<&'a Value> {
    artifacts
        .iter()
        .find(|artifact| artifact.artifact_kind == artifact_kind)
        .map(|artifact| &artifact.payload_json)
}

fn baseline_directory_name(benchmark_json: &Value) -> Result<String, String> {
    let benchmark = serde_json::from_value::<CriterionBenchmarkJson>(benchmark_json.clone())
        .map_err(|error| format!("failed to parse persisted benchmark.json: {error}"))?;
    Ok(benchmark.directory_name)
}

fn parse_comparison(
    benchmark_root: &Path,
    baseline_artifacts: Option<&[BenchArtifact]>,
    current_samples: &CriterionSampleJson,
    config: &BenchConfig,
) -> Result<Option<BenchComparison>, String> {
    let Some(baseline_artifacts) = baseline_artifacts else {
        return Ok(None);
    };

    let change_estimates_path = benchmark_root.join("change").join("estimates.json");
    if !change_estimates_path.exists() {
        return Ok(None);
    }

    let change_estimates_json = read_json_value(&change_estimates_path)?;
    let change_estimates = serde_json::from_value::<CriterionChangeEstimatesJson>(
        change_estimates_json,
    )
    .map_err(|error| format!("failed to parse {}: {error}", change_estimates_path.display()))?;
    let baseline_sample_json = find_artifact(baseline_artifacts, ARTIFACT_KIND_SAMPLE_JSON)
        .ok_or_else(|| "persisted Criterion baseline is missing sample.json".to_string())?;
    let baseline_samples =
        serde_json::from_value::<CriterionSampleJson>(baseline_sample_json.clone())
            .map_err(|error| format!("failed to parse persisted sample.json: {error}"))?;

    let p_value = criterion_p_value(
        &current_samples.avg_times()?,
        &baseline_samples.avg_times()?,
        config.nresamples,
    )?;
    let summary = criterion_summary_from_change_estimate(
        &change_estimates.mean,
        p_value,
        config.significance_level,
        config.noise_threshold,
    );

    Ok(Some(BenchComparison {
        mean: change_estimates.mean.into_relative_estimate("mean"),
        median: change_estimates.median.into_relative_estimate("median"),
        p_value,
        significance_level: config.significance_level,
        noise_threshold: config.noise_threshold,
        summary,
    }))
}

fn criterion_summary_from_change_estimate(
    estimate: &CriterionEstimateJson,
    p_value: f64,
    significance_level: f64,
    noise_threshold: f64,
) -> String {
    // Match Criterion's reporting rule: significance is based on the bootstrap T distribution,
    // then the final label is chosen from the relative mean estimate's confidence interval.
    if p_value >= significance_level {
        return "No change in performance detected.".to_string();
    }

    let lower_bound = estimate.confidence_interval.lower_bound;
    let upper_bound = estimate.confidence_interval.upper_bound;

    if lower_bound < -noise_threshold && upper_bound < -noise_threshold {
        "Performance has improved.".to_string()
    } else if lower_bound > noise_threshold && upper_bound > noise_threshold {
        "Performance has regressed.".to_string()
    } else {
        "Change within noise threshold.".to_string()
    }
}

fn find_new_report_dir(root: &Path) -> Option<PathBuf> {
    let mut stack = vec![root.to_path_buf()];
    while let Some(path) = stack.pop() {
        let Ok(entries) = fs::read_dir(&path) else {
            continue;
        };

        let mut files = Vec::new();
        for entry in entries.flatten() {
            let entry_path = entry.path();
            if entry_path.is_dir() {
                stack.push(entry_path);
            } else {
                files.push(entry.file_name());
            }
        }

        // Criterion writes both `base/` and `new/` directories with the same core files. For the
        // current benchmark result we must read `new/`, otherwise comparisons end up mixing the
        // baseline's absolute estimates with the current run's relative change output.
        let is_new_dir = path.file_name().and_then(|name| name.to_str()) == Some("new");
        let has_benchmark = files.iter().any(|name| name == "benchmark.json");
        let has_estimates = files.iter().any(|name| name == "estimates.json");
        let has_samples = files.iter().any(|name| name == "sample.json");
        if is_new_dir && has_benchmark && has_estimates && has_samples {
            return Some(path);
        }
    }

    None
}

fn parse_throughput(value: Value) -> Option<BenchThroughput> {
    let object = value.as_object()?;
    let (kind, value) = object.iter().next()?;
    value.as_f64().map(|value| BenchThroughput { kind: kind.to_lowercase(), value })
}

const fn ends_with(value: &[u8], suffix: &[u8]) -> bool {
    if suffix.len() > value.len() {
        return false;
    }

    let offset = value.len() - suffix.len();
    let mut index = 0;
    while index < suffix.len() {
        if value[offset + index] != suffix[index] {
            return false;
        }
        index += 1;
    }
    true
}

const fn equals(left: &[u8], right: &[u8]) -> bool {
    if left.len() != right.len() {
        return false;
    }

    let mut index = 0;
    while index < left.len() {
        if left[index] != right[index] {
            return false;
        }
        index += 1;
    }
    true
}

#[derive(Debug, Deserialize)]
struct CriterionBenchmarkJson {
    group_id: String,
    function_id: Option<String>,
    value_str: Option<String>,
    throughput: Option<Value>,
    full_id: String,
    directory_name: String,
    title: String,
}

#[derive(Debug, Deserialize)]
struct CriterionEstimatesJson {
    mean: Option<CriterionEstimateJson>,
    median: Option<CriterionEstimateJson>,
    median_abs_dev: Option<CriterionEstimateJson>,
    slope: Option<CriterionEstimateJson>,
    std_dev: Option<CriterionEstimateJson>,
}

impl CriterionEstimatesJson {
    fn into_estimates(self) -> Vec<BenchEstimate> {
        let mut estimates = Vec::new();
        push_estimate(&mut estimates, "mean", self.mean);
        push_estimate(&mut estimates, "median", self.median);
        push_estimate(&mut estimates, "median_abs_dev", self.median_abs_dev);
        push_estimate(&mut estimates, "slope", self.slope);
        push_estimate(&mut estimates, "std_dev", self.std_dev);
        estimates
    }
}

fn push_estimate(
    estimates: &mut Vec<BenchEstimate>,
    estimate_kind: &str,
    estimate: Option<CriterionEstimateJson>,
) {
    if let Some(estimate) = estimate {
        estimates.push(BenchEstimate {
            estimate_kind: estimate_kind.to_string(),
            point_estimate_ns: estimate.point_estimate,
            standard_error_ns: Some(estimate.standard_error),
            confidence_level: Some(estimate.confidence_interval.confidence_level),
            ci_lower_bound_ns: Some(estimate.confidence_interval.lower_bound),
            ci_upper_bound_ns: Some(estimate.confidence_interval.upper_bound),
        });
    }
}

#[derive(Debug, Deserialize)]
struct CriterionChangeEstimatesJson {
    mean: CriterionEstimateJson,
    median: CriterionEstimateJson,
}

#[derive(Debug, Deserialize)]
struct CriterionEstimateJson {
    confidence_interval: CriterionConfidenceIntervalJson,
    point_estimate: f64,
    standard_error: f64,
}

impl CriterionEstimateJson {
    fn into_relative_estimate(self, estimate_kind: &str) -> BenchComparisonEstimate {
        BenchComparisonEstimate {
            estimate_kind: estimate_kind.to_string(),
            point_estimate: self.point_estimate,
            standard_error: self.standard_error,
            confidence_level: self.confidence_interval.confidence_level,
            ci_lower_bound: self.confidence_interval.lower_bound,
            ci_upper_bound: self.confidence_interval.upper_bound,
        }
    }
}

#[derive(Debug, Deserialize)]
struct CriterionConfidenceIntervalJson {
    confidence_level: f64,
    lower_bound: f64,
    upper_bound: f64,
}

#[derive(Debug, Clone, Deserialize)]
struct CriterionSampleJson {
    iters: Vec<CriterionIterationCount>,
    times: Vec<f64>,
}

impl CriterionSampleJson {
    fn into_samples(self) -> Result<Vec<BenchSample>, String> {
        self.iters
            .into_iter()
            .zip(self.times)
            .enumerate()
            .map(|(sample_index, (iteration_count, elapsed_ns))| {
                Ok(BenchSample {
                    sample_index,
                    iteration_count: iteration_count.into_u64()?,
                    elapsed_ns,
                })
            })
            .collect()
    }

    fn avg_times(&self) -> Result<Vec<f64>, String> {
        self.iters
            .iter()
            .zip(&self.times)
            .map(|(iteration_count, elapsed_ns)| {
                let iteration_count = iteration_count.as_f64()?;
                if iteration_count == 0.0 {
                    return Err("criterion sample iteration count was zero".to_string());
                }
                Ok(*elapsed_ns / iteration_count)
            })
            .collect()
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(untagged)]
enum CriterionIterationCount {
    Integer(u64),
    Float(f64),
}

impl CriterionIterationCount {
    fn into_u64(self) -> Result<u64, String> {
        match self {
            Self::Integer(value) => Ok(value),
            Self::Float(value)
                if value.is_finite()
                    && value >= 0.0
                    && value.fract() == 0.0
                    && value <= u64::MAX as f64 =>
            {
                Ok(value as u64)
            }
            Self::Float(value) => Err(format!(
                "criterion sample iteration count `{value}` is not a non-negative whole number"
            )),
        }
    }

    fn as_f64(&self) -> Result<f64, String> {
        match self {
            Self::Integer(value) => Ok(*value as f64),
            Self::Float(value)
                if value.is_finite()
                    && *value >= 0.0
                    && value.fract() == 0.0
                    && *value <= u64::MAX as f64 =>
            {
                Ok(*value)
            }
            Self::Float(value) => Err(format!(
                "criterion sample iteration count `{value}` is not a non-negative whole number"
            )),
        }
    }
}

thread_local! {
    static SEED_RNG: RefCell<Rand64> = RefCell::new(Rand64::new(
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_else(|_| panic!("time went backwards"))
            .as_millis(),
    ));
}

fn criterion_p_value(
    current_samples: &[f64],
    baseline_samples: &[f64],
    nresamples: usize,
) -> Result<f64, String> {
    criterion_p_value_with_rng(current_samples, baseline_samples, nresamples, criterion_new_rng())
}

fn criterion_p_value_with_rng(
    current_samples: &[f64],
    baseline_samples: &[f64],
    nresamples: usize,
    rng: Rand64,
) -> Result<f64, String> {
    if current_samples.len() < 2 || baseline_samples.len() < 2 {
        return Err("criterion comparison requires at least two samples in each run".to_string());
    }

    let t_statistic = sample_t(current_samples, baseline_samples);
    if !t_statistic.is_finite() {
        return Err("criterion comparison could not compute a finite T statistic".to_string());
    }
    let mut combined = Vec::with_capacity(current_samples.len() + baseline_samples.len());
    combined.extend_from_slice(current_samples);
    combined.extend_from_slice(baseline_samples);

    let mut resampler = CriterionResamples::with_rng(combined, rng);
    let mut t_distribution = Vec::with_capacity(nresamples);
    // Criterion derives `p_value` from a mixed-bootstrap T distribution rather than from a
    // closed-form Welch test. We mirror that private 0.5.1 implementation here so the CLI can
    // report the same style of comparison data while still persisting exact Criterion JSON files.
    for _ in 0..nresamples {
        let resample = resampler.next();
        let split = current_samples.len();
        let t_value = sample_t(&resample[..split], &resample[split..]);
        if t_value.is_finite() {
            t_distribution.push(t_value);
        }
    }

    if t_distribution.is_empty() {
        return Err("criterion comparison produced an empty T distribution".to_string());
    }

    let hits = t_distribution.iter().filter(|value| **value < t_statistic).count();
    let tails = 2.0;
    Ok((usize::min(hits, t_distribution.len() - hits) as f64 / t_distribution.len() as f64) * tails)
}

fn sample_t(current_samples: &[f64], baseline_samples: &[f64]) -> f64 {
    let current_mean = sample_mean(current_samples);
    let baseline_mean = sample_mean(baseline_samples);
    let current_variance = sample_variance(current_samples, current_mean);
    let baseline_variance = sample_variance(baseline_samples, baseline_mean);
    let denominator = (current_variance / current_samples.len() as f64
        + baseline_variance / baseline_samples.len() as f64)
        .sqrt();

    (current_mean - baseline_mean) / denominator
}

fn sample_mean(values: &[f64]) -> f64 {
    values.iter().copied().sum::<f64>() / values.len() as f64
}

fn sample_variance(values: &[f64], mean: f64) -> f64 {
    let squared_diffs = values.iter().map(|value| (*value - mean).powi(2)).sum::<f64>();
    squared_diffs / (values.len() - 1) as f64
}

struct CriterionResamples {
    rng: Rand64,
    sample: Vec<f64>,
    stage: Vec<f64>,
}

impl CriterionResamples {
    fn with_rng(sample: Vec<f64>, rng: Rand64) -> Self {
        let sample_len = sample.len();
        Self { rng, sample, stage: Vec::with_capacity(sample_len) }
    }

    fn next(&mut self) -> &[f64] {
        if self.stage.is_empty() {
            self.stage.resize(self.sample.len(), 0.0);
        }

        for slot in &mut self.stage {
            let index = self.rng.rand_range(0..self.sample.len() as u64) as usize;
            *slot = self.sample[index];
        }

        &self.stage
    }
}

fn criterion_new_rng() -> Rand64 {
    SEED_RNG.with(|rng| {
        let mut rng = rng.borrow_mut();
        let seed = ((rng.rand_u64() as u128) << 64) | (rng.rand_u64() as u128);
        Rand64::new(seed)
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use oorandom::Rand64;
    use serde_json::json;
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn find_new_report_dir_prefers_criterion_new_directory() {
        let tempdir = TempDir::new().expect("tempdir");
        let benchmark_root = tempdir.path().join("bench_normalize_phrase");
        let base_dir = benchmark_root.join("base");
        let new_dir = benchmark_root.join("new");

        fs::create_dir_all(&base_dir).expect("base dir");
        fs::create_dir_all(&new_dir).expect("new dir");

        for directory in [&base_dir, &new_dir] {
            fs::write(directory.join("benchmark.json"), "{}").expect("benchmark.json");
            fs::write(directory.join("estimates.json"), "{}").expect("estimates.json");
            fs::write(directory.join("sample.json"), "{}").expect("sample.json");
        }

        let discovered = find_new_report_dir(tempdir.path()).expect("criterion new dir");
        assert_eq!(discovered, new_dir);
    }

    #[test]
    fn criterion_summary_matches_expected_labels() {
        let improved = estimate_json(-1.5, -1.2, -1.0);
        assert_eq!(
            criterion_summary_from_change_estimate(&improved, 0.01, 0.05, 0.01),
            "Performance has improved."
        );

        let regressed = estimate_json(1.5, 1.2, 1.8);
        assert_eq!(
            criterion_summary_from_change_estimate(&regressed, 0.01, 0.05, 0.01),
            "Performance has regressed."
        );

        let within_noise = estimate_json(0.004, -0.009, 0.008);
        assert_eq!(
            criterion_summary_from_change_estimate(&within_noise, 0.01, 0.05, 0.01),
            "Change within noise threshold."
        );

        let not_significant = estimate_json(1.5, 1.2, 1.8);
        assert_eq!(
            criterion_summary_from_change_estimate(&not_significant, 0.75, 0.05, 0.01),
            "No change in performance detected."
        );
    }

    #[test]
    fn materialize_baseline_artifacts_writes_criterion_base_layout() {
        let tempdir = TempDir::new().expect("tempdir");
        let artifacts = baseline_artifacts("bench_normalize_phrase");

        materialize_baseline_artifacts(tempdir.path(), &artifacts).expect("materialize baseline");

        let base_dir = tempdir.path().join("bench_normalize_phrase").join("base");
        assert!(base_dir.join("benchmark.json").exists());
        assert!(base_dir.join("estimates.json").exists());
        assert!(base_dir.join("sample.json").exists());
        assert!(base_dir.join("tukey.json").exists());
    }

    #[test]
    fn collect_artifacts_includes_change_estimates_when_present() {
        let tempdir = TempDir::new().expect("tempdir");
        let benchmark_root = tempdir.path().join("bench_normalize_phrase");
        let new_dir = benchmark_root.join("new");
        let change_dir = benchmark_root.join("change");
        fs::create_dir_all(&new_dir).expect("new dir");
        fs::create_dir_all(&change_dir).expect("change dir");

        let benchmark_json = benchmark_json("bench_normalize_phrase");
        let estimates_json = absolute_estimates_json();
        let sample_json = sample_json(&[1, 2, 3], &[10.0, 20.0, 30.0]);

        write_json_value(&new_dir.join("benchmark.json"), &benchmark_json).expect("benchmark");
        write_json_value(&new_dir.join("estimates.json"), &estimates_json).expect("estimates");
        write_json_value(&new_dir.join("sample.json"), &sample_json).expect("sample");
        write_json_value(&new_dir.join("tukey.json"), &json!({"a": 1})).expect("tukey");
        write_json_value(&change_dir.join("estimates.json"), &change_estimates_json(1.5, 1.2, 1.8))
            .expect("change estimates");

        let artifacts =
            collect_artifacts(&benchmark_root, &benchmark_json, &estimates_json, &sample_json)
                .expect("collect artifacts");
        let artifact_kinds =
            artifacts.iter().map(|artifact| artifact.artifact_kind.as_str()).collect::<Vec<_>>();

        assert!(artifact_kinds.contains(&ARTIFACT_KIND_BENCHMARK_JSON));
        assert!(artifact_kinds.contains(&ARTIFACT_KIND_ESTIMATES_JSON));
        assert!(artifact_kinds.contains(&ARTIFACT_KIND_SAMPLE_JSON));
        assert!(artifact_kinds.contains(&ARTIFACT_KIND_TUKEY_JSON));
        assert!(artifact_kinds.contains(&ARTIFACT_KIND_CHANGE_ESTIMATES_JSON));
    }

    #[test]
    fn criterion_p_value_with_rng_detects_large_regression() {
        let current = [1000.0, 1001.0, 1002.5, 998.0, 1003.0, 999.5];
        let baseline = [1.0, 2.0, 2.5, 1.5, 3.0, 2.2];

        let p_value =
            criterion_p_value_with_rng(&current, &baseline, 10_000, Rand64::new(42)).unwrap();
        assert!(p_value < 0.05, "expected a significant difference, got p={p_value}");
    }

    #[test]
    fn criterion_p_value_with_rng_is_high_for_identical_samples() {
        let sample = [10.0, 12.0, 13.5, 11.5, 9.5, 14.0];

        let p_value =
            criterion_p_value_with_rng(&sample, &sample, 10_000, Rand64::new(42)).expect("p value");
        assert!(p_value >= 0.5, "expected no significant difference, got p={p_value}");
    }

    #[test]
    fn parse_comparison_uses_persisted_baseline_artifacts() {
        let tempdir = TempDir::new().expect("tempdir");
        let benchmark_root = tempdir.path().join("bench_normalize_phrase");
        let change_dir = benchmark_root.join("change");
        fs::create_dir_all(&change_dir).expect("change dir");

        write_json_value(&change_dir.join("estimates.json"), &change_estimates_json(1.5, 1.2, 1.8))
            .expect("change estimates");

        let current_samples = serde_json::from_value::<CriterionSampleJson>(sample_json(
            &[1, 1, 1, 1, 1, 1],
            &[1000.0, 1001.0, 1002.0, 998.0, 1003.0, 999.0],
        ))
        .expect("current samples");
        let config = BenchConfig {
            sample_size: 100,
            measurement_time_ms: 5_000,
            warm_up_time_ms: 3_000,
            nresamples: 10_000,
            noise_threshold: 0.01,
            significance_level: 0.05,
        };

        let comparison = parse_comparison(
            &benchmark_root,
            Some(&baseline_artifacts("bench_normalize_phrase")),
            &current_samples,
            &config,
        )
        .expect("comparison")
        .expect("comparison payload");

        assert_eq!(comparison.summary, "Performance has regressed.");
        assert!(comparison.p_value < 0.05, "expected a significant difference");
        assert!(comparison.mean.point_estimate > 1.0);
    }

    fn baseline_artifacts(directory_name: &str) -> Vec<BenchArtifact> {
        vec![
            BenchArtifact {
                artifact_kind: ARTIFACT_KIND_BENCHMARK_JSON.to_string(),
                media_type: "application/json".to_string(),
                payload_json: benchmark_json(directory_name),
            },
            BenchArtifact {
                artifact_kind: ARTIFACT_KIND_ESTIMATES_JSON.to_string(),
                media_type: "application/json".to_string(),
                payload_json: absolute_estimates_json(),
            },
            BenchArtifact {
                artifact_kind: ARTIFACT_KIND_SAMPLE_JSON.to_string(),
                media_type: "application/json".to_string(),
                payload_json: sample_json(&[1, 1, 1, 1, 1, 1], &[1.0, 2.0, 2.5, 1.5, 3.0, 2.2]),
            },
            BenchArtifact {
                artifact_kind: ARTIFACT_KIND_TUKEY_JSON.to_string(),
                media_type: "application/json".to_string(),
                payload_json: json!({"fences": [0.0, 1.0, 2.0, 3.0]}),
            },
        ]
    }

    fn benchmark_json(directory_name: &str) -> Value {
        json!({
            "group_id": "bench_normalize_phrase",
            "function_id": null,
            "value_str": null,
            "throughput": null,
            "full_id": "bench_normalize_phrase",
            "directory_name": directory_name,
            "title": "bench_normalize_phrase",
        })
    }

    fn absolute_estimates_json() -> Value {
        json!({
            "mean": estimate_value(300.0, 290.0, 310.0),
            "median": estimate_value(295.0, 285.0, 305.0),
            "median_abs_dev": estimate_value(5.0, 4.0, 6.0),
            "slope": estimate_value(280.0, 270.0, 290.0),
            "std_dev": estimate_value(8.0, 7.0, 9.0),
        })
    }

    fn change_estimates_json(point: f64, lower: f64, upper: f64) -> Value {
        json!({
            "mean": estimate_value(point, lower, upper),
            "median": estimate_value(point, lower, upper),
        })
    }

    fn estimate_value(point: f64, lower: f64, upper: f64) -> Value {
        json!({
            "confidence_interval": {
                "confidence_level": 0.95,
                "lower_bound": lower,
                "upper_bound": upper,
            },
            "point_estimate": point,
            "standard_error": 0.01,
        })
    }

    fn estimate_json(point: f64, lower: f64, upper: f64) -> CriterionEstimateJson {
        serde_json::from_value(estimate_value(point, lower, upper)).expect("estimate")
    }

    fn sample_json(iters: &[u64], times: &[f64]) -> Value {
        json!({
            "iters": iters,
            "times": times,
        })
    }
}

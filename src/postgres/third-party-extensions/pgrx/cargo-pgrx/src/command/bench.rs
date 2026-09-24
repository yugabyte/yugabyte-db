//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::CommandExecute;
use crate::cargo::CargoProfile;
use crate::command::get::get_property;
use crate::command::run::run;
use crate::command::start::{collect_postgresql_conf_settings, start_postgres};
use crate::manifest::{get_package_manifest, pg_config_and_version};
use eyre::{Context, eyre};
use owo_colors::OwoColorize;
use pgrx_pg_config::{PgConfig, Pgrx, createdb, dropdb};
use postgres::{Client, NoTls};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::thread;
use std::time::{Duration, SystemTime};
use uuid::Uuid;

const BENCH_WRAPPER_SCHEMA: &str = "benches";
const REPORT_HISTORY_LIMIT: i64 = 10;
const REPORT_BAR_WIDTH: usize = 28;

/// Run in-process benchmarks using `#[pg_bench]` functions
#[derive(clap::Args, Debug, Clone)]
#[clap(author)]
pub(crate) struct Bench {
    /// Positional arguments: [pgXX] [benchname]
    #[clap(env = "PG_VERSION")]
    args: Vec<String>,

    /// If specified, use this database name instead of `$extname_benches`
    #[clap(long)]
    dbname: Option<String>,
    /// Unique name for this benchmark group
    #[clap(long)]
    group_name: Option<String>,
    /// Named benchmark group to compare against
    #[clap(long)]
    compare_group: Option<String>,
    /// Recreate the benchmark database before running
    #[clap(long)]
    resetdb: bool,
    /// Use CASCADE when dropping the extension during refresh
    #[clap(long)]
    cascade: bool,
    /// List discovered benchmark wrappers and exit
    #[clap(long)]
    list: bool,
    /// Render a read-only history report from the benchmark database
    #[clap(long)]
    report: bool,
    /// Emit the final summary as JSON
    #[clap(long)]
    json: bool,
    /// Sleep for this many seconds after printing the backend PID and before starting benchmarks
    #[clap(long, value_name = "SECONDS", default_value_t = 0)]
    wait: u64,
    /// Package to build (see `cargo help pkgid`)
    #[clap(long, short)]
    package: Option<String>,
    /// Path to Cargo.toml
    #[clap(long, value_parser)]
    manifest_path: Option<PathBuf>,
    /// Compile for debug mode instead of the default release mode
    #[clap(long)]
    debug: bool,
    /// Specific profile to use (conflicts with `--debug`)
    #[clap(long)]
    profile: Option<String>,
    #[clap(flatten)]
    features: clap_cargo::Features,
    #[clap(long)]
    target: Option<String>,
    #[clap(from_global, action = clap::ArgAction::Count)]
    verbose: u8,
    /// Custom `postgresql.conf` settings in the form of `key=value`
    #[clap(long)]
    postgresql_conf: Vec<String>,
}

impl CommandExecute for Bench {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(self) -> eyre::Result<()> {
        let (resolved_pg_version, bench_filter) = self.resolve_args()?;
        let pgrx = Pgrx::from_config()?;

        let (package_manifest, package_manifest_path) = get_package_manifest(
            &self.features,
            self.package.as_deref(),
            self.manifest_path.as_deref(),
        )?;
        let mut resolved_features = self.features.clone();
        let (pg_config, _) = pg_config_and_version(
            &pgrx,
            &package_manifest,
            resolved_pg_version,
            Some(&mut resolved_features),
            true,
        )?;

        let extname = get_property(&package_manifest_path, "extname")?
            .ok_or(eyre!("could not determine extension name"))?;
        let dbname = self.dbname.clone().unwrap_or_else(|| format!("{extname}_benches"));
        if self.report {
            return self.execute_report(&pg_config, &dbname, &extname, bench_filter.as_deref());
        }

        let postgresql_conf = collect_postgresql_conf_settings(&self.postgresql_conf)?;
        let extversion = crate::command::install::get_version(&package_manifest_path)?;
        let mut features = resolved_features;
        ensure_feature(&mut features, "pg_bench");
        let profile = CargoProfile::from_flags(
            self.profile.as_deref(),
            if self.debug { CargoProfile::Dev } else { CargoProfile::Release },
        )?;

        run(
            &pg_config,
            self.manifest_path.as_deref(),
            self.package.as_deref(),
            &package_manifest_path,
            &dbname,
            true,
            &profile,
            &features,
            false,
            false,
            self.target.as_deref(),
            &postgresql_conf,
        )?;

        if self.resetdb {
            dropdb(&pg_config, &dbname, false, None)?;
            createdb(&pg_config, &dbname, false, true, None)?;
        }

        let mut client = connect_client(&pg_config, &dbname)?;
        ensure_persistent_schema(&mut client)?;

        refresh_extension(&mut client, &extname, self.cascade)?;

        let benchmarks = discover_benchmarks(&mut client, bench_filter.as_deref())?;
        if self.list {
            for benchmark in benchmarks {
                println!(
                    "{} [{}]",
                    benchmark.descriptor.bench_name,
                    format_benchmark_settings(&benchmark.descriptor)
                );
            }
            return Ok(());
        }

        if benchmarks.is_empty() {
            eyre::bail!(
                "no benchmarks discovered in schema `{BENCH_WRAPPER_SCHEMA}` from `mod benches`"
            );
        }

        let git_metadata = collect_git_metadata(package_manifest_path.parent().unwrap())?;
        let resolved_group_name = match self.group_name.clone() {
            Some(name) => name,
            None => default_group_name(&mut client, git_metadata.git_commit.as_deref())?,
        };
        let compare_group = resolve_compare_group(
            &mut client,
            self.compare_group.as_deref(),
            &profile,
            &resolved_group_name,
        )?;

        let run_group_id = insert_run_group(
            &mut client,
            &resolved_group_name,
            compare_group.as_ref(),
            &extname,
            &extversion,
            &pg_config,
            &profile,
            &features,
            &git_metadata,
        )?;
        snapshot_pg_settings(&mut client, run_group_id)?;

        let backend_pid = load_backend_pid(&mut client)?;
        print_backend_pid(backend_pid);
        wait_before_starting_benchmarks(self.wait);

        let mut summary_benchmarks = Vec::new();
        let mut failures = 0usize;

        let show_human_output = !self.json;

        for benchmark in &benchmarks {
            if show_human_output {
                print_running_benchmark(benchmark);
            }
            let baseline = compare_group
                .as_ref()
                .map(|group| {
                    load_benchmark_result_for_group(
                        &mut client,
                        group.id,
                        &benchmark.descriptor.bench_name,
                    )
                })
                .transpose()?
                .flatten();
            // Persisted Criterion artifacts are replayed back into the backend so the benchmark's
            // `change` analysis comes from Criterion's own baseline processing instead of a host-
            // side approximation over normalized SQL tables.
            let baseline_artifacts =
                if baseline.as_ref().is_some_and(|baseline| baseline.status == BenchStatus::Ok) {
                    compare_group
                        .as_ref()
                        .map(|group| {
                            load_criterion_artifacts_for_group(
                                &mut client,
                                group.id,
                                &benchmark.descriptor.bench_name,
                            )
                        })
                        .transpose()?
                        .flatten()
                } else {
                    None
                };
            let started_at = SystemTime::now();
            let payload = execute_benchmark_query(
                &mut client,
                &benchmark.run_wrapper_name,
                baseline_artifacts.as_deref(),
            )?;
            let finished_at = SystemTime::now();

            if payload.status == BenchStatus::Failed {
                failures += 1;
            }

            let benchmark_run_id = persist_benchmark_result(
                &mut client,
                run_group_id,
                &payload,
                started_at,
                finished_at,
            )?;
            summary_benchmarks.push(build_benchmark_summary(
                benchmark_run_id,
                &payload,
                baseline.as_ref(),
                compare_group.as_ref().map(|group| group.group_name.as_str()),
            ));
            if show_human_output && let Some(completed_benchmark) = summary_benchmarks.last() {
                print_completed_benchmark(completed_benchmark);
            }
        }

        let status = if failures == 0 {
            "completed"
        } else if failures == benchmarks.len() {
            "failed"
        } else {
            "partial"
        };
        mark_run_group_complete(&mut client, run_group_id, status)?;

        let missing_from_current = if let Some(compare_group) = &compare_group {
            load_missing_benchmarks(&mut client, run_group_id, compare_group.id)?
        } else {
            Vec::new()
        };
        let summary = BenchSummary {
            group_name: resolved_group_name,
            compare_group_name: compare_group.map(|group| group.group_name),
            benchmarks: summary_benchmarks,
            missing_from_current,
        };

        if self.json {
            println!("{}", serde_json::to_string_pretty(&summary)?);
        } else {
            print_summary(&summary);
        }

        Ok(())
    }
}

impl Bench {
    fn resolve_args(&self) -> eyre::Result<(Option<String>, Option<String>)> {
        match self.args.as_slice() {
            [] => Ok((None, None)),
            [only] if only.starts_with("pg") => Ok((Some(only.clone()), None)),
            [only] => Ok((None, Some(only.clone()))),
            [pg_version, benchname] if pg_version.starts_with("pg") => {
                Ok((Some(pg_version.clone()), Some(benchname.clone())))
            }
            _ => Err(eyre!(
                "expected positional arguments `[pgXX] [benchname]`, got `{}`",
                self.args.join(" ")
            )),
        }
    }

    fn validate_report_args(&self) -> eyre::Result<()> {
        let mut incompatible = Vec::new();
        if self.group_name.is_some() {
            incompatible.push("--group-name");
        }
        if self.compare_group.is_some() {
            incompatible.push("--compare-group");
        }
        if self.resetdb {
            incompatible.push("--resetdb");
        }
        if self.cascade {
            incompatible.push("--cascade");
        }
        if self.list {
            incompatible.push("--list");
        }
        if self.json {
            incompatible.push("--json");
        }
        if self.wait != 0 {
            incompatible.push("--wait");
        }
        if !self.postgresql_conf.is_empty() {
            incompatible.push("--postgresql-conf");
        }

        if incompatible.is_empty() {
            return Ok(());
        }

        Err(eyre!("`--report` can't be combined with {}", incompatible.join(", ")))
    }

    fn execute_report(
        &self,
        pg_config: &PgConfig,
        dbname: &str,
        extname: &str,
        bench_filter: Option<&str>,
    ) -> eyre::Result<()> {
        self.validate_report_args()?;
        start_postgres(pg_config, &Default::default(), false)?;

        let mut client = connect_client(pg_config, dbname)
            .wrap_err_with(|| format!("failed to connect to benchmark database `{dbname}`"))?;
        ensure_report_history_available(&mut client, dbname)?;

        let rows = load_recent_report_runs(&mut client, extname, bench_filter)?;
        if rows.is_empty() {
            if let Some(bench_filter) = bench_filter {
                eyre::bail!(
                    "no benchmark history matching `{bench_filter}` was found in database `{dbname}`"
                );
            }
            eyre::bail!("no benchmark history was found in database `{dbname}`");
        }

        let baselines = load_report_baselines(&mut client, extname, bench_filter)?;
        let settings_by_group = load_nondefault_settings_for_groups(
            &mut client,
            rows.iter()
                .map(|row| row.group.group_id)
                .chain(baselines.values().map(|baseline| baseline.group.group_id)),
        )?;

        let report = BenchHistoryReport {
            dbname: dbname.to_string(),
            bench_filter: bench_filter.map(str::to_string),
            sections: build_report_sections(rows, baselines, &settings_by_group),
        };
        print_history_report(&report);
        Ok(())
    }
}

fn ensure_feature(features: &mut clap_cargo::Features, feature: &str) {
    if !features.features.iter().any(|value| value == feature) {
        features.features.push(feature.to_string());
    }
}

fn connect_client(pg_config: &PgConfig, dbname: &str) -> eyre::Result<Client> {
    let user = current_user()?;
    postgres::Config::new()
        .host(pg_config.host())
        .port(pg_config.port()?)
        .user(&user)
        .dbname(dbname)
        .connect(NoTls)
        .wrap_err("failed to connect to Postgres benchmark database")
}

fn current_user() -> eyre::Result<String> {
    std::env::var("USER")
        .or_else(|_| std::env::var("USERNAME"))
        .map_err(|_| eyre!("could not determine current operating-system user from environment"))
}

fn ensure_persistent_schema(client: &mut Client) -> eyre::Result<()> {
    let schema_sql = std::str::from_utf8(PERSISTENT_SCHEMA_SQL_BYTES)
        .expect("pgrx-bench.sql should contain valid UTF-8");
    client.batch_execute(schema_sql)?;
    Ok(())
}

fn refresh_extension(client: &mut Client, extname: &str, cascade: bool) -> eyre::Result<()> {
    let quoted_extname = quote_ident(extname);
    let drop_sql = if cascade {
        format!("DROP EXTENSION IF EXISTS {quoted_extname} CASCADE")
    } else {
        format!("DROP EXTENSION IF EXISTS {quoted_extname}")
    };

    if let Err(error) = client.batch_execute(&drop_sql) {
        if !cascade {
            return Err(eyre!(
                "failed to drop extension `{extname}` before bench refresh: {error}\nrerun with `cargo pgrx bench --cascade` if you want dependency cleanup"
            ));
        }
        return Err(error.into());
    }

    client.batch_execute(&format!("CREATE EXTENSION {quoted_extname}"))?;
    Ok(())
}

fn discover_benchmarks(
    client: &mut Client,
    filter: Option<&str>,
) -> eyre::Result<Vec<DiscoveredBenchmark>> {
    let rows = client.query(
        "SELECT proname
         FROM pg_proc
         JOIN pg_namespace ON pg_namespace.oid = pg_proc.pronamespace
         WHERE pg_namespace.nspname = $1
           AND proname LIKE '__pgrx_bench_run_%'
         ORDER BY proname",
        &[&BENCH_WRAPPER_SCHEMA],
    )?;

    let mut benchmarks = Vec::new();
    for row in rows {
        let run_wrapper_name: String = row.get(0);
        let descriptor = load_benchmark_descriptor(client, &run_wrapper_name)?;
        if filter.is_none_or(|filter| {
            run_wrapper_name.contains(filter) || descriptor.bench_name.contains(filter)
        }) {
            benchmarks.push(DiscoveredBenchmark { run_wrapper_name, descriptor });
        }
    }

    Ok(benchmarks)
}

fn execute_benchmark_query(
    client: &mut Client,
    benchmark_wrapper: &str,
    baseline_artifacts: Option<&[BenchArtifact]>,
) -> eyre::Result<BenchResult> {
    let query = format!(
        "SELECT {}.{}($1)",
        quote_ident(BENCH_WRAPPER_SCHEMA),
        quote_ident(benchmark_wrapper)
    );
    let mut tx = client.transaction()?;
    let baseline_payload = baseline_artifacts.map(serde_json::to_value).transpose()?;
    let row = tx.query_one(&query, &[&baseline_payload])?;
    let payload: Value = row.get(0);
    tx.rollback()?;
    serde_json::from_value(payload).wrap_err("failed to decode benchmark result payload")
}

fn load_benchmark_descriptor(
    client: &mut Client,
    run_wrapper_name: &str,
) -> eyre::Result<BenchDescriptor> {
    let describe_wrapper_name =
        run_wrapper_name.replacen("__pgrx_bench_run_", "__pgrx_bench_describe_", 1);
    let query = format!(
        "SELECT {}.{}()",
        quote_ident(BENCH_WRAPPER_SCHEMA),
        quote_ident(&describe_wrapper_name)
    );
    let row = client.query_one(&query, &[])?;
    let payload: Value = row.get(0);
    serde_json::from_value(payload).wrap_err("failed to decode benchmark descriptor payload")
}

fn insert_run_group(
    client: &mut Client,
    group_name: &str,
    compare_group: Option<&ResolvedGroup>,
    extname: &str,
    extversion: &str,
    pg_config: &PgConfig,
    profile: &CargoProfile,
    features: &clap_cargo::Features,
    git_metadata: &GitMetadata,
) -> eyre::Result<Uuid> {
    let id = Uuid::new_v4();
    let cargo_features = features.features.clone();
    let command_line = std::env::args().collect::<Vec<_>>().join(" ");
    let rustc_version = command_output("rustc", ["--version"]).ok();
    let cargo_version = command_output("cargo", ["--version"]).ok();
    let os = std::env::consts::OS.to_string();
    let arch = std::env::consts::ARCH.to_string();
    let compare_group_id = compare_group.map(|group| group.id);

    client.execute(
        "INSERT INTO pgrx_bench.run_group (
            id,
            group_name,
            status,
            compare_group_id,
            extname,
            extversion,
            pg_version_major,
            profile_name,
            cargo_features,
            command_line,
            os,
            arch,
            rustc_version,
            cargo_version,
            pgrx_version,
            cargo_pgrx_version,
            git_commit,
            git_branch,
            git_dirty,
            git_describe
        ) VALUES (
            $1, $2, 'running', $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19
        )",
        &[
            &id,
            &group_name,
            &compare_group_id,
            &extname,
            &extversion,
            &i32::try_from(pg_config.major_version()?)?,
            &profile.name(),
            &cargo_features,
            &command_line,
            &os,
            &arch,
            &rustc_version,
            &cargo_version,
            &env!("CARGO_PKG_VERSION"),
            &env!("CARGO_PKG_VERSION"),
            &git_metadata.git_commit,
            &git_metadata.git_branch,
            &git_metadata.git_dirty,
            &git_metadata.git_describe,
        ],
    )?;

    Ok(id)
}

fn snapshot_pg_settings(client: &mut Client, group_id: Uuid) -> eyre::Result<()> {
    client.execute(
        "INSERT INTO pgrx_bench.run_group_pg_setting (
            group_id, name, setting, unit, source, sourcefile, sourceline, boot_val, reset_val, pending_restart
        )
        SELECT
            $1, name, setting, unit, source, sourcefile, sourceline, boot_val, reset_val, pending_restart
        FROM pg_settings",
        &[&group_id],
    )?;
    Ok(())
}

fn load_backend_pid(client: &mut Client) -> eyre::Result<i32> {
    let row = client.query_one("SELECT pg_backend_pid()", &[])?;
    Ok(row.get(0))
}

fn print_backend_pid(backend_pid: i32) {
    eprintln!("backend pid={backend_pid}");
    let _ = std::io::stderr().flush();
}

fn wait_before_starting_benchmarks(wait_secs: u64) {
    if wait_secs == 0 {
        return;
    }

    eprintln!(
        "{} {} before starting benchmarks",
        "     Waiting".bold().cyan(),
        format_wait_duration(wait_secs).bold().white()
    );
    let _ = std::io::stderr().flush();
    thread::sleep(Duration::from_secs(wait_secs));
}

fn format_wait_duration(wait_secs: u64) -> String {
    let unit = if wait_secs == 1 { "second" } else { "seconds" };
    format!("{wait_secs} {unit}")
}

fn persist_benchmark_result(
    client: &mut Client,
    group_id: Uuid,
    payload: &BenchResult,
    started_at: SystemTime,
    finished_at: SystemTime,
) -> eyre::Result<i64> {
    let mut tx = client.transaction()?;

    let case_id: i64 = tx
        .query_one(
            "INSERT INTO pgrx_bench.benchmark_case (
                schema_name,
                bench_name,
                function_name,
                setup_function,
                transaction_mode,
                source_file,
                source_line
            ) VALUES ($1, $2, $3, $4, $5, $6, $7)
            ON CONFLICT (schema_name, bench_name) DO UPDATE
            SET function_name = EXCLUDED.function_name,
                setup_function = EXCLUDED.setup_function,
                transaction_mode = EXCLUDED.transaction_mode,
                source_file = EXCLUDED.source_file,
                source_line = EXCLUDED.source_line
            RETURNING id",
            &[
                &payload.schema_name,
                &payload.bench_name,
                &payload.function_name,
                &payload.setup_function,
                &payload.transaction_mode.as_str(),
                &payload.source_file,
                &i32::try_from(payload.source_line)?,
            ],
        )?
        .get(0);

    // The exact Criterion files live in `pgrx_bench.artifact`; `raw_result` keeps the normalized
    // benchmark payload small enough for summary queries and historical comparisons.
    let mut raw_payload = payload.clone();
    raw_payload.artifacts.clear();
    let raw_result = serde_json::to_value(&raw_payload)?;
    let benchmark_run_id: i64 = tx
        .query_one(
            "INSERT INTO pgrx_bench.benchmark_run (
                group_id,
                case_id,
                status,
                error_text,
                started_at,
                finished_at,
                criterion_config,
                raw_result
            ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
            RETURNING id",
            &[
                &group_id,
                &case_id,
                &bench_status_as_str(&payload.status),
                &payload.error_text,
                &started_at,
                &finished_at,
                &serde_json::to_value(&payload.criterion_config)?,
                &raw_result,
            ],
        )?
        .get(0);

    for estimate in &payload.estimates {
        tx.execute(
            "INSERT INTO pgrx_bench.benchmark_estimate (
                benchmark_run_id,
                estimate_kind,
                point_estimate_ns,
                standard_error_ns,
                confidence_level,
                ci_lower_bound_ns,
                ci_upper_bound_ns
            ) VALUES ($1, $2, $3, $4, $5, $6, $7)",
            &[
                &benchmark_run_id,
                &estimate.estimate_kind,
                &estimate.point_estimate_ns,
                &estimate.standard_error_ns,
                &estimate.confidence_level,
                &estimate.ci_lower_bound_ns,
                &estimate.ci_upper_bound_ns,
            ],
        )?;
    }

    for sample in &payload.samples {
        tx.execute(
            "INSERT INTO pgrx_bench.benchmark_sample (
                benchmark_run_id,
                sample_index,
                iteration_count,
                elapsed_ns
            ) VALUES ($1, $2, $3, $4)",
            &[
                &benchmark_run_id,
                &i32::try_from(sample.sample_index)?,
                &i64::try_from(sample.iteration_count)?,
                &sample.elapsed_ns,
            ],
        )?;
    }

    if let Some(throughput) = &payload.throughput {
        tx.execute(
            "INSERT INTO pgrx_bench.benchmark_throughput (
                benchmark_run_id,
                kind,
                value
            ) VALUES ($1, $2, $3)",
            &[&benchmark_run_id, &throughput.kind, &throughput.value],
        )?;
    }

    for artifact in &payload.artifacts {
        tx.execute(
            "INSERT INTO pgrx_bench.artifact (
                benchmark_run_id,
                artifact_kind,
                media_type,
                payload_json
            ) VALUES ($1, $2, $3, $4)",
            &[
                &benchmark_run_id,
                &artifact.artifact_kind,
                &artifact.media_type,
                &artifact.payload_json,
            ],
        )?;
    }

    tx.commit()?;
    Ok(benchmark_run_id)
}

fn mark_run_group_complete(client: &mut Client, group_id: Uuid, status: &str) -> eyre::Result<()> {
    client.execute(
        "UPDATE pgrx_bench.run_group
         SET status = $2,
             completed_at = clock_timestamp()
         WHERE id = $1",
        &[&group_id, &status],
    )?;
    Ok(())
}

fn load_benchmark_result_for_group(
    client: &mut Client,
    group_id: Uuid,
    bench_name: &str,
) -> eyre::Result<Option<BenchResult>> {
    let row = client.query_opt(
        "SELECT benchmark_run.raw_result
         FROM pgrx_bench.benchmark_run
         JOIN pgrx_bench.benchmark_case ON benchmark_case.id = benchmark_run.case_id
         WHERE benchmark_run.group_id = $1
           AND benchmark_case.bench_name = $2",
        &[&group_id, &bench_name],
    )?;

    row.map(|row| {
        let payload: Value = row.get(0);
        serde_json::from_value(payload).wrap_err("failed to decode persisted benchmark result")
    })
    .transpose()
}

fn load_criterion_artifacts_for_group(
    client: &mut Client,
    group_id: Uuid,
    bench_name: &str,
) -> eyre::Result<Option<Vec<BenchArtifact>>> {
    let rows = client.query(
        "SELECT artifact_kind, media_type, payload_json
         FROM pgrx_bench.artifact
         JOIN pgrx_bench.benchmark_run ON benchmark_run.id = artifact.benchmark_run_id
         JOIN pgrx_bench.benchmark_case ON benchmark_case.id = benchmark_run.case_id
         WHERE benchmark_run.group_id = $1
           AND benchmark_case.bench_name = $2
         ORDER BY artifact_kind",
        &[&group_id, &bench_name],
    )?;

    if rows.is_empty() {
        return Ok(None);
    }

    Ok(Some(
        rows.into_iter()
            .map(|row| BenchArtifact {
                artifact_kind: row.get(0),
                media_type: row.get(1),
                payload_json: row.get(2),
            })
            .collect(),
    ))
}

fn load_missing_benchmarks(
    client: &mut Client,
    current_group_id: Uuid,
    baseline_group_id: Uuid,
) -> eyre::Result<Vec<String>> {
    let rows = client.query(
        "SELECT baseline_case.bench_name
         FROM pgrx_bench.benchmark_run AS baseline_run
         JOIN pgrx_bench.benchmark_case AS baseline_case
           ON baseline_case.id = baseline_run.case_id
         LEFT JOIN pgrx_bench.benchmark_run AS current_run
           ON current_run.group_id = $1
          AND current_run.case_id = baseline_run.case_id
         WHERE baseline_run.group_id = $2
           AND current_run.id IS NULL
         ORDER BY baseline_case.bench_name",
        &[&current_group_id, &baseline_group_id],
    )?;

    Ok(rows.into_iter().map(|row| row.get(0)).collect())
}

fn ensure_report_history_available(client: &mut Client, dbname: &str) -> eyre::Result<()> {
    let row = client.query_one(
        "SELECT
            to_regclass('pgrx_bench.run_group') IS NOT NULL
            AND to_regclass('pgrx_bench.benchmark_case') IS NOT NULL
            AND to_regclass('pgrx_bench.benchmark_run') IS NOT NULL
            AND to_regclass('pgrx_bench.benchmark_estimate') IS NOT NULL
            AND to_regclass('pgrx_bench.run_group_pg_setting') IS NOT NULL",
        &[],
    )?;
    let has_history_schema: bool = row.get(0);
    if has_history_schema {
        return Ok(());
    }

    Err(eyre!(
        "benchmark history schema was not found in database `{dbname}`\nrun `cargo pgrx bench` first"
    ))
}

fn load_recent_report_runs(
    client: &mut Client,
    extname: &str,
    bench_filter: Option<&str>,
) -> eyre::Result<Vec<HistoricalBenchRun>> {
    let bench_filter = bench_filter.map(str::to_string);
    let rows = client.query(
        "WITH primary_estimate AS (
            SELECT DISTINCT ON (benchmark_run_id)
                benchmark_run_id,
                point_estimate_ns
            FROM pgrx_bench.benchmark_estimate
            ORDER BY
                benchmark_run_id,
                CASE
                    WHEN estimate_kind = 'slope' THEN 0
                    WHEN estimate_kind = 'mean' THEN 1
                    ELSE 2
                END,
                estimate_kind
        ),
        ranked_runs AS (
            SELECT
                benchmark_case.bench_name,
                benchmark_run.group_id,
                run_group.group_name,
                benchmark_run.status,
                primary_estimate.point_estimate_ns,
                run_group.profile_name,
                run_group.pg_version_major,
                run_group.cargo_features,
                row_number() OVER (
                    PARTITION BY benchmark_case.bench_name
                    ORDER BY run_group.created_at DESC, benchmark_run.id DESC
                ) AS recency_rank
            FROM pgrx_bench.benchmark_run
            JOIN pgrx_bench.benchmark_case
                ON benchmark_case.id = benchmark_run.case_id
            JOIN pgrx_bench.run_group
                ON run_group.id = benchmark_run.group_id
            LEFT JOIN primary_estimate
                ON primary_estimate.benchmark_run_id = benchmark_run.id
            WHERE run_group.extname = $1
              AND ($2::text IS NULL OR benchmark_case.bench_name LIKE '%' || $2 || '%')
        )
        SELECT
            bench_name,
            group_id,
            group_name,
            status,
            point_estimate_ns,
            profile_name,
            pg_version_major,
            cargo_features
        FROM ranked_runs
        WHERE recency_rank <= $3
        ORDER BY bench_name, recency_rank",
        &[&extname, &bench_filter, &REPORT_HISTORY_LIMIT],
    )?;

    rows.into_iter().map(decode_historical_bench_run).collect()
}

fn load_report_baselines(
    client: &mut Client,
    extname: &str,
    bench_filter: Option<&str>,
) -> eyre::Result<BTreeMap<String, HistoricalBenchBaseline>> {
    let bench_filter = bench_filter.map(str::to_string);
    let rows = client.query(
        "WITH primary_estimate AS (
            SELECT DISTINCT ON (benchmark_run_id)
                benchmark_run_id,
                point_estimate_ns
            FROM pgrx_bench.benchmark_estimate
            ORDER BY
                benchmark_run_id,
                CASE
                    WHEN estimate_kind = 'slope' THEN 0
                    WHEN estimate_kind = 'mean' THEN 1
                    ELSE 2
                END,
                estimate_kind
        ),
        ranked_baselines AS (
            SELECT
                benchmark_case.bench_name,
                benchmark_run.group_id,
                run_group.group_name,
                primary_estimate.point_estimate_ns,
                run_group.profile_name,
                run_group.pg_version_major,
                run_group.cargo_features,
                row_number() OVER (
                    PARTITION BY benchmark_case.bench_name
                    ORDER BY run_group.created_at, benchmark_run.id
                ) AS baseline_rank
            FROM pgrx_bench.benchmark_run
            JOIN pgrx_bench.benchmark_case
                ON benchmark_case.id = benchmark_run.case_id
            JOIN pgrx_bench.run_group
                ON run_group.id = benchmark_run.group_id
            JOIN primary_estimate
                ON primary_estimate.benchmark_run_id = benchmark_run.id
            WHERE run_group.extname = $1
              AND benchmark_run.status = 'ok'
              AND ($2::text IS NULL OR benchmark_case.bench_name LIKE '%' || $2 || '%')
        )
        SELECT
            bench_name,
            group_id,
            group_name,
            point_estimate_ns,
            profile_name,
            pg_version_major,
            cargo_features
        FROM ranked_baselines
        WHERE baseline_rank = 1
        ORDER BY bench_name",
        &[&extname, &bench_filter],
    )?;

    let mut baselines = BTreeMap::new();
    for row in rows {
        let baseline = decode_historical_bench_baseline(row)?;
        baselines.insert(baseline.bench_name.clone(), baseline);
    }
    Ok(baselines)
}

fn decode_historical_bench_run(row: postgres::Row) -> eyre::Result<HistoricalBenchRun> {
    let status: String = row.get(3);
    Ok(HistoricalBenchRun {
        bench_name: row.get(0),
        group: HistoricalGroupMetadata {
            group_id: row.get(1),
            group_name: row.get(2),
            profile_name: row.get(5),
            pg_version_major: row.get(6),
            cargo_features: row.get(7),
        },
        status: parse_bench_status(&status)?,
        point_estimate_ns: row.get(4),
    })
}

fn decode_historical_bench_baseline(row: postgres::Row) -> eyre::Result<HistoricalBenchBaseline> {
    let point_estimate_ns: Option<f64> = row.get(3);
    let point_estimate_ns =
        point_estimate_ns.ok_or_else(|| eyre!("baseline row is missing a primary estimate"))?;
    Ok(HistoricalBenchBaseline {
        bench_name: row.get(0),
        group: HistoricalGroupMetadata {
            group_id: row.get(1),
            group_name: row.get(2),
            profile_name: row.get(4),
            pg_version_major: row.get(5),
            cargo_features: row.get(6),
        },
        point_estimate_ns,
    })
}

fn parse_bench_status(value: &str) -> eyre::Result<BenchStatus> {
    match value {
        "ok" => Ok(BenchStatus::Ok),
        "failed" => Ok(BenchStatus::Failed),
        other => Err(eyre!("unexpected benchmark status `{other}`")),
    }
}

fn load_nondefault_settings_for_groups(
    client: &mut Client,
    group_ids: impl IntoIterator<Item = Uuid>,
) -> eyre::Result<BTreeMap<Uuid, GroupSettingsSnapshot>> {
    let mut settings_by_group = BTreeMap::new();
    for group_id in group_ids {
        if settings_by_group.contains_key(&group_id) {
            continue;
        }

        let rows = client.query(
            "SELECT name, setting, unit
             FROM pgrx_bench.run_group_pg_setting
             WHERE group_id = $1
               AND (
                   source IS DISTINCT FROM 'default'
                   OR sourcefile IS NOT NULL
                   OR setting IS DISTINCT FROM boot_val
               )
             ORDER BY name",
            &[&group_id],
        )?;
        let mut snapshot = GroupSettingsSnapshot::new();
        for row in rows {
            let name: String = row.get(0);
            snapshot.insert(name, NondefaultSettingValue { setting: row.get(1), unit: row.get(2) });
        }
        settings_by_group.insert(group_id, snapshot);
    }

    Ok(settings_by_group)
}

fn build_report_sections(
    rows: Vec<HistoricalBenchRun>,
    baselines: BTreeMap<String, HistoricalBenchBaseline>,
    settings_by_group: &BTreeMap<Uuid, GroupSettingsSnapshot>,
) -> Vec<BenchHistorySection> {
    let mut grouped_rows = BTreeMap::<String, Vec<HistoricalBenchRun>>::new();
    for row in rows {
        grouped_rows.entry(row.bench_name.clone()).or_default().push(row);
    }

    grouped_rows
        .into_iter()
        .map(|(bench_name, rows)| {
            let baseline = baselines.get(&bench_name).cloned();
            let failed_runs_omitted =
                rows.iter().filter(|row| row.status == BenchStatus::Failed).count();
            let incomplete_runs_omitted = rows
                .iter()
                .filter(|row| row.status == BenchStatus::Ok && row.point_estimate_ns.is_none())
                .count();

            let mut drift_categories = BTreeSet::new();
            let displayed_runs = rows
                .into_iter()
                .rev()
                .filter_map(|row| {
                    if row.status != BenchStatus::Ok {
                        return None;
                    }
                    let point_estimate_ns = row.point_estimate_ns?;
                    let categories = baseline
                        .as_ref()
                        .map(|baseline| drift_categories_for_run(baseline, &row, settings_by_group))
                        .unwrap_or_default();
                    drift_categories.extend(categories.iter().copied());
                    Some(DisplayedHistoryRun {
                        group_name: row.group.group_name,
                        point_estimate_ns,
                        delta_pct: baseline.as_ref().and_then(|baseline| {
                            percent_change_from_baseline(
                                point_estimate_ns,
                                baseline.point_estimate_ns,
                            )
                        }),
                        drifted: !categories.is_empty(),
                        is_baseline: baseline
                            .as_ref()
                            .is_some_and(|baseline| baseline.group.group_id == row.group.group_id),
                    })
                })
                .collect();

            BenchHistorySection {
                bench_name,
                baseline,
                displayed_runs,
                failed_runs_omitted,
                incomplete_runs_omitted,
                drift_categories,
            }
        })
        .collect()
}

fn drift_categories_for_run(
    baseline: &HistoricalBenchBaseline,
    row: &HistoricalBenchRun,
    settings_by_group: &BTreeMap<Uuid, GroupSettingsSnapshot>,
) -> BTreeSet<&'static str> {
    let mut categories = BTreeSet::new();
    if baseline.group.profile_name != row.group.profile_name {
        categories.insert("profile");
    }
    if baseline.group.pg_version_major != row.group.pg_version_major {
        categories.insert("postgres version");
    }
    if cargo_feature_set(&baseline.group.cargo_features)
        != cargo_feature_set(&row.group.cargo_features)
    {
        categories.insert("cargo features");
    }

    let baseline_settings = settings_by_group.get(&baseline.group.group_id);
    let row_settings = settings_by_group.get(&row.group.group_id);
    if baseline_settings != row_settings {
        categories.insert("pg_settings");
    }

    categories
}

fn cargo_feature_set(features: &[String]) -> BTreeSet<&str> {
    features.iter().map(String::as_str).collect()
}

fn percent_change_from_baseline(value: f64, baseline: f64) -> Option<f64> {
    if baseline == 0.0 {
        return None;
    }

    Some(((value - baseline) / baseline) * 100.0)
}

fn print_history_report(report: &BenchHistoryReport) {
    println!("{}", "Bench history report".bold().green());
    println!("{} {}", "  Database".bold().cyan(), report.dbname.bold().white());
    println!(
        "{} last {} groups per benchmark",
        "     Scope".bold().cyan(),
        REPORT_HISTORY_LIMIT.to_string().bold().white()
    );
    if let Some(bench_filter) = &report.bench_filter {
        println!("{} {}", "    Filter".bold().cyan(), bench_filter.bold().white());
    }
    println!();

    for (index, section) in report.sections.iter().enumerate() {
        print_history_section(section);
        if index + 1 != report.sections.len() {
            println!();
        }
    }
}

fn print_history_section(section: &BenchHistorySection) {
    println!("{}", section.bench_name.bold());

    match &section.baseline {
        Some(baseline) => println!(
            "  {} {} ({})",
            "baseline:".cyan(),
            baseline.group.group_name.bold().white(),
            format_duration_ns(baseline.point_estimate_ns).bold()
        ),
        None => println!(
            "  {}",
            "no successful baseline has been recorded for this benchmark yet".yellow()
        ),
    }

    if section.displayed_runs.is_empty() {
        println!("  {}", "no successful runs to display".yellow());
    } else {
        let max_point_estimate =
            section.displayed_runs.iter().map(|run| run.point_estimate_ns).fold(0.0, f64::max);
        let label_width = history_label_width(&section.displayed_runs);

        for run in &section.displayed_runs {
            let label =
                if run.drifted { format!("{}*", run.group_name) } else { run.group_name.clone() };
            let label = pad_history_label(&label, label_width);
            let bar = format_history_bar(run.point_estimate_ns, max_point_estimate);
            let runtime = format_duration_ns(run.point_estimate_ns);
            let delta = format_history_delta(run.delta_pct, run.is_baseline);

            println!(
                "  {} {} {} {}",
                label.white(),
                colorize_history_bar(&bar, run.delta_pct, run.is_baseline),
                runtime.white(),
                colorize_history_delta(&delta, run.delta_pct, run.is_baseline)
            );
        }
    }

    if !section.drift_categories.is_empty() {
        println!(
            "  {} {}",
            "*".yellow(),
            format!(
                "broad drift vs baseline in: {}",
                section.drift_categories.iter().copied().collect::<Vec<_>>().join(", ")
            )
            .yellow()
        );
    }

    if section.failed_runs_omitted > 0 {
        let unit = if section.failed_runs_omitted == 1 { "run" } else { "runs" };
        println!(
            "  {}",
            format!(
                "{} failed {} omitted from the last {} groups",
                section.failed_runs_omitted, unit, REPORT_HISTORY_LIMIT
            )
            .yellow()
        );
    }

    if section.incomplete_runs_omitted > 0 {
        let unit = if section.incomplete_runs_omitted == 1 { "run" } else { "runs" };
        println!(
            "  {}",
            format!(
                "{} successful {} without a primary estimate omitted",
                section.incomplete_runs_omitted, unit
            )
            .yellow()
        );
    }
}

fn history_label_width(runs: &[DisplayedHistoryRun]) -> usize {
    let widest = runs
        .iter()
        .map(|run| run.group_name.chars().count() + usize::from(run.drifted))
        .max()
        .unwrap_or(0);
    widest.clamp(12, 32)
}

fn pad_history_label(label: &str, width: usize) -> String {
    let shortened = shorten_history_label(label, width);
    format!("{shortened:<width$}")
}

fn shorten_history_label(label: &str, width: usize) -> String {
    let label_len = label.chars().count();
    if label_len <= width {
        return label.to_string();
    }
    if width <= 3 {
        return label.chars().take(width).collect();
    }

    let mut shortened = label.chars().take(width - 3).collect::<String>();
    shortened.push_str("...");
    shortened
}

fn format_history_bar(value: f64, max_value: f64) -> String {
    if value <= 0.0 || max_value <= 0.0 {
        return format!("|{}|", " ".repeat(REPORT_BAR_WIDTH));
    }

    let mut filled = ((value / max_value) * REPORT_BAR_WIDTH as f64).round() as usize;
    filled = filled.clamp(1, REPORT_BAR_WIDTH);
    let empty = REPORT_BAR_WIDTH.saturating_sub(filled);
    format!("|{}{}|", "#".repeat(filled), " ".repeat(empty))
}

fn format_history_delta(delta_pct: Option<f64>, is_baseline: bool) -> String {
    if is_baseline {
        return "(baseline)".to_string();
    }
    match delta_pct {
        Some(delta_pct) => format!("({})", format_percent(delta_pct)),
        None => "(n/a)".to_string(),
    }
}

fn colorize_history_bar(bar: &str, delta_pct: Option<f64>, is_baseline: bool) -> String {
    if is_baseline {
        return bar.cyan().to_string();
    }
    match delta_pct {
        Some(delta_pct) if delta_pct < 0.0 => bar.green().to_string(),
        Some(delta_pct) if delta_pct > 0.0 => bar.red().to_string(),
        _ => bar.white().to_string(),
    }
}

fn colorize_history_delta(delta: &str, delta_pct: Option<f64>, is_baseline: bool) -> String {
    if is_baseline {
        return delta.cyan().to_string();
    }
    match delta_pct {
        Some(delta_pct) if delta_pct < 0.0 => delta.green().to_string(),
        Some(delta_pct) if delta_pct > 0.0 => delta.red().to_string(),
        _ => delta.white().to_string(),
    }
}

fn resolve_compare_group(
    client: &mut Client,
    compare_group_name: Option<&str>,
    profile: &CargoProfile,
    current_group_name: &str,
) -> eyre::Result<Option<ResolvedGroup>> {
    let row = if let Some(compare_group_name) = compare_group_name {
        client
            .query_opt(
                "SELECT id, group_name
                 FROM pgrx_bench.run_group
                 WHERE group_name = $1",
                &[&compare_group_name],
            )?
            .ok_or_else(|| eyre!("comparison group `{compare_group_name}` was not found"))?
    } else {
        match client.query_opt(
            "SELECT id, group_name
             FROM pgrx_bench.run_group
             WHERE status IN ('completed', 'partial')
               AND profile_name = $1
               AND group_name <> $2
             ORDER BY created_at DESC
             LIMIT 1",
            &[&profile.name(), &current_group_name],
        )? {
            Some(row) => row,
            None => return Ok(None),
        }
    };

    Ok(Some(ResolvedGroup { id: row.get(0), group_name: row.get(1) }))
}

fn default_group_name(client: &mut Client, git_commit: Option<&str>) -> eyre::Result<String> {
    let timestamp: String =
        client.query_one("SELECT to_char(clock_timestamp(), 'YYYYMMDD_HH24MISS')", &[])?.get(0);
    let short_hash = git_commit
        .map(|commit| commit.chars().take(7).collect::<String>())
        .unwrap_or_else(|| "nogit".to_string());
    Ok(format!("{timestamp}_{short_hash}"))
}

fn build_benchmark_summary(
    benchmark_run_id: i64,
    current: &BenchResult,
    baseline: Option<&BenchResult>,
    compare_group_name: Option<&str>,
) -> BenchmarkSummaryRow {
    let primary_estimate = primary_estimate_display(current);
    let comparison = compare_group_name
        .map(|compare_group_name| build_change_summary(current, baseline, compare_group_name));

    BenchmarkSummaryRow {
        benchmark_run_id,
        bench_name: current.bench_name.clone(),
        status: current.status.clone(),
        error_text: current.error_text.clone(),
        primary_estimate,
        throughput: current.throughput.clone(),
        slope: estimate_display(current, "slope"),
        mean: estimate_display(current, "mean"),
        std_dev: estimate_display(current, "std_dev"),
        median: estimate_display(current, "median"),
        median_abs_dev: estimate_display(current, "median_abs_dev"),
        comparison,
    }
}

fn primary_estimate_display(payload: &BenchResult) -> Option<EstimateDisplay> {
    estimate_display(payload, "slope").or_else(|| estimate_display(payload, "mean"))
}

fn estimate_display(payload: &BenchResult, estimate_kind: &str) -> Option<EstimateDisplay> {
    payload.estimates.iter().find(|estimate| estimate.estimate_kind == estimate_kind).map(
        |estimate| EstimateDisplay {
            estimate_kind: estimate.estimate_kind.clone(),
            point_estimate_ns: estimate.point_estimate_ns,
            ci_lower_bound_ns: estimate.ci_lower_bound_ns,
            ci_upper_bound_ns: estimate.ci_upper_bound_ns,
            confidence_level: estimate.confidence_level,
            standard_error_ns: estimate.standard_error_ns,
        },
    )
}

fn build_change_summary(
    current: &BenchResult,
    baseline: Option<&BenchResult>,
    compare_group_name: &str,
) -> ChangeSummary {
    if let Some(comparison) = &current.comparison {
        return ChangeSummary {
            baseline_group_name: compare_group_name.to_string(),
            lower_pct: Some(comparison.mean.ci_lower_bound * 100.0),
            point_pct: Some(comparison.mean.point_estimate * 100.0),
            upper_pct: Some(comparison.mean.ci_upper_bound * 100.0),
            p_value: Some(comparison.p_value),
            significance_level: comparison.significance_level,
            noise_threshold: comparison.noise_threshold,
            summary: comparison.summary.clone(),
        };
    }

    let baseline = match baseline {
        Some(baseline) => baseline,
        None => {
            return ChangeSummary {
                baseline_group_name: compare_group_name.to_string(),
                lower_pct: None,
                point_pct: None,
                upper_pct: None,
                p_value: None,
                significance_level: current.criterion_config.significance_level,
                noise_threshold: current.criterion_config.noise_threshold,
                summary: "New benchmark; no baseline comparison available.".to_string(),
            };
        }
    };

    if current.status != BenchStatus::Ok {
        return ChangeSummary {
            baseline_group_name: compare_group_name.to_string(),
            lower_pct: None,
            point_pct: None,
            upper_pct: None,
            p_value: None,
            significance_level: current.criterion_config.significance_level,
            noise_threshold: current.criterion_config.noise_threshold,
            summary: "Comparison unavailable because the current benchmark failed.".to_string(),
        };
    }

    if baseline.status != BenchStatus::Ok {
        return ChangeSummary {
            baseline_group_name: compare_group_name.to_string(),
            lower_pct: None,
            point_pct: None,
            upper_pct: None,
            p_value: None,
            significance_level: current.criterion_config.significance_level,
            noise_threshold: current.criterion_config.noise_threshold,
            summary: "Comparison unavailable because the baseline benchmark did not complete successfully.".to_string(),
        };
    }

    // If we had a persisted baseline and both runs succeeded, the backend should normally have
    // returned Criterion comparison data. Hitting this branch means the raw samples were kept but
    // Criterion did not emit `change/estimates.json`, which is useful to surface explicitly.
    ChangeSummary {
        baseline_group_name: compare_group_name.to_string(),
        lower_pct: None,
        point_pct: None,
        upper_pct: None,
        p_value: None,
        significance_level: current.criterion_config.significance_level,
        noise_threshold: current.criterion_config.noise_threshold,
        summary: "Comparison unavailable because Criterion did not emit comparison output."
            .to_string(),
    }
}

fn bench_status_as_str(status: &BenchStatus) -> &'static str {
    match status {
        BenchStatus::Ok => "ok",
        BenchStatus::Failed => "failed",
    }
}

fn quote_ident(value: &str) -> String {
    format!("\"{}\"", value.replace('"', "\"\""))
}

fn command_output<'a>(
    program: &str,
    args: impl IntoIterator<Item = &'a str>,
) -> eyre::Result<String> {
    let args = args.into_iter().collect::<Vec<_>>();
    let output = Command::new(program)
        .args(&args)
        .output()
        .wrap_err_with(|| format!("failed to run `{}`", format_program_and_args(program, &args)))?;
    if output.status.success() {
        Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
    } else {
        Err(eyre!(
            "command `{}` failed: {}",
            format_program_and_args(program, &args),
            String::from_utf8_lossy(&output.stderr).trim()
        ))
    }
}

fn command_output_in_dir<'a>(
    program: &str,
    args: impl IntoIterator<Item = &'a str>,
    current_dir: &Path,
) -> eyre::Result<String> {
    let args = args.into_iter().collect::<Vec<_>>();
    let output =
        Command::new(program).args(&args).current_dir(current_dir).output().wrap_err_with(
            || format!("failed to run `{}`", format_program_and_args(program, &args)),
        )?;
    if output.status.success() {
        Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
    } else {
        Err(eyre!(
            "command `{}` failed: {}",
            format_program_and_args(program, &args),
            String::from_utf8_lossy(&output.stderr).trim()
        ))
    }
}

fn format_program_and_args(program: &str, args: &[&str]) -> String {
    let mut parts = Vec::with_capacity(args.len() + 1);
    parts.push(program);
    parts.extend_from_slice(args);
    parts.join(" ")
}

fn collect_git_metadata(root: &Path) -> eyre::Result<GitMetadata> {
    let git_commit = command_output_in_dir("git", ["rev-parse", "HEAD"], root).ok();
    let git_branch = command_output_in_dir("git", ["rev-parse", "--abbrev-ref", "HEAD"], root).ok();
    let git_describe =
        command_output_in_dir("git", ["describe", "--always", "--dirty", "--tags"], root).ok();
    let git_dirty =
        command_output_in_dir("git", ["status", "--porcelain", "--untracked-files=no"], root)
            .map(|status| !status.is_empty())
            .unwrap_or(false);

    Ok(GitMetadata { git_commit, git_branch, git_describe, git_dirty })
}

fn print_summary(summary: &BenchSummary) {
    let total = summary.benchmarks.len();
    let failed = summary
        .benchmarks
        .iter()
        .filter(|benchmark| benchmark.status == BenchStatus::Failed)
        .count();
    let succeeded = total.saturating_sub(failed);

    println!("{} {}", "Bench group".bold().green(), summary.group_name.bold().white());
    if let Some(compare_group_name) = &summary.compare_group_name {
        println!("{} {}", "   Compared".bold().cyan(), compare_group_name.bold().white());
    }
    println!(
        "{} {} total, {} ok, {} failed",
        "    Result".bold().cyan(),
        total,
        succeeded.to_string().green(),
        failed.to_string().red()
    );

    if !summary.missing_from_current.is_empty() {
        println!("{}", "Missing From Current Run".bold().cyan());
        for bench_name in &summary.missing_from_current {
            println!("  {}", bench_name);
        }
    }
}

fn print_completed_benchmark(benchmark: &BenchmarkSummaryRow) {
    println!("{}", benchmark.bench_name.bold());

    if benchmark.status == BenchStatus::Failed {
        let message =
            benchmark.error_text.as_deref().unwrap_or("benchmark failed without an error message");
        println!("{}{}", summary_indent(), format!("error:  {message}").red());
        println!();
        return;
    }

    if let Some(primary_estimate) = &benchmark.primary_estimate {
        println!("{}time:   {}", summary_indent(), format_estimate_interval(primary_estimate));
    }

    if let (Some(throughput), Some(primary_estimate)) =
        (&benchmark.throughput, &benchmark.primary_estimate)
    {
        println!(
            "{}thrpt:  {}",
            summary_indent(),
            format_throughput_interval(throughput, primary_estimate)
        );
    }

    if let Some(change) = &benchmark.comparison {
        if let (Some(lower), Some(point), Some(upper)) =
            (change.lower_pct, change.point_pct, change.upper_pct)
        {
            let p_value = change
                .p_value
                .map(|p_value| {
                    let comparator = if p_value < change.significance_level { "<" } else { ">" };
                    format!(" (p = {:.2} {} {:.2})", p_value, comparator, change.significance_level)
                })
                .unwrap_or_default();
            println!(
                "{}change: [{} {} {}]{}",
                summary_indent(),
                format_percent(lower),
                format_percent(point),
                format_percent(upper),
                p_value
            );
        }
        println!("{}{}", summary_indent(), change.summary);
    }

    if let Some(slope) = &benchmark.slope {
        println!("{}slope:  {}", summary_indent(), format_estimate_interval(slope));
    }

    if let Some(mean) = &benchmark.mean {
        if let Some(std_dev) = &benchmark.std_dev {
            println!(
                "{}mean:   {} std. dev. {}",
                summary_indent(),
                format_estimate_interval(mean),
                format_estimate_interval(std_dev)
            );
        } else {
            println!("{}mean:   {}", summary_indent(), format_estimate_interval(mean));
        }
    }

    if let Some(median) = &benchmark.median {
        if let Some(median_abs_dev) = &benchmark.median_abs_dev {
            println!(
                "{}median: {} med. abs. dev. {}",
                summary_indent(),
                format_estimate_interval(median),
                format_estimate_interval(median_abs_dev)
            );
        } else {
            println!("{}median: {}", summary_indent(), format_estimate_interval(median));
        }
    }

    println!();
}

fn print_running_benchmark(benchmark: &DiscoveredBenchmark) {
    println!(
        "{} {} [{}]",
        "     Running".bold().green(),
        benchmark.descriptor.bench_name.bold().white(),
        format_benchmark_settings(&benchmark.descriptor).cyan()
    );
}

fn format_benchmark_settings(descriptor: &BenchDescriptor) -> String {
    format!(
        "transaction={}, setup={}, sample_size={}, warm_up={}ms, measurement={}ms, nresamples={}, noise_threshold={}, significance_level={}",
        descriptor.transaction_mode.as_str(),
        descriptor.setup_function.as_deref().unwrap_or("none"),
        descriptor.criterion_config.sample_size,
        descriptor.criterion_config.warm_up_time_ms,
        descriptor.criterion_config.measurement_time_ms,
        descriptor.criterion_config.nresamples,
        descriptor.criterion_config.noise_threshold,
        descriptor.criterion_config.significance_level,
    )
}

fn summary_indent() -> String {
    format!("{:>28}", "")
}

fn format_estimate_interval(estimate: &EstimateDisplay) -> String {
    match (estimate.ci_lower_bound_ns, estimate.ci_upper_bound_ns) {
        (Some(lower), Some(upper)) => format!(
            "[{} {} {}]",
            format_duration_ns(lower),
            format_duration_ns(estimate.point_estimate_ns),
            format_duration_ns(upper)
        ),
        _ => format_duration_ns(estimate.point_estimate_ns),
    }
}

fn format_duration_ns(value_ns: f64) -> String {
    if value_ns.abs() >= 1_000_000_000.0 {
        format_measurement(value_ns / 1_000_000_000.0, "s")
    } else if value_ns.abs() >= 1_000_000.0 {
        format_measurement(value_ns / 1_000_000.0, "ms")
    } else if value_ns.abs() >= 1_000.0 {
        format_measurement(value_ns / 1_000.0, "us")
    } else if value_ns.abs() >= 1.0 {
        format_measurement(value_ns, "ns")
    } else {
        format_measurement(value_ns * 1_000.0, "ps")
    }
}

fn format_measurement(value: f64, unit: &str) -> String {
    let formatted = format_measurement_value(value);
    if unit.is_empty() {
        trim_trailing_zeroes(formatted)
    } else {
        format!("{} {}", trim_trailing_zeroes(formatted), unit)
    }
}

fn format_measurement_value(value: f64) -> String {
    if value.abs() >= 100.0 {
        format!("{value:.2}")
    } else if value.abs() >= 10.0 {
        format!("{value:.3}")
    } else {
        format!("{value:.4}")
    }
}

fn trim_trailing_zeroes(mut value: String) -> String {
    if value.contains('.') {
        while value.ends_with('0') {
            value.pop();
        }
        if value.ends_with('.') {
            value.push('0');
        }
    }
    value
}

fn format_percent(value: f64) -> String {
    let formatted = if value.abs() >= 100.0 {
        format!("{value:+.2}")
    } else if value.abs() >= 10.0 {
        format!("{value:+.3}")
    } else {
        format!("{value:+.4}")
    };
    format!("{}%", trim_trailing_zeroes(formatted))
}

fn format_throughput_interval(throughput: &BenchThroughput, estimate: &EstimateDisplay) -> String {
    // Criterion records throughput as the amount of work per iteration in benchmark.json and then
    // derives the displayed rate from the chosen time estimate. Mirror that here so the CLI
    // reports the same kind of throughput interval as Criterion instead of echoing the raw count.
    let Some(scale) = throughput_scale(throughput, estimate.point_estimate_ns) else {
        return "invalid throughput".to_string();
    };

    match (estimate.ci_lower_bound_ns, estimate.ci_upper_bound_ns) {
        (Some(lower), Some(upper)) => format!(
            "[{} {} {}]",
            format_throughput_value(throughput, upper, scale),
            format_throughput_value(throughput, estimate.point_estimate_ns, scale),
            format_throughput_value(throughput, lower, scale),
        ),
        _ => format_throughput_value(throughput, estimate.point_estimate_ns, scale),
    }
}

fn format_throughput_value(
    throughput: &BenchThroughput,
    time_ns: f64,
    scale: (f64, &'static str),
) -> String {
    if time_ns <= 0.0 || !time_ns.is_finite() {
        return "invalid throughput".to_string();
    }

    let amount = throughput.value;
    let per_second = amount * (1e9 / time_ns);
    let (denominator, unit) = scale;
    let scaled = per_second / denominator;
    format!("{} {}", format_measurement(scaled, ""), unit)
}

fn throughput_scale(
    throughput: &BenchThroughput,
    typical_time_ns: f64,
) -> Option<(f64, &'static str)> {
    if typical_time_ns <= 0.0 || !typical_time_ns.is_finite() {
        return None;
    }

    let per_second = throughput.value * (1e9 / typical_time_ns);
    Some(match throughput.kind.as_str() {
        "bytes" => choose_binary_throughput_unit(per_second),
        "bytesdecimal" => choose_decimal_throughput_unit(per_second),
        "elements" => choose_element_throughput_unit(per_second),
        _ => (1.0, "ops/s"),
    })
}

fn choose_binary_throughput_unit(per_second: f64) -> (f64, &'static str) {
    if per_second < 1024.0 {
        (1.0, "B/s")
    } else if per_second < 1024.0 * 1024.0 {
        (1024.0, "KiB/s")
    } else if per_second < 1024.0 * 1024.0 * 1024.0 {
        (1024.0 * 1024.0, "MiB/s")
    } else {
        (1024.0 * 1024.0 * 1024.0, "GiB/s")
    }
}

fn choose_decimal_throughput_unit(per_second: f64) -> (f64, &'static str) {
    if per_second < 1000.0 {
        (1.0, "B/s")
    } else if per_second < 1000.0 * 1000.0 {
        (1000.0, "KB/s")
    } else if per_second < 1000.0 * 1000.0 * 1000.0 {
        (1000.0 * 1000.0, "MB/s")
    } else {
        (1000.0 * 1000.0 * 1000.0, "GB/s")
    }
}

fn choose_element_throughput_unit(per_second: f64) -> (f64, &'static str) {
    if per_second < 1000.0 {
        (1.0, "elem/s")
    } else if per_second < 1000.0 * 1000.0 {
        (1000.0, "Kelem/s")
    } else if per_second < 1000.0 * 1000.0 * 1000.0 {
        (1000.0 * 1000.0, "Melem/s")
    } else {
        (1000.0 * 1000.0 * 1000.0, "Gelem/s")
    }
}

#[derive(Debug)]
struct ResolvedGroup {
    id: Uuid,
    group_name: String,
}

#[derive(Debug, Default)]
struct GitMetadata {
    git_commit: Option<String>,
    git_branch: Option<String>,
    git_describe: Option<String>,
    git_dirty: bool,
}

#[derive(Debug, Serialize)]
struct BenchSummary {
    group_name: String,
    compare_group_name: Option<String>,
    benchmarks: Vec<BenchmarkSummaryRow>,
    missing_from_current: Vec<String>,
}

#[derive(Debug)]
struct BenchHistoryReport {
    dbname: String,
    bench_filter: Option<String>,
    sections: Vec<BenchHistorySection>,
}

#[derive(Debug)]
struct BenchHistorySection {
    bench_name: String,
    baseline: Option<HistoricalBenchBaseline>,
    displayed_runs: Vec<DisplayedHistoryRun>,
    failed_runs_omitted: usize,
    incomplete_runs_omitted: usize,
    drift_categories: BTreeSet<&'static str>,
}

#[derive(Debug)]
struct DisplayedHistoryRun {
    group_name: String,
    point_estimate_ns: f64,
    delta_pct: Option<f64>,
    drifted: bool,
    is_baseline: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct HistoricalGroupMetadata {
    group_id: Uuid,
    group_name: String,
    profile_name: String,
    pg_version_major: i32,
    cargo_features: Vec<String>,
}

#[derive(Debug, Clone)]
struct HistoricalBenchRun {
    bench_name: String,
    group: HistoricalGroupMetadata,
    status: BenchStatus,
    point_estimate_ns: Option<f64>,
}

#[derive(Debug, Clone)]
struct HistoricalBenchBaseline {
    bench_name: String,
    group: HistoricalGroupMetadata,
    point_estimate_ns: f64,
}

type GroupSettingsSnapshot = BTreeMap<String, NondefaultSettingValue>;

#[derive(Debug, Clone, PartialEq, Eq)]
struct NondefaultSettingValue {
    setting: Option<String>,
    unit: Option<String>,
}

#[derive(Debug, Serialize)]
struct BenchmarkSummaryRow {
    benchmark_run_id: i64,
    bench_name: String,
    status: BenchStatus,
    error_text: Option<String>,
    primary_estimate: Option<EstimateDisplay>,
    throughput: Option<BenchThroughput>,
    slope: Option<EstimateDisplay>,
    mean: Option<EstimateDisplay>,
    std_dev: Option<EstimateDisplay>,
    median: Option<EstimateDisplay>,
    median_abs_dev: Option<EstimateDisplay>,
    comparison: Option<ChangeSummary>,
}

#[derive(Debug, Serialize)]
struct EstimateDisplay {
    estimate_kind: String,
    point_estimate_ns: f64,
    ci_lower_bound_ns: Option<f64>,
    ci_upper_bound_ns: Option<f64>,
    confidence_level: Option<f64>,
    standard_error_ns: Option<f64>,
}

#[derive(Debug, Serialize)]
struct ChangeSummary {
    baseline_group_name: String,
    lower_pct: Option<f64>,
    point_pct: Option<f64>,
    upper_pct: Option<f64>,
    p_value: Option<f64>,
    significance_level: f64,
    noise_threshold: f64,
    summary: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct DiscoveredBenchmark {
    run_wrapper_name: String,
    descriptor: BenchDescriptor,
}

#[derive(Debug, Clone, Serialize, Deserialize, Eq, PartialEq)]
#[serde(rename_all = "snake_case")]
enum BenchStatus {
    Ok,
    Failed,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BenchResult {
    schema_name: String,
    bench_name: String,
    function_name: String,
    setup_function: Option<String>,
    transaction_mode: BenchTransactionMode,
    source_file: String,
    source_line: u32,
    criterion_config: BenchConfig,
    status: BenchStatus,
    error_text: Option<String>,
    estimates: Vec<BenchEstimate>,
    samples: Vec<BenchSample>,
    throughput: Option<BenchThroughput>,
    #[serde(default)]
    comparison: Option<BenchComparison>,
    #[serde(default)]
    artifacts: Vec<BenchArtifact>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BenchDescriptor {
    schema_name: String,
    bench_name: String,
    function_name: String,
    setup_function: Option<String>,
    transaction_mode: BenchTransactionMode,
    source_file: String,
    source_line: u32,
    criterion_config: BenchConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BenchConfig {
    sample_size: usize,
    measurement_time_ms: u64,
    warm_up_time_ms: u64,
    nresamples: usize,
    noise_threshold: f64,
    significance_level: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BenchEstimate {
    estimate_kind: String,
    point_estimate_ns: f64,
    standard_error_ns: Option<f64>,
    confidence_level: Option<f64>,
    ci_lower_bound_ns: Option<f64>,
    ci_upper_bound_ns: Option<f64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BenchSample {
    sample_index: usize,
    iteration_count: u64,
    elapsed_ns: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BenchThroughput {
    kind: String,
    value: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BenchComparison {
    mean: BenchComparisonEstimate,
    median: BenchComparisonEstimate,
    p_value: f64,
    significance_level: f64,
    noise_threshold: f64,
    summary: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BenchComparisonEstimate {
    estimate_kind: String,
    point_estimate: f64,
    standard_error: f64,
    confidence_level: f64,
    ci_lower_bound: f64,
    ci_upper_bound: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BenchArtifact {
    artifact_kind: String,
    media_type: String,
    payload_json: Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
enum BenchTransactionMode {
    Shared,
    SubtransactionPerBatch,
    SubtransactionPerIteration,
}

impl BenchTransactionMode {
    fn as_str(&self) -> &'static str {
        match self {
            Self::Shared => "shared",
            Self::SubtransactionPerBatch => "subtransaction_per_batch",
            Self::SubtransactionPerIteration => "subtransaction_per_iteration",
        }
    }
}

const PERSISTENT_SCHEMA_SQL_BYTES: &[u8] = include_bytes!("pgrx-bench.sql");

#[cfg(test)]
mod tests {
    use super::*;
    use clap::{Args, Parser, Subcommand};

    #[derive(Parser)]
    #[command(name = "cargo", bin_name = "cargo")]
    struct CargoCli {
        #[command(subcommand)]
        subcommand: CargoSubcommand,
        #[arg(short = 'v', long, action = clap::ArgAction::Count, global = true)]
        verbose: u8,
    }

    #[derive(Subcommand)]
    enum CargoSubcommand {
        Pgrx(PgrxCli),
    }

    #[derive(Args)]
    struct PgrxCli {
        #[command(subcommand)]
        subcommand: PgrxSubcommand,
        #[arg(from_global, action = clap::ArgAction::Count)]
        verbose: u8,
    }

    #[derive(Subcommand)]
    enum PgrxSubcommand {
        Bench(Bench),
    }

    fn parse_bench(args: &[&str]) -> Bench {
        let cli = CargoCli::try_parse_from(args).expect("bench cli should parse");
        match cli.subcommand {
            CargoSubcommand::Pgrx(PgrxCli { subcommand: PgrxSubcommand::Bench(bench), .. }) => {
                bench
            }
        }
    }

    fn estimate_with_interval(point: f64, lower: f64, upper: f64) -> EstimateDisplay {
        EstimateDisplay {
            estimate_kind: "mean".to_string(),
            point_estimate_ns: point,
            ci_lower_bound_ns: Some(lower),
            ci_upper_bound_ns: Some(upper),
            confidence_level: Some(0.95),
            standard_error_ns: None,
        }
    }

    #[test]
    fn throughput_interval_uses_time_estimate_to_compute_rate() {
        let throughput = BenchThroughput { kind: "bytes".to_string(), value: 1024.0 };
        let estimate = estimate_with_interval(1_000.0, 900.0, 1_100.0);

        assert_eq!(
            format_throughput_interval(&throughput, &estimate),
            "[887.78 MiB/s 976.56 MiB/s 1085.07 MiB/s]"
        );
    }

    #[test]
    fn throughput_without_interval_uses_decimal_units_when_requested() {
        let throughput = BenchThroughput { kind: "bytesdecimal".to_string(), value: 5_000.0 };
        let estimate = EstimateDisplay {
            estimate_kind: "mean".to_string(),
            point_estimate_ns: 2_000_000.0,
            ci_lower_bound_ns: None,
            ci_upper_bound_ns: None,
            confidence_level: None,
            standard_error_ns: None,
        };

        assert_eq!(format_throughput_interval(&throughput, &estimate), "2.5 MB/s");
    }

    #[test]
    fn wait_defaults_to_zero() {
        let bench = parse_bench(&["cargo", "pgrx", "bench"]);
        assert_eq!(bench.wait, 0);
    }

    #[test]
    fn wait_parses_from_cli() {
        let bench = parse_bench(&["cargo", "pgrx", "bench", "--wait", "15"]);
        assert_eq!(bench.wait, 15);
    }

    #[test]
    fn report_flag_parses_from_cli() {
        let bench = parse_bench(&["cargo", "pgrx", "bench", "--report"]);
        assert!(bench.report);
    }

    #[test]
    fn report_rejects_run_only_flags() {
        let bench = parse_bench(&["cargo", "pgrx", "bench", "--report", "--wait", "5"]);
        let error =
            bench.validate_report_args().expect_err("wait should be rejected in report mode");
        assert!(error.to_string().contains("--wait"));
    }

    fn history_group(
        id: u128,
        group_name: &str,
        profile_name: &str,
        pg_version_major: i32,
        cargo_features: &[&str],
    ) -> HistoricalGroupMetadata {
        HistoricalGroupMetadata {
            group_id: Uuid::from_u128(id),
            group_name: group_name.to_string(),
            profile_name: profile_name.to_string(),
            pg_version_major,
            cargo_features: cargo_features.iter().map(|feature| (*feature).to_string()).collect(),
        }
    }

    fn successful_history_run(
        bench_name: &str,
        group: &HistoricalGroupMetadata,
        point_estimate_ns: f64,
    ) -> HistoricalBenchRun {
        HistoricalBenchRun {
            bench_name: bench_name.to_string(),
            group: group.clone(),
            status: BenchStatus::Ok,
            point_estimate_ns: Some(point_estimate_ns),
        }
    }

    fn failed_history_run(bench_name: &str, group: &HistoricalGroupMetadata) -> HistoricalBenchRun {
        HistoricalBenchRun {
            bench_name: bench_name.to_string(),
            group: group.clone(),
            status: BenchStatus::Failed,
            point_estimate_ns: None,
        }
    }

    #[test]
    fn report_sections_omit_failed_runs_and_mark_drift() {
        let baseline_group = history_group(1, "baseline", "release", 18, &["pg18", "pg_bench"]);
        let drifted_group = history_group(2, "rewrite", "release", 18, &["pg18", "pg_bench"]);
        let failed_group = history_group(3, "broken", "release", 18, &["pg18", "pg_bench"]);

        let rows = vec![
            successful_history_run("bench_parse_query", &drifted_group, 80.0),
            failed_history_run("bench_parse_query", &failed_group),
            successful_history_run("bench_parse_query", &baseline_group, 100.0),
        ];

        let mut baselines = BTreeMap::new();
        baselines.insert(
            "bench_parse_query".to_string(),
            HistoricalBenchBaseline {
                bench_name: "bench_parse_query".to_string(),
                group: baseline_group.clone(),
                point_estimate_ns: 100.0,
            },
        );

        let mut settings_by_group = BTreeMap::new();
        settings_by_group.insert(baseline_group.group_id, GroupSettingsSnapshot::new());
        settings_by_group.insert(
            drifted_group.group_id,
            BTreeMap::from([(
                "shared_buffers".to_string(),
                NondefaultSettingValue { setting: Some("1GB".to_string()), unit: None },
            )]),
        );
        settings_by_group.insert(failed_group.group_id, GroupSettingsSnapshot::new());

        let sections = build_report_sections(rows, baselines, &settings_by_group);
        let section = sections.first().expect("section should exist");

        assert_eq!(section.failed_runs_omitted, 1);
        assert_eq!(section.displayed_runs.len(), 2);
        assert!(section.drift_categories.contains("pg_settings"));
        assert!(section.displayed_runs[0].is_baseline);
        assert_eq!(section.displayed_runs[1].group_name, "rewrite");
        assert_eq!(section.displayed_runs[1].delta_pct, Some(-20.0));
        assert!(section.displayed_runs[1].drifted);
    }

    #[test]
    fn format_wait_duration_uses_singular_and_plural_units() {
        assert_eq!(format_wait_duration(1), "1 second");
        assert_eq!(format_wait_duration(2), "2 seconds");
    }
}

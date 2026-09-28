--LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
--LICENSE
--LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
--LICENSE
--LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
--LICENSE
--LICENSE All rights reserved.
--LICENSE
--LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

CREATE SCHEMA IF NOT EXISTS pgrx_bench;

CREATE TABLE IF NOT EXISTS pgrx_bench.run_group (
    id uuid PRIMARY KEY,
    group_name text NOT NULL UNIQUE,
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    completed_at timestamptz,
    status text NOT NULL,
    compare_group_id uuid REFERENCES pgrx_bench.run_group(id),
    extname text NOT NULL,
    extversion text,
    pg_version_major integer NOT NULL,
    profile_name text NOT NULL,
    cargo_features text[] NOT NULL DEFAULT ARRAY[]::text[],
    command_line text NOT NULL,
    os text,
    arch text,
    rustc_version text,
    cargo_version text,
    pgrx_version text,
    cargo_pgrx_version text,
    git_commit text,
    git_branch text,
    git_dirty boolean NOT NULL DEFAULT false,
    git_describe text,
    extra_metadata jsonb NOT NULL DEFAULT '{}'::jsonb
);

CREATE TABLE IF NOT EXISTS pgrx_bench.run_group_pg_setting (
    group_id uuid NOT NULL REFERENCES pgrx_bench.run_group(id) ON DELETE CASCADE,
    name text NOT NULL,
    setting text,
    unit text,
    source text,
    sourcefile text,
    sourceline integer,
    boot_val text,
    reset_val text,
    pending_restart boolean,
    PRIMARY KEY (group_id, name)
);

CREATE TABLE IF NOT EXISTS pgrx_bench.benchmark_case (
    id bigserial PRIMARY KEY,
    schema_name text NOT NULL,
    bench_name text NOT NULL,
    function_name text NOT NULL,
    setup_function text,
    transaction_mode text NOT NULL,
    source_file text,
    source_line integer,
    UNIQUE (schema_name, bench_name)
);

CREATE TABLE IF NOT EXISTS pgrx_bench.benchmark_run (
    id bigserial PRIMARY KEY,
    group_id uuid NOT NULL REFERENCES pgrx_bench.run_group(id) ON DELETE CASCADE,
    case_id bigint NOT NULL REFERENCES pgrx_bench.benchmark_case(id),
    status text NOT NULL,
    error_text text,
    started_at timestamptz NOT NULL,
    finished_at timestamptz,
    criterion_config jsonb NOT NULL,
    raw_result jsonb NOT NULL,
    UNIQUE (group_id, case_id)
);

CREATE TABLE IF NOT EXISTS pgrx_bench.benchmark_estimate (
    benchmark_run_id bigint NOT NULL REFERENCES pgrx_bench.benchmark_run(id) ON DELETE CASCADE,
    estimate_kind text NOT NULL,
    point_estimate_ns double precision NOT NULL,
    standard_error_ns double precision,
    confidence_level double precision,
    ci_lower_bound_ns double precision,
    ci_upper_bound_ns double precision,
    PRIMARY KEY (benchmark_run_id, estimate_kind)
);

CREATE TABLE IF NOT EXISTS pgrx_bench.benchmark_sample (
    benchmark_run_id bigint NOT NULL REFERENCES pgrx_bench.benchmark_run(id) ON DELETE CASCADE,
    sample_index integer NOT NULL,
    iteration_count bigint NOT NULL,
    elapsed_ns double precision NOT NULL,
    PRIMARY KEY (benchmark_run_id, sample_index)
);

CREATE TABLE IF NOT EXISTS pgrx_bench.benchmark_throughput (
    benchmark_run_id bigint PRIMARY KEY REFERENCES pgrx_bench.benchmark_run(id) ON DELETE CASCADE,
    kind text NOT NULL,
    value double precision NOT NULL
);

CREATE TABLE IF NOT EXISTS pgrx_bench.artifact (
    id bigserial PRIMARY KEY,
    benchmark_run_id bigint NOT NULL REFERENCES pgrx_bench.benchmark_run(id) ON DELETE CASCADE,
    artifact_kind text NOT NULL,
    media_type text NOT NULL,
    payload bytea,
    payload_json jsonb,
    metadata jsonb NOT NULL DEFAULT '{}'::jsonb
);

CREATE OR REPLACE VIEW pgrx_bench.v_run_group_summary AS
SELECT
    id,
    group_name,
    created_at,
    completed_at,
    status,
    compare_group_id,
    extname,
    extversion,
    pg_version_major,
    profile_name,
    git_commit,
    git_branch,
    git_dirty,
    git_describe
FROM pgrx_bench.run_group;

CREATE OR REPLACE VIEW pgrx_bench.v_run_group_nondefault_settings AS
SELECT *
FROM pgrx_bench.run_group_pg_setting
WHERE source IS DISTINCT FROM 'default'
   OR sourcefile IS NOT NULL
   OR setting IS DISTINCT FROM boot_val;

CREATE OR REPLACE VIEW pgrx_bench.v_primary_estimate AS
SELECT DISTINCT ON (benchmark_run_id)
    benchmark_run_id,
    estimate_kind,
    point_estimate_ns,
    standard_error_ns,
    confidence_level,
    ci_lower_bound_ns,
    ci_upper_bound_ns
FROM pgrx_bench.benchmark_estimate
ORDER BY
    benchmark_run_id,
    CASE
        WHEN estimate_kind = 'slope' THEN 0
        WHEN estimate_kind = 'mean' THEN 1
        ELSE 2
    END,
    estimate_kind;

CREATE OR REPLACE VIEW pgrx_bench.v_group_results AS
SELECT
    benchmark_run.id AS benchmark_run_id,
    benchmark_run.group_id,
    benchmark_run.case_id,
    benchmark_run.status,
    benchmark_case.schema_name,
    benchmark_case.bench_name,
    benchmark_case.function_name,
    benchmark_case.setup_function,
    benchmark_case.transaction_mode,
    benchmark_case.source_file,
    benchmark_case.source_line,
    primary_estimate.estimate_kind AS primary_estimate_kind,
    primary_estimate.point_estimate_ns
FROM pgrx_bench.benchmark_run
JOIN pgrx_bench.benchmark_case ON benchmark_case.id = benchmark_run.case_id
LEFT JOIN pgrx_bench.v_primary_estimate AS primary_estimate
    ON primary_estimate.benchmark_run_id = benchmark_run.id;

CREATE OR REPLACE VIEW pgrx_bench.v_default_comparison AS
SELECT
    current_group.id AS group_id,
    current_group.group_name,
    current_group.compare_group_id,
    compare_group.group_name AS compare_group_name,
    cases.case_id,
    COALESCE(current_result.schema_name, baseline_result.schema_name) AS schema_name,
    COALESCE(current_result.bench_name, baseline_result.bench_name) AS bench_name,
    current_result.point_estimate_ns AS current_point_estimate_ns,
    baseline_result.point_estimate_ns AS baseline_point_estimate_ns,
    CASE
        WHEN current_result.point_estimate_ns IS NOT NULL
         AND baseline_result.point_estimate_ns IS NOT NULL
         AND current_result.status = 'ok'
         AND baseline_result.status = 'ok'
         AND baseline_result.point_estimate_ns <> 0
        THEN ((current_result.point_estimate_ns - baseline_result.point_estimate_ns)
              / baseline_result.point_estimate_ns) * 100.0
    END AS delta_pct,
    CASE
        WHEN baseline_result.case_id IS NULL THEN 'new'
        WHEN current_result.case_id IS NULL THEN 'missing'
        -- Failed or estimate-less runs should never be silently collapsed into "unchanged",
        -- otherwise SQL consumers can mistake a broken benchmark for a successful no-op.
        WHEN current_result.status IS DISTINCT FROM 'ok' THEN 'failed_current'
        WHEN baseline_result.status IS DISTINCT FROM 'ok' THEN 'failed_baseline'
        WHEN current_result.point_estimate_ns IS NULL OR baseline_result.point_estimate_ns IS NULL
            THEN 'unavailable'
        WHEN current_result.point_estimate_ns < baseline_result.point_estimate_ns THEN 'faster'
        WHEN current_result.point_estimate_ns > baseline_result.point_estimate_ns THEN 'slower'
        ELSE 'unchanged'
    END AS comparison_status
FROM pgrx_bench.run_group AS current_group
LEFT JOIN pgrx_bench.run_group AS compare_group ON compare_group.id = current_group.compare_group_id
LEFT JOIN LATERAL (
    SELECT case_id
    FROM pgrx_bench.v_group_results
    WHERE group_id = current_group.id
    UNION
    SELECT case_id
    FROM pgrx_bench.v_group_results
    WHERE group_id = current_group.compare_group_id
) AS cases ON TRUE
LEFT JOIN pgrx_bench.v_group_results AS current_result
    ON current_result.group_id = current_group.id
   AND current_result.case_id = cases.case_id
LEFT JOIN pgrx_bench.v_group_results AS baseline_result
    ON baseline_result.group_id = current_group.compare_group_id
   AND baseline_result.case_id = cases.case_id;

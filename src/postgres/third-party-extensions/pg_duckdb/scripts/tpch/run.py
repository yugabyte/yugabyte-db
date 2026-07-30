#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.8"
# dependencies = [
#     "pandas",
#     "matplotlib",
#     "psycopg[binary]",
# ]
# ///
import argparse
import csv
import os
import shlex
import subprocess
import sys
import time
from contextlib import contextmanager
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
import psycopg


def eprint(*args, **kwargs):
    """eprint prints to stderr"""
    print(*args, file=sys.stderr, **kwargs)


@contextmanager
def cd(path: Path | str):
    """Sets the cwd within the context"""
    origin = Path().resolve()
    try:
        eprint(f"+ cd {shlex.quote(str(path))}")
        os.chdir(path)
        yield
    finally:
        eprint(f"+ cd {shlex.quote(str(origin))}")
        os.chdir(origin)


def run(command, *args, check=True, shell=None, silent=False, **kwargs):
    """Runs the given command and prints it to stderr"""
    if shell is None:
        shell = isinstance(command, str)

    if not shell:
        command = list(map(str, command))

    if not silent:
        if shell:
            eprint(f"+ {command}")
        else:
            eprint(f"+ {shlex.join(command)}")
    if silent:
        kwargs.setdefault("stdout", subprocess.DEVNULL)
    return subprocess.run(command, *args, check=check, shell=shell, **kwargs)


def capture(command, *args, stdout=subprocess.PIPE, encoding="utf-8", **kwargs):
    return run(
        command, *args, stdout=stdout, encoding=encoding, **kwargs
    ).stdout.removesuffix("\n")


def parse_timeout(timeout_str):
    """Parse timeout string with suffix (e.g., '5m', '300s') and return seconds"""
    if timeout_str.endswith("m"):
        return int(timeout_str[:-1]) * 60
    elif timeout_str.endswith("s"):
        return int(timeout_str[:-1])
    else:
        raise ValueError(
            f"Timeout must end with 's' (seconds) or 'm' (minutes): {timeout_str}"
        )


def get_pg_credentials(username=None, password=None):
    """Get consistent PostgreSQL credentials with CLI args taking precedence"""
    final_username = username or os.environ.get("PGUSER", "postgres")
    final_password = password or os.environ.get("PGPASSWORD", "")
    return final_username, final_password


def build_pg_env(username, password):
    """Build environment variables for PostgreSQL authentication"""
    env = {**os.environ}
    if username:
        env["PGUSER"] = username
    if password:
        env["PGPASSWORD"] = password
    return env


def create_tables(
    database_name, schema_name, username, password, no_indexes=False, pk_only=False
):
    """Create tables using SQL files"""
    pg_env = build_pg_env(username, password)

    # Create schema if it doesn't exist
    run(
        [
            "psql",
            "-d",
            database_name,
            "-c",
            f'CREATE SCHEMA IF NOT EXISTS "{schema_name}"',
        ],
        env=pg_env,
    )

    # Determine which schema file to use
    if no_indexes:
        sql_file = "schemas/create-schema-no-indexes.sql"
        eprint("Using no-indexes schema")
    elif pk_only:
        sql_file = "schemas/create-schema-pk.sql"
        eprint("Using primary keys only schema")
    else:
        sql_file = "schemas/create-schema.sql"
        eprint("Using full schema with indexes")

    if not os.path.exists(sql_file):
        eprint(f"ERROR: Schema file {sql_file} not found")
        sys.exit(1)

    eprint(f"Executing schema file: {sql_file}")
    run(
        [
            "psql",
            "-d",
            database_name,
            "-v",
            "ON_ERROR_STOP=1",
            "-c",
            f"SET search_path = '{schema_name}'",
            "-f",
            sql_file,
        ],
        env=pg_env,
    )


def execute_tpch_queries(
    database_name,
    schema_name,
    username,
    password,
    queries_dir,
    engine="pg",
    host="localhost",
    port=5432,
    timeout_seconds=300,
    disable_nested_loop_join=False,
    pg_work_mem=None,
    selected_queries=None,
):
    """Execute TPC-H queries using psycopg and time them with configurable timeout"""
    results = []

    # Find all query files
    all_query_files = sorted(
        [f for f in os.listdir(queries_dir) if f.startswith("q") and f.endswith(".sql")]
    )

    if not all_query_files:
        eprint(f"ERROR: No query files found in {queries_dir}")
        sys.exit(1)

    # Filter query files based on selected_queries if provided
    if selected_queries:
        query_files = []
        for query_num in selected_queries:
            # Pad single digits with leading zero to match file naming (q01.sql, q02.sql, etc.)
            query_file = f"q{query_num.zfill(2)}.sql"
            if query_file in all_query_files:
                query_files.append(query_file)
            else:
                eprint(f"WARNING: Query file {query_file} not found in {queries_dir}")

        if not query_files:
            eprint("ERROR: No valid query files found from the specified queries")
            sys.exit(1)

        query_files.sort()
        eprint(
            f"Running {len(query_files)} selected queries: {', '.join([f.replace('.sql', '').upper() for f in query_files])}"
        )
    else:
        query_files = all_query_files
        eprint(f"Found {len(query_files)} query files")

    # Build connection string
    conn_params = {
        "host": host,
        "port": port,
        "dbname": database_name,
        "user": username,
    }
    if password:
        conn_params["password"] = password

    try:
        # Connect to database
        eprint(f"Connecting to {database_name} as {username}@{host}:{port}")
        if timeout_seconds >= 60:
            eprint(
                f"Query timeout set to {timeout_seconds // 60}m {timeout_seconds % 60}s"
            )
        else:
            eprint(f"Query timeout set to {timeout_seconds}s")
        with psycopg.connect(**conn_params, autocommit=True) as conn:
            with conn.cursor() as cursor:
                # Set search path (can't use parameters for schema names)
                cursor.execute(f"SET search_path = '{schema_name}'")
                # Set statement timeout
                cursor.execute(f"SET statement_timeout = '{timeout_seconds}s'")

                # Configure PostgreSQL settings for better performance
                if pg_work_mem:
                    cursor.execute(f"SET work_mem = '{pg_work_mem}'")

                # Configure nested loop joins for PostgreSQL
                if disable_nested_loop_join:
                    cursor.execute("SET enable_nestloop = off")
                else:
                    cursor.execute("SET enable_nestloop = on")

                if engine == "duckdb":
                    cursor.execute("SET duckdb.force_execution = true")
                # Warm-up the connection
                cursor.execute("SELECT 1 FROM customer LIMIT 0")

                for query_file in query_files:
                    query_path = os.path.join(queries_dir, query_file)
                    query_name = query_file.replace(".sql", "").upper()

                    eprint(f"Executing {query_name}...")

                    try:
                        # Read query from file
                        with open(query_path, "r") as f:
                            query = f.read().strip()

                        # Skip empty queries
                        if not query:
                            eprint(f"WARNING: {query_name} is empty, skipping")
                            continue

                        # Time the query execution
                        start_time = time.time()
                        cursor.execute(query)

                        # Fetch all results to ensure query completion
                        try:
                            rows = cursor.fetchall()
                            row_count = len(rows) if rows else 0
                        except psycopg.ProgrammingError:
                            # Query didn't return results (e.g., DDL)
                            row_count = cursor.rowcount if cursor.rowcount >= 0 else 0

                        end_time = time.time()

                        execution_time_ms = (end_time - start_time) * 1000
                        eprint(
                            f"{query_name}: {execution_time_ms:.1f} ms ({row_count} rows)"
                        )

                        results.append(
                            {
                                "Transaction Name": query_name,
                                "Latency (microseconds)": execution_time_ms
                                * 1000,  # Convert to microseconds for compatibility
                                "Latency (ms)": execution_time_ms,
                                "Rows": row_count,
                            }
                        )

                    except Exception as e:
                        if isinstance(
                            e, psycopg.errors.QueryCanceled
                        ) or "Query cancelled" in str(e):
                            timeout_display = (
                                f"{timeout_seconds // 60}m {timeout_seconds % 60}s"
                                if timeout_seconds >= 60
                                else f"{timeout_seconds}s"
                            )
                            eprint(
                                f"TIMEOUT executing {query_name} (exceeded {timeout_display}): {e}"
                            )
                            # Add timed out query with timeout latency
                            results.append(
                                {
                                    "Transaction Name": query_name,
                                    "Latency (microseconds)": timeout_seconds * 1000000,
                                    "Latency (ms)": timeout_seconds * 1000,
                                    "Rows": 0,
                                }
                            )
                        else:
                            eprint(f"ERROR executing {query_name}: {e}")
                            # Add failed query with very high latency
                            results.append(
                                {
                                    "Transaction Name": query_name,
                                    "Latency (microseconds)": 999999999,
                                    "Latency (ms)": 999999.999,
                                    "Rows": 0,
                                }
                            )

    except psycopg.Error as e:
        eprint(f"Database connection error: {e}")
        sys.exit(1)

    return results


def save_results(results, output_file):
    """Save results to CSV file"""
    os.makedirs(os.path.dirname(output_file), exist_ok=True)

    with open(output_file, "w", newline="") as csvfile:
        fieldnames = [
            "Transaction Name",
            "Latency (microseconds)",
            "Latency (ms)",
            "Rows",
        ]
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(results)

    eprint(f"Results saved to {output_file}")
    return output_file


def parse_results(csv_file):
    """Parse benchmark results from CSV file"""
    df = pd.read_csv(csv_file)
    # Ensure we have the ms column
    if "Latency (ms)" not in df.columns:
        df["Latency (ms)"] = df["Latency (microseconds)"] / 1000
    return df


def create_comparison_chart(result_files, output_file="comparison.png"):
    """Create a bar chart comparing query runtimes across multiple configurations"""
    # Parse all result files
    dataframes = {}
    for label, file_path in result_files.items():
        dataframes[label] = parse_results(file_path)

    # Merge all dataframes on Transaction Name
    comparison = None
    for label, df in dataframes.items():
        df_subset = df[["Transaction Name", "Latency (ms)"]].rename(
            columns={"Latency (ms)": f"Latency (ms)_{label}"}
        )
        if comparison is None:
            comparison = df_subset
        else:
            comparison = pd.merge(comparison, df_subset, on="Transaction Name")

    # Find PostgreSQL baseline (prefer any label containing "PostgreSQL")
    postgres_label = None
    for label in dataframes.keys():
        if "PostgreSQL" in label:
            postgres_label = label
            break

    # If no PostgreSQL found, use first label as baseline
    if postgres_label is None:
        postgres_label = list(dataframes.keys())[0]
        eprint(
            f"Warning: No PostgreSQL label found, using {postgres_label} as baseline"
        )

    # Check if we need log scale - if max/min ratio > 100, use log scale
    all_values = []
    for label in dataframes.keys():
        col_name = f"Latency (ms)_{label}"
        all_values.extend(comparison[col_name].values)

    max_val = max(all_values)
    min_val = min(v for v in all_values if v > 0)  # Exclude zeros
    use_log_scale = (max_val / min_val) > 100

    if use_log_scale:
        eprint(
            f"Using log scale due to large range: {max_val:.0f}ms / {min_val:.0f}ms = {max_val / min_val:.1f}x"
        )

    # Create speedup chart
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(20, 8))

    x = range(len(comparison))
    width = 0.8 / len(dataframes)
    colors = ["#336699", "#ff9933", "#66cc66", "#cc6666", "#9966cc"]
    labels = list(dataframes.keys())

    # Plot 1: Absolute times (linear or log scale based on data)
    bars1 = []
    for i, label in enumerate(labels):
        col_name = f"Latency (ms)_{label}"
        bars1.append(
            ax1.bar(
                [pos + (i - len(labels) / 2 + 0.5) * width for pos in x],
                comparison[col_name],
                width,
                label=label,
                alpha=0.8,
                color=colors[i % len(colors)],
            )
        )

    ax1.set_xlabel("Query")
    if use_log_scale:
        ax1.set_ylabel("Latency (ms) - Log Scale")
        ax1.set_title("TPC-H Query Performance (Absolute Times)")
        ax1.set_yscale("log")
    else:
        ax1.set_ylabel("Latency (ms)")
        ax1.set_title("TPC-H Query Performance (Absolute Times)")

    ax1.set_xticks(x)
    ax1.set_xticklabels(comparison["Transaction Name"], rotation=45)
    ax1.legend()
    ax1.grid(axis="y", alpha=0.3)

    # Plot 2: Speedup relative to PostgreSQL with symmetric scaling
    postgres_col = f"Latency (ms)_{postgres_label}"

    for i, label in enumerate(labels):
        if label == postgres_label:
            continue  # Skip baseline

        col_name = f"Latency (ms)_{label}"
        # Calculate raw speedup: postgres_time / other_time
        raw_speedups = comparison[postgres_col] / comparison[col_name]

        # Transform to symmetric scale: speedup >= 1 stays as-is, speedup < 1 becomes -1/speedup
        symmetric_speedups = [s if s >= 1.0 else -1 / s for s in raw_speedups]

        # Use different colors for speedup vs slowdown
        bar_colors = ["#22cc22" if s >= 0 else "#cc2222" for s in symmetric_speedups]

        bars = ax2.bar(
            [pos + (i - len(labels) / 2 + 0.5) * width for pos in x],
            symmetric_speedups,
            width,
            label=f"{label} vs {postgres_label}",
            alpha=0.8,
            color=bar_colors,
        )

        # Add speedup labels on bars
        for j, (bar, raw_speedup) in enumerate(zip(bars, raw_speedups)):
            height = bar.get_height()

            if raw_speedup >= 1.0:
                label_text = f"{raw_speedup:.1f}x"
            else:
                label_text = f"{1 / raw_speedup:.1f}x slower"

            # Position label above positive bars, below negative bars
            y_pos = height + 0.2 if height >= 0 else height - 0.2
            va = "bottom" if height >= 0 else "top"

            ax2.text(
                bar.get_x() + bar.get_width() / 2.0,
                y_pos,
                label_text,
                ha="center",
                va=va,
                fontsize=8,
                rotation=90,
            )

    # Add horizontal line at y=0 (no speedup/slowdown)
    ax2.axhline(y=0, color="black", linestyle="--", alpha=0.7, linewidth=1)
    ax2.set_xlabel("Query")
    ax2.set_ylabel(f"Speedup relative to {postgres_label} (symmetric scale)")
    ax2.set_title(f"TPC-H Query Speedup vs {postgres_label}")
    ax2.set_xticks(x)
    ax2.set_xticklabels(comparison["Transaction Name"], rotation=45)
    ax2.legend()
    ax2.grid(axis="y", alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches="tight")
    plt.show()

    # Print detailed statistics
    print(f"\nDetailed Query Results (baseline: {postgres_label}):")
    print("=" * 80)

    postgres_col = f"Latency (ms)_{postgres_label}"

    for _, row in comparison.iterrows():
        query = row["Transaction Name"]
        postgres_time = row[postgres_col]
        print(f"\n{query}:")
        print(f"  {postgres_label:15}: {postgres_time:8.1f} ms (baseline)")

        for label in labels:
            if label == postgres_label:
                continue
            col_name = f"Latency (ms)_{label}"
            latency = row[col_name]
            speedup = postgres_time / latency

            if speedup >= 1.0:
                print(f"  {label:15}: {latency:8.1f} ms ({speedup:.2f}x faster)")
            else:
                print(f"  {label:15}: {latency:8.1f} ms ({1 / speedup:.2f}x slower)")

    print(f"\nSummary (baseline: {postgres_label}):")
    print("=" * 50)

    postgres_total = comparison[postgres_col].sum()
    print(f"Total {postgres_label} time: {postgres_total:.0f} ms (baseline)")

    for label in labels:
        if label == postgres_label:
            continue
        col_name = f"Latency (ms)_{label}"
        total = comparison[col_name].sum()
        speedup = postgres_total / total

        if speedup >= 1.0:
            print(f"Total {label} time: {total:.0f} ms ({speedup:.2f}x faster overall)")
        else:
            print(
                f"Total {label} time: {total:.0f} ms ({1 / speedup:.2f}x slower overall)"
            )

    eprint(f"Comparison chart saved to {output_file}")


def run_benchmark(args, engine, results_suffix="", timeout_seconds=300):
    """Run a single benchmark iteration"""
    dump_name = f"tpch{args.scale_factor}".replace(".", "")

    # For MotherDuck, use ddb prefix for schema name
    if engine == "motherduck":
        schema_name = args.schema_name or f"ddb${dump_name}"
        database_name = args.motherduck_database or args.database_name
    else:
        schema_name = args.schema_name or dump_name
        database_name = args.database_name

    username, password = get_pg_credentials(args.username, args.password)

    # Skip data generation and loading for MotherDuck
    if engine != "motherduck" and not args.skip_generate and not args.skip_load:
        run(
            [
                "duckdb",
                "-c",
                f"CALL dbgen(sf={args.scale_factor}); EXPORT DATABASE '{dump_name}' (FORMAT CSV, DELIMITER '|')",
            ]
        )

    if engine != "motherduck" and not args.skip_load:
        # Create schema using SQL files
        create_tables(
            database_name,
            schema_name,
            username,
            password,
            args.no_indexes,
            args.pk_only,
        )

        # Load data
        dump_dir = schema_name
        if os.path.exists(dump_dir):
            pg_env = build_pg_env(username, password)
            run(
                [
                    "psql",
                    "-d",
                    database_name,
                    "-v",
                    "ON_ERROR_STOP=1",
                    "-c",
                    f"SET search_path = '{schema_name}'",
                    "-f",
                    "../load-psql.sql",
                ],
                cwd=dump_dir,
                env=pg_env,
            )
        else:
            eprint(f"ERROR: Dump directory {dump_dir} not found")
            sys.exit(1)

    if not args.skip_execute:
        # Parse selected queries if provided
        selected_queries = None
        if args.queries:
            selected_queries = [q.strip() for q in args.queries.split(",")]

        # Execute TPC-H queries directly
        results = execute_tpch_queries(
            database_name,
            schema_name,
            username,
            password,
            args.queries_dir,
            engine,
            args.host,
            args.port,
            timeout_seconds,
            args.disable_nested_loop_join,
            args.pg_work_mem,
            selected_queries,
        )

        # Save results to CSV
        os.makedirs("results", exist_ok=True)
        timestamp = int(time.time())
        result_file = f"results/tpch_{timestamp}{results_suffix}.raw.csv"
        return save_results(results, result_file)

    return None


def get_engine_name(engine):
    """Get display name for engine"""
    if engine == "pg":
        return "PostgreSQL"
    elif engine == "duckdb":
        return "DuckDB"
    elif engine == "motherduck":
        return "MotherDuck"
    return engine


def main():
    parser = argparse.ArgumentParser(
        description="Run TPCH benchmark with multiple engines using direct query execution."
    )
    parser.add_argument(
        "--database-name",
        type=str,
        default="postgres",
        help="Database name to connect to.",
    )
    parser.add_argument(
        "--motherduck-database",
        type=str,
        help="MotherDuck database name (overrides --database-name for MotherDuck).",
    )
    parser.add_argument(
        "--schema-name",
        type=str,
        default=None,
        help="Schema name to export database to.",
    )
    parser.add_argument(
        "--scale-factor",
        type=str,
        default="1",
        help="Scale factor for data generation.",
    )
    parser.add_argument(
        "--username",
        "-U",
        type=str,
        help="PostgreSQL username (overrides PGUSER env var).",
    )
    parser.add_argument(
        "--password",
        type=str,
        help="PostgreSQL password (overrides PGPASSWORD env var).",
    )
    parser.add_argument(
        "--host",
        type=str,
        default="localhost",
        help="PostgreSQL host (default: localhost)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=5432,
        help="PostgreSQL port (default: 5432)",
    )
    parser.add_argument(
        "--queries-dir",
        type=str,
        default="../../third_party/duckdb/extension/tpch/dbgen/queries/",
        help="Directory containing TPC-H query SQL files",
    )
    parser.add_argument(
        "--queries",
        type=str,
        help="Comma-separated list of specific queries to run (e.g., --queries=2,20,22). If not specified, all queries will be run.",
    )
    parser.add_argument(
        "--timeout",
        type=str,
        default="5m",
        help="Query timeout with suffix (e.g., 5m for minutes, 300s for seconds, default: 5m)",
    )

    # Engine selection
    parser.add_argument(
        "--pg-engine", action="store_true", help="Run benchmark with PostgreSQL engine"
    )
    parser.add_argument(
        "--duckdb-engine", action="store_true", help="Run benchmark with DuckDB engine"
    )
    parser.add_argument(
        "--motherduck", action="store_true", help="Run benchmark with MotherDuck"
    )

    # Temperature selection
    parser.add_argument(
        "--cold", action="store_true", help="Run cold benchmark (after data loading)"
    )
    parser.add_argument(
        "--hot", action="store_true", help="Run hot benchmark (reusing loaded data)"
    )

    # Skip options
    parser.add_argument(
        "--skip-generate", action="store_true", help="Skip the data generation step."
    )
    parser.add_argument(
        "--skip-load", action="store_true", help="Skip the data loading step."
    )
    parser.add_argument(
        "--skip-execute", action="store_true", help="Skip the benchmark execution step."
    )

    # Schema options
    parser.add_argument(
        "--no-indexes",
        action="store_true",
        help="Create schema without indexes (uses create-schema-no-indexes.sql)",
    )
    parser.add_argument(
        "--pk-only",
        action="store_true",
        help="Create schema with primary keys only (uses create-schema-pk.sql)",
    )

    # PostgreSQL optimization options
    parser.add_argument(
        "--disable-nested-loop-join",
        action="store_true",
        help="Disable nested loop joins in PostgreSQL (enabled by default)",
    )
    parser.add_argument(
        "--pg-work-mem",
        type=str,
        default=None,
        help="Set PostgreSQL work_mem setting (default: use PostgreSQL default)",
    )

    args = parser.parse_args()

    # Parse timeout early
    try:
        timeout_seconds = parse_timeout(args.timeout)
    except ValueError as e:
        eprint(f"ERROR: {e}")
        sys.exit(1)

    # Validate mutually exclusive flags
    if args.no_indexes and args.pk_only:
        eprint("ERROR: --no-indexes and --pk-only cannot be used together")
        sys.exit(1)

    # Check if queries directory exists
    if not os.path.exists(args.queries_dir):
        eprint(f"ERROR: Queries directory {args.queries_dir} not found")
        sys.exit(1)

    # Default to --cold and --pg-engine if nothing specified
    if not any([args.cold, args.hot]):
        args.cold = True

    if not any([args.pg_engine, args.duckdb_engine, args.motherduck]):
        args.pg_engine = True

    # Build lists of engines and temperatures to test
    engines = []
    if args.pg_engine:
        engines.append("pg")
    if args.duckdb_engine:
        engines.append("duckdb")
    if args.motherduck:
        engines.append("motherduck")

    temperatures = []
    if args.cold:
        temperatures.append("cold")
    if args.hot:
        temperatures.append("hot")

    # Build filename suffix for schema type
    schema_suffix = ""
    if args.no_indexes:
        schema_suffix = "_no_indexes"
    elif args.pk_only:
        schema_suffix = "_pk_only"

    file_prefix = f"tpch{args.scale_factor}".replace(".", "") + schema_suffix

    # Run matrix of benchmarks
    result_files = {}

    for engine in engines:
        for temperature in temperatures:
            engine_name = get_engine_name(engine)
            temp_name = temperature.capitalize()
            label = f"{engine_name} ({temp_name})"

            eprint(f"=== Running {label} benchmark ===")

            # Set up args for this run
            run_args = argparse.Namespace(**vars(args))

            # For hot runs after the first cold run, skip loading
            if temperature == "hot" and len(result_files) > 0:
                run_args.skip_load = True

            # For subsequent engine runs after the first engine, skip generation
            if (
                len([e for e in engines[: engines.index(engine)] if e != "motherduck"])
                > 0
            ):
                run_args.skip_generate = True

            results_suffix = f"_{engine}_{temperature}"

            result_file = run_benchmark(
                run_args, engine, results_suffix, timeout_seconds
            )
            if result_file:
                result_files[label] = result_file
            else:
                eprint(f"ERROR: Could not find result file for {label}")

    # Generate comparison chart if multiple results
    if len(result_files) > 1:
        # Build descriptive filename
        engine_names = "_".join(engines)
        temp_names = "_".join(temperatures)
        output_file = f"{file_prefix}_{engine_names}_{temp_names}_comparison.png"

        create_comparison_chart(result_files, output_file)
    elif len(result_files) == 1:
        eprint("Single benchmark completed successfully")
        # Print results for single run
        result_file = list(result_files.values())[0]
        df = parse_results(result_file)
        print("\nQuery Results:")
        print("=" * 40)
        for _, row in df.iterrows():
            print(f"{row['Transaction Name']:4}: {row['Latency (ms)']:8.1f} ms")
        print(f"\nTotal time: {df['Latency (ms)'].sum():.0f} ms")
    else:
        eprint("ERROR: No benchmark results found")
        sys.exit(1)


if __name__ == "__main__":
    main()

#!/bin/sh
# This uses duckdb to generate a tpcds database in Postgres.
# This script uses psql environment variables from the shell, such as:
# PGUSER, PGPASSWORD, PGHOST, PGPORT, and PGDATABASE

set -eu
scale_factor=${1:-1}
clean_scale_factor=$(echo "$scale_factor" | tr -d ".")
schema_name=${2:-tpcds${clean_scale_factor}}

set -x

# Generate the data within duckdb and export it to CSV
duckdb -c "CALL dsdgen(sf=$scale_factor); EXPORT DATABASE '$schema_name' (FORMAT CSV, DELIMITER '|');"
# Change the load script to use \copy instead of COPY
sed 's/COPY/\\copy/' "$schema_name/load.sql" >"$schema_name/load-psql.sql"
# Load the data into postgres
psql -v ON_ERROR_STOP=1 "options=--search-path=$schema_name" -c "CREATE SCHEMA IF NOT EXISTS $schema_name" -f "$schema_name/schema.sql" -f "$schema_name/load-psql.sql" -c "ANALYZE;"

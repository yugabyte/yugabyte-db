#!/usr/bin/env bash

export PGDATA=/var/lib/postgresql/data/
export PGDATABASE=postgres
export PGUSER=postgres

{
mkdir -p $PGDATA
chown postgres $PGDATA
gosu postgres initdb
gosu postgres pg_ctl start
gosu postgres psql -c "ALTER SYSTEM SET session_preload_libraries = 'anon';"
gosu postgres psql -c "SELECT pg_reload_conf();"
gosu postgres psql -c "CREATE EXTENSION IF NOT EXISTS anon;"
gosu postgres psql -c "CREATE ROLE dump_anon LOGIN;"
gosu postgres psql -c "ALTER ROLE dump_anon SET anon.transparent_dynamic_masking = True;"
gosu postgres psql -c "SECURITY LABEL FOR anon ON ROLE dump_anon IS 'MASKED';"
gosu postgres psql -c "GRANT pg_read_all_data to dump_anon;"

cat | gosu postgres psql
} &> /dev/null

/usr/bin/pg_dump "$@" -U dump_anon --no-security-labels --exclude-extension="anon"

#!/bin/sh

set -e

# Perform all actions as $POSTGRES_USER
export PGUSER="$POSTGRES_USER"

# this is simpler than updating shared_preload_libraries in postgresql.conf
echo "Loading extension for all further sessions"
psql --dbname="postgres" -c "ALTER SYSTEM SET session_preload_libraries = 'anon';"
psql --dbname="postgres" -c "SELECT pg_reload_conf();"

echo "Creating extension inside template1 and postgres databases"
SQL="CREATE EXTENSION IF NOT EXISTS anon CASCADE;"
psql --dbname="template1" -c "$SQL"
psql --dbname="postgres" -c "$SQL"

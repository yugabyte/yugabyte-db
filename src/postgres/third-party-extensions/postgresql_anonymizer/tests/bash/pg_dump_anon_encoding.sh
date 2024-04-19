#!/bin/bash

PSQL=psql
SUPERUSER=postgres
DB_SOURCE=test_12343RDEFSDFSFQCDSFS
DB_TARGET=test_sFSFdvicsdfea232EDEF

# Prepare
createdb --encoding=UTF8 $DB_SOURCE

$PSQL $DB_SOURCE << EOSQL
  CREATE EXTENSION anon CASCADE;
  CREATE TABLE people AS SELECT 'Éléonore' AS firstname;
  SECURITY LABEL FOR anon ON COLUMN people.firstname
  IS 'MASKED WITH VALUE ''Amédée'' ';
EOSQL

# Dump & Restore
./bin/pg_dump_anon.sh --encoding=LATIN9 $DB_SOURCE > /tmp/dump_anon.latin9.sql

file /tmp/dump_anon.latin9.sql

pg_dump --encoding=LATIN9 $DB_SOURCE > /tmp/dump.latin9.sql

file /tmp/dump.latin9.sql

# clean up
dropdb $DB_SOURCE


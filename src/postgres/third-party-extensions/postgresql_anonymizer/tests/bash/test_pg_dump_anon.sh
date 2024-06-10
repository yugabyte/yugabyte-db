#!/bin/bash

PSQL=psql
SUPERUSER=postgres
MASKED_ROLE=test_dnfvdjlsfjdsnfjdff
MASKED_ROLE_PWD=vkskf345GRGEKF40404
DB_SOURCE=test_12343RDEFSDFSFQCDSFS
DB_TARGET=test_sFSFdvicsdfea232EDEF

createdb $DB_SOURCE
createuser $MASKED_ROLE

$PSQL $DB_SOURCE << EOSQL
  CREATE ROLE $MASKED_ROLE LOGIN PASSWORD '$MASKED_ROLE_PWD';
  CREATE EXTENSION anon CASCADE;
  CREATE TABLE people AS SELECT 'John' AS firstname;
  SECURITY LABEL FOR anon ON COLUMN people.firstname
  IS 'MASKED WITH VALUE ''Robert'' ';
EOSQL

##
## Dump & Restore
##

createdb $DB_TARGET
./bin/pg_dump_anon.sh $DB_SOURCE | $PSQL $DB_TARGET
#PGPASSWORD=$MASKED_ROLE_PWD ./pg_dump_anon.sh -U $MASKED_ROLE $DB_SOURCE | $PSQL $DB_TARGET

$PSQL $DB_TARGET << EOSQL
  SELECT firstname FROM people;
EOSQL

# clean up
dropdb $DB_SOURCE
dropdb $DB_TARGET
dropuser $MASKED_ROLE

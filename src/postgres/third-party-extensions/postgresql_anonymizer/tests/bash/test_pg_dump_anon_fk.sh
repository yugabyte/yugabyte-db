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
CREATE SCHEMA test_fk;
CREATE TABLE test_fk.table_a (
    id character varying NOT NULL,
    fkb character varying
);
CREATE TABLE test_fk.table_b (
    id character varying NOT NULL,
    fka character varying
);

COPY test_fk.table_a (id, fkb) FROM stdin;
A	B
\.

COPY test_fk.table_b (id, fka) FROM stdin;
B	A
\.

ALTER TABLE ONLY test_fk.table_a
    ADD CONSTRAINT table_a_pkey PRIMARY KEY (id);

ALTER TABLE ONLY test_fk.table_b
    ADD CONSTRAINT table_b_pk PRIMARY KEY (id);

ALTER TABLE ONLY test_fk.table_a
    ADD CONSTRAINT table_a_fk FOREIGN KEY (fkb) REFERENCES test_fk.table_b(id);

ALTER TABLE ONLY test_fk.table_b
    ADD CONSTRAINT table_b_fk FOREIGN KEY (fka) REFERENCES test_fk.table_a(id);

EOSQL

##
## Dump & Restore
##

createdb $DB_TARGET
./bin/pg_dump_anon.sh $DB_SOURCE | $PSQL $DB_TARGET

# clean up
dropdb $DB_SOURCE
dropdb $DB_TARGET
dropuser $MASKED_ROLE

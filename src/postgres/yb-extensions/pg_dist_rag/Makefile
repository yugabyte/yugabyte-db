# Makefile for pg_dist_rag extension

EXTENSION = pg_dist_rag
EXTVERSION = 0.0.2

MODULE_big = $(EXTENSION)
# Every SQL script the extension mechanism needs to reach EXTVERSION: the
# original released base version plus each subsequent --from--to upgrade
# script. 0.0.1 is already released, so new schema changes land as a new
# upgrade script here rather than editing $(EXTENSION)--0.0.1.sql in place.
DATA = $(EXTENSION)--0.0.1.sql $(EXTENSION)--0.0.1--0.0.2.sql

TESTS = $(wildcard sql/*.sql)
REGRESS = $(patsubst sql/%.sql,%,$(TESTS))

PG_CONFIG ?= pg_config

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

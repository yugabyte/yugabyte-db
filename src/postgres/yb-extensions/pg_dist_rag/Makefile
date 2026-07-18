# Makefile for pg_dist_rag extension

EXTENSION = pg_dist_rag
EXTVERSION = 0.0.1

MODULE_big = $(EXTENSION)
DATA = $(EXTENSION)--$(EXTVERSION).sql

TESTS = $(wildcard sql/*.sql)
REGRESS = $(patsubst sql/%.sql,%,$(TESTS))

PG_CONFIG ?= pg_config

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

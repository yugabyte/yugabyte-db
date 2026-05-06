# Makefile for pg_dist_rag extension

EXTENSION = pg_dist_rag
EXTVERSION = 0.0.2

MODULE_big = $(EXTENSION)
DATA = $(EXTENSION)--0.0.1.sql \
       $(EXTENSION)--0.0.1--0.0.2.sql \
       $(EXTENSION)--$(EXTVERSION).sql

TESTS = $(wildcard sql/*.sql)
REGRESS = $(patsubst sql/%.sql,%,$(TESTS))

PG_CONFIG ?= pg_config

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

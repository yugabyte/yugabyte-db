EXTENSION = pg_partman
EXTVERSION = $(shell grep default_version $(EXTENSION).control | \
               sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")

DATA = $(filter-out $(wildcard updates/*--*.sql),$(wildcard sql/*.sql))
DOCS = $(wildcard doc/*.md)
SCRIPTS = bin/*.py
PG_CONFIG = pg_config
PG91 = $(shell $(PG_CONFIG) --version | egrep " 8\.| 9\.0" > /dev/null && echo no || echo yes)
PG92 = $(shell $(PG_CONFIG) --version | egrep " 8\.| 9\.0| 9\.1" > /dev/null && echo no || echo yes)

ifeq ($(PG91),yes)
all: sql/$(EXTENSION)--$(EXTVERSION).sql

ifeq ($(PG92),yes)
sql/$(EXTENSION)--$(EXTVERSION).sql: sql/types/*.sql sql/tables/*.sql sql/functions/*.sql sql/92/tables/*.sql
	cat $^ > $@
else
sql/$(EXTENSION)--$(EXTVERSION).sql: sql/types/*.sql sql/tables/*.sql sql/functions/*.sql
	cat $^ > $@
endif

DATA = $(wildcard updates/*--*.sql) sql/$(EXTENSION)--$(EXTVERSION).sql
EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

EXTENSION = pg_partman
EXTVERSION = $(shell grep default_version $(EXTENSION).control | \
               sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")

PG_CONFIG = pg_config
PG94 = $(shell $(PG_CONFIG) --version | egrep " 8\.| 9\.0| 9\.1| 9\.2| 9\.3" > /dev/null && echo no || echo yes)
PG11 = $(shell $(PG_CONFIG) --version | egrep " 8\.| 9\.| 10\." > /dev/null && echo no || echo yes)

ifeq ($(PG94),yes)

DOCS = $(wildcard doc/*.md)
MODULES = src/pg_partman_bgw

# If user does not want the background worker, run: make NO_BGW=1
ifneq ($(NO_BGW),)
	MODULES=
endif

all: sql/$(EXTENSION)--$(EXTVERSION).sql

# Only include scripts common to all version of PG in PG11+. 
# Otherwise if less than PG11, include the old python scripts along with common.
# Only include procedures in PG11+
ifeq ($(PG11),yes)
SCRIPTS = bin/common/*.py

sql/$(EXTENSION)--$(EXTVERSION).sql: $(sort $(wildcard sql/types/*.sql)) $(sort $(wildcard sql/tables/*.sql)) $(sort $(wildcard sql/functions/*.sql)) $(sort $(wildcard sql/procedures/*.sql))
	cat $^ > $@

else
SCRIPTS = bin/common/*.py bin/pg10/*.py

sql/$(EXTENSION)--$(EXTVERSION).sql: $(sort $(wildcard sql/types/*.sql)) $(sort $(wildcard sql/tables/*.sql)) $(sort $(wildcard sql/functions/*.sql))
	cat $^ > $@
	#
# end PG11 if
endif

DATA = $(wildcard updates/*--*.sql) sql/$(EXTENSION)--$(EXTVERSION).sql
EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql
else
$(error Minimum version of PostgreSQL required is 9.4.0)

# end PG94 if
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

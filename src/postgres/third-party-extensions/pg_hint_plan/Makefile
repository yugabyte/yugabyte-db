MODULES = pg_hint_plan
HINTPLANVER = 1.3.7

REGRESS_OPTS = --encoding=UTF8

EXTENSION = pg_hint_plan
DATA = pg_hint_plan--*.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

STARBALL11 = pg_hint_plan11-$(HINTPLANVER).tar.gz
STARBALLS = $(STARBALL11)

LDFLAGS += -lyb_pggate_util
LDFLAGS += -L${BUILD_ROOT}/lib

# pg_hint_plan.c includes core.c and make_join_rel.c
pg_hint_plan.o: core.c make_join_rel.c # pg_stat_statements.c

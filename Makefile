MODULE_big = pg_hypo
OBJS = pg_hypo.o

EXTENSION = pg_hypo
DATA = pg_hypo--0.1.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)


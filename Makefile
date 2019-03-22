MODULE_big = agensgraph

OBJS = agensgraph.o \
       analyze.o

EXTENSION = agensgraph

DATA = agensgraph--0.0.0.sql

REGRESS = cypher

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

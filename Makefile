MODULE_big = agensgraph

OBJS = agensgraph.o \
       analyze.o \
       scan.o

EXTENSION = agensgraph

DATA = agensgraph--0.0.0.sql

REGRESS = cypher \
          scan
REGRESS_OPTS = --load-extension=agensgraph

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

scan.c: FLEX_NO_BACKUP=yes

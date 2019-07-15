MODULE_big = agensgraph

OBJS = ag_catalog.o \
       ag_graph.o \
       agensgraph.o \
       analyze.o \
       commands.o \
       scan.o

EXTENSION = agensgraph

DATA = agensgraph--0.0.0.sql

REGRESS = commands \
          cypher \
          scan
REGRESS_OPTS = --load-extension=agensgraph

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

scan.c: FLEX_NO_BACKUP=yes

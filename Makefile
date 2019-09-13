MODULE_big = agensgraph

OBJS = ag_catalog.o \
       ag_graph.o \
       ag_extended_type.o \
       ag_json.o \
       ag_jsonbx.o \
       ag_jsonbx_util.o \
       agensgraph.o \
       analyze.o \
       commands.o \
       cypher_clause.o \
       cypher_expr.o \
       cypher_gram.o \
       cypher_keywords.o \
       cypher_parser.o \
       nodes.o \
       outfuncs.o \
       scan.o

EXTENSION = agensgraph

DATA = agensgraph--0.0.0.sql

REGRESS = commands \
          cypher \
          scan \
          jsonbx
REGRESS_OPTS = --load-extension=agensgraph

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

cypher_gram.c: BISONFLAGS += --defines=$(basename $@)_def.h

scan.c: FLEX_NO_BACKUP=yes

.PHONY: doc-html
doc-html:
	make -C doc html

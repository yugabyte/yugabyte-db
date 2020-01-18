MODULE_big = agensgraph

OBJS = src/backend/agensgraph.o \
       src/backend/catalog/ag_catalog.o \
       src/backend/catalog/ag_graph.o \
       src/backend/commands/commands.o \
       src/backend/executor/cypher_create.o \
       src/backend/nodes/ag_nodes.o \
       src/backend/nodes/outfuncs.o \
       src/backend/optimizer/cypher_createplan.o \
       src/backend/optimizer/cypher_pathnode.o \
       src/backend/optimizer/cypher_paths.o \
       src/backend/parser/ag_scanner.o \
       src/backend/parser/cypher_analyze.o \
       src/backend/parser/cypher_clause.o \
       src/backend/parser/cypher_expr.o \
       src/backend/parser/cypher_gram.o \
       src/backend/parser/cypher_keywords.o \
       src/backend/parser/cypher_parser.o \
       src/backend/parser/cypher_parse_node.o \
       src/backend/utils/adt/agtype.o \
       src/backend/utils/adt/agtype_ext.o \
       src/backend/utils/adt/agtype_ops.o \
       src/backend/utils/adt/agtype_parser.o \
       src/backend/utils/adt/agtype_util.o \
       src/backend/utils/adt/graphid.o

EXTENSION = agensgraph

DATA = agensgraph--0.0.0.sql

REGRESS = agtype \
          commands \
          cypher \
          expr \
          scan

REGRESS_OPTS = --load-extension=agensgraph

ag_include_dir = $(srcdir)/src/include
PG_CPPFLAGS = -I$(ag_include_dir)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

src/backend/parser/cypher_gram.c: BISONFLAGS += --defines=$(ag_include_dir)/parser/$(basename $(notdir $@))_def.h

src/backend/parser/ag_scanner.c: FLEX_NO_BACKUP=yes

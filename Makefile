# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

MODULE_big = age

age_sql = age--1.5.0.sql

OBJS = src/backend/age.o \
       src/backend/catalog/ag_catalog.o \
       src/backend/catalog/ag_graph.o \
       src/backend/catalog/ag_label.o \
       src/backend/catalog/ag_namespace.o \
       src/backend/commands/graph_commands.o \
       src/backend/commands/label_commands.o \
       src/backend/executor/cypher_create.o \
       src/backend/executor/cypher_merge.o \
       src/backend/executor/cypher_set.o \
       src/backend/executor/cypher_utils.o \
       src/backend/nodes/ag_nodes.o \
       src/backend/nodes/cypher_copyfuncs.o \
       src/backend/nodes/cypher_outfuncs.o \
       src/backend/nodes/cypher_readfuncs.o \
       src/backend/optimizer/cypher_createplan.o \
       src/backend/optimizer/cypher_pathnode.o \
       src/backend/optimizer/cypher_paths.o \
       src/backend/parser/ag_scanner.o \
       src/backend/parser/cypher_analyze.o \
       src/backend/parser/cypher_clause.o \
       src/backend/executor/cypher_delete.o \
       src/backend/parser/cypher_expr.o \
       src/backend/parser/cypher_gram.o \
       src/backend/parser/cypher_item.o \
       src/backend/parser/cypher_keywords.o \
       src/backend/parser/cypher_parse_agg.o \
       src/backend/parser/cypher_parse_node.o \
       src/backend/parser/cypher_parser.o \
       src/backend/parser/cypher_transform_entity.o \
       src/backend/utils/adt/age_graphid_ds.o \
       src/backend/utils/adt/agtype.o \
       src/backend/utils/adt/agtype_ext.o \
       src/backend/utils/adt/agtype_gin.o \
       src/backend/utils/adt/agtype_ops.o \
       src/backend/utils/adt/agtype_parser.o \
       src/backend/utils/adt/agtype_util.o \
       src/backend/utils/adt/agtype_raw.o \
       src/backend/utils/adt/age_global_graph.o \
       src/backend/utils/adt/age_session_info.o \
       src/backend/utils/adt/age_vle.o \
       src/backend/utils/adt/cypher_funcs.o \
       src/backend/utils/adt/ag_float8_supp.o \
       src/backend/utils/adt/graphid.o \
       src/backend/utils/ag_func.o \
       src/backend/utils/graph_generation.o \
       src/backend/utils/cache/ag_cache.o \
       src/backend/utils/load/ag_load_labels.o \
       src/backend/utils/load/ag_load_edges.o \
       src/backend/utils/load/age_load.o \
       src/backend/utils/load/libcsv.o \
       src/backend/utils/name_validation.o \
       src/backend/utils/ag_guc.o

EXTENSION = age

# to allow cleaning of previous (old) age--.sql files
all_age_sql = $(shell find . -maxdepth 1 -type f -regex './age--[0-9]+\.[0-9]+\.[0-9]+\.sql')

SQLS := $(shell cat sql/sql_files)
SQLS := $(addprefix sql/,$(SQLS))
SQLS := $(addsuffix .sql,$(SQLS))

DATA_built = $(age_sql)

# sorted in dependency order
REGRESS = scan \
          graphid \
          agtype \
          catalog \
          cypher \
          expr \
          cypher_create \
          cypher_match \
          cypher_unwind \
          cypher_set \
          cypher_remove \
          cypher_delete \
          cypher_with \
          cypher_vle \
          cypher_union \
          cypher_call \
          cypher_merge \
          age_global_graph \
          age_load \
          index \
          analyze \
          graph_generation \
          name_validation \
          jsonb_operators \
          drop

srcdir=`pwd`

ag_regress_dir = $(srcdir)/regress
REGRESS_OPTS = --load-extension=age --inputdir=$(ag_regress_dir) --outputdir=$(ag_regress_dir) --temp-instance=$(ag_regress_dir)/instance --port=61958 --encoding=UTF-8 --temp-config $(ag_regress_dir)/age_regression.conf

ag_regress_out = instance/ log/ results/ regression.*
EXTRA_CLEAN = $(addprefix $(ag_regress_dir)/, $(ag_regress_out)) src/backend/parser/cypher_gram.c src/include/parser/cypher_gram_def.h src/include/parser/cypher_kwlist_d.h $(all_age_sql)

GEN_KEYWORDLIST = $(PERL) -I ./tools/ ./tools/gen_keywordlist.pl
GEN_KEYWORDLIST_DEPS = ./tools/gen_keywordlist.pl tools/PerfectHash.pm

ag_include_dir = $(srcdir)/src/include
PG_CPPFLAGS = -I$(ag_include_dir) -I$(ag_include_dir)/parser

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

src/backend/parser/cypher_keywords.o: src/include/parser/cypher_kwlist_d.h

src/include/parser/cypher_kwlist_d.h: src/include/parser/cypher_kwlist.h $(GEN_KEYWORDLIST_DEPS)
	$(GEN_KEYWORDLIST) --extern --varname CypherKeyword --output src/include/parser $<

src/include/parser/cypher_gram_def.h: src/backend/parser/cypher_gram.c

src/backend/parser/cypher_gram.c: BISONFLAGS += --defines=src/include/parser/cypher_gram_def.h

src/backend/parser/cypher_parser.o: src/backend/parser/cypher_gram.c
src/backend/parser/cypher_keywords.o: src/backend/parser/cypher_gram.c

$(age_sql):
	@cat $(SQLS) > $@

src/backend/parser/ag_scanner.c: FLEX_NO_BACKUP=yes

installcheck: export LC_COLLATE=C

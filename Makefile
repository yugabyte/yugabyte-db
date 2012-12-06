#
# pg_hint_plan: Makefile
#
# Copyright (c) 2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#

MODULES = pg_hint_plan
REGRESS = init base_plan pg_hint_plan fdw ut-init ut-A ut-S ut-J ut-L ut-G ut-fini create_execute

REGRESSION_EXPECTED = expected/init.out expected/base_plan.out expected/pg_hint_plan.out expected/ut-A.out expected/ut-S.out expected/ut-J.out expected/ut-L.out expected/ut-G.out expected/create_execute.out

REGRESS_OPTS = --encoding=UTF8

EXTRA_CLEAN = core.c sql/fdw.sql expected/fdw.out $(REGRESSION_EXPECTED)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

core.c: core-$(MAJORVERSION).c
	cp core-$(MAJORVERSION).c core.c

$(REGRESSION_EXPECTED): %.out: %-$(MAJORVERSION).out
	cp $< $@

installcheck: $(REGRESSION_EXPECTED)

# pg_hint_plan.c includes core.c and make_join_rel.c
pg_hint_plan.o: core.c make_join_rel.c

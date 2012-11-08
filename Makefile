#
# pg_hint_plan: Makefile
#
# Copyright (c) 2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#

MODULES = pg_hint_plan
REGRESS = init base_plan pg_hint_plan prepare fdw ut-init ut-A ut-A2 ut-S ut-J ut-L ut-G ut-fini
#REGRESS = ut-init ut-J ut-fini

EXTRA_CLEAN = core.c sql/fdw.sql expected/base_plan.out expected/prepare.out expected/fdw.out

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

core.c: core-$(MAJORVERSION).c
	cp core-$(MAJORVERSION).c core.c
expected/base_plan.out: expected/base_plan-$(MAJORVERSION).out
	cp expected/base_plan-$(MAJORVERSION).out expected/base_plan.out
expected/prepare.out: expected/prepare-$(MAJORVERSION).out
	cp expected/prepare-$(MAJORVERSION).out expected/prepare.out

installcheck: expected/base_plan.out expected/prepare.out

# pg_hint_plan.c includes core.c and make_join_rel.c
pg_hint_plan.o: core.c make_join_rel.c

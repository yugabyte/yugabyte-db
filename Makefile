#
# pg_hint_plan: Makefile
#
# Copyright (c) 2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#

MODULES = pg_hint_plan
REGRESS = init base_plan pg_hint_plan fdw ut-init ut-A ut-A2 ut-S ut-J ut-L ut-G ut-fini indexonly create_execute
#REGRESS = ut-init ut-J ut-fini

EXTRA_CLEAN = core.c sql/fdw.sql expected/init.out expected/base_plan.out expected/pg_hint_plan.out expected/fdw.out expected/ut-A.out expected/ut-A2.out expected/ut-S.out expected/ut-J.out expected/ut-L.out expected/ut-G.out expected/indexonly.out expected/create_execute.out

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

core.c: core-$(MAJORVERSION).c
	cp core-$(MAJORVERSION).c core.c
expected/init.out: expected/init-$(MAJORVERSION).out
	cp expected/init-$(MAJORVERSION).out expected/init.out
expected/pg_hint_plan.out: expected/pg_hint_plan-$(MAJORVERSION).out
	cp expected/pg_hint_plan-$(MAJORVERSION).out expected/pg_hint_plan.out
expected/ut-A.out: expected/ut-A-$(MAJORVERSION).out
	cp expected/ut-A-$(MAJORVERSION).out expected/ut-A.out
expected/ut-A2.out: expected/ut-A2-$(MAJORVERSION).out
	cp expected/ut-A2-$(MAJORVERSION).out expected/ut-A2.out
expected/ut-G.out: expected/ut-G-$(MAJORVERSION).out
	cp expected/ut-G-$(MAJORVERSION).out expected/ut-G.out
expected/ut-J.out: expected/ut-J-$(MAJORVERSION).out
	cp expected/ut-J-$(MAJORVERSION).out expected/ut-J.out
expected/ut-L.out: expected/ut-L-$(MAJORVERSION).out
	cp expected/ut-L-$(MAJORVERSION).out expected/ut-L.out
expected/ut-S.out: expected/ut-S-$(MAJORVERSION).out
	cp expected/ut-S-$(MAJORVERSION).out expected/ut-S.out
expected/base_plan.out: expected/base_plan-$(MAJORVERSION).out
	cp expected/base_plan-$(MAJORVERSION).out expected/base_plan.out
expected/indexonly.out: expected/indexonly-$(MAJORVERSION).out
	cp expected/indexonly-$(MAJORVERSION).out expected/indexonly.out
expected/create_execute.out: expected/create_execute-$(MAJORVERSION).out
	cp expected/create_execute-$(MAJORVERSION).out expected/create_execute.out

installcheck: expected/init.out expected/base_plan.out expected/pg_hint_plan.out expected/ut-A.out expected/ut-A2.out expected/ut-S.out expected/ut-J.out expected/ut-L.out expected/ut-G.out expected/indexonly.out expected/create_execute.out

# pg_hint_plan.c includes core.c and make_join_rel.c
pg_hint_plan.o: core.c make_join_rel.c

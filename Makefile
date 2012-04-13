#
# pg_hint_plan: Makefile
#
# Copyright (c) 2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#

MODULES = pg_hint_plan
REGRESS = init base_plan pg_hint_plan

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

expected/base_plan.out: expected/base_plan-$(MAJORVERSION).out
	cp expected/base_plan-$(MAJORVERSION).out expected/base_plan.out

.PHONY: subclean
clean: subclean
subclean:
	rm -f expected/base_plan.out

installcheck: expected/base_plan.out

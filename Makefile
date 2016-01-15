#
# pg_hint_plan: Makefile
#
# Copyright (c) 2012-2015, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#

MODULES = pg_hint_plan
HINTPLANVER = 1.1.3

REGRESS = init base_plan pg_hint_plan ut-init ut-A ut-S ut-J ut-L ut-G ut-R ut-fdw ut-fini

REGRESSION_EXPECTED = expected/init.out expected/base_plan.out expected/pg_hint_plan.out expected/ut-A.out expected/ut-S.out expected/ut-J.out expected/ut-L.out expected/ut-G.out

REGRESS_OPTS = --encoding=UTF8

EXTENSION = pg_hint_plan
DATA = pg_hint_plan--1.1.3.sql

EXTRA_CLEAN = sql/ut-fdw.sql expected/ut-fdw.out

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

STARBALL = pg_dbms_stats-$(DBMSSTATSVER).tar.gz
STARBALL95 = pg_hint_plan95-$(HINTPLANVER).tar.gz
STARBALLS = $(STARBALL) $(STARBALL95)

TARSOURCES = Makefile *.c  *.h \
	pg_hint_plan--*.sql \
	pg_hint_plan.control \
	doc/* expected/*.out sql/*.sql \
	input/*.source output/*.source SPECS/*.spec

installcheck: $(REGRESSION_EXPECTED)

rpms: rpm95

# pg_hint_plan.c includes core.c and make_join_rel.c
pg_hint_plan.o: core.c make_join_rel.c # pg_stat_statements.c

$(STARBALLS): $(TARSOURCES)
	if [ -h $(subst .tar.gz,,$@) ]; then rm $(subst .tar.gz,,$@); fi
	if [ -e $(subst .tar.gz,,$@) ]; then \
	  echo "$(subst .tar.gz,,$@) is not a symlink. Stop."; \
	  exit 1; \
	fi
	ln -s . $(subst .tar.gz,,$@)
	tar -chzf $@ $(addprefix $(subst .tar.gz,,$@)/, $^)
	rm $(subst .tar.gz,,$@)

rpm95: $(STARBALL95)
	MAKE_ROOT=`pwd` rpmbuild -bb SPECS/pg_hint_plan95.spec



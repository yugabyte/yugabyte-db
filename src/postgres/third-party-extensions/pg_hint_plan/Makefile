#
# pg_hint_plan: Makefile
#
# Copyright (c) 2012-2026, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#

MODULE_big = pg_hint_plan
OBJS = \
	$(WIN32RES) \
	pg_hint_plan.o \
	query_scan.o

HINTPLANVER = 1.9.0

REGRESS = init base_plan pg_hint_plan ut-init ut-A ut-S ut-J ut-L ut-G ut-R \
	ut-fdw ut-W ut-T ut-fini plpgsql hint_table disable_index \
	query_parser oldextversions
REGRESS_OPTS = --encoding=UTF8

EXTENSION = pg_hint_plan
DATA = \
	pg_hint_plan--1.3.0.sql \
	pg_hint_plan--1.3.0--1.3.1.sql \
	pg_hint_plan--1.3.1--1.3.2.sql \
	pg_hint_plan--1.3.2--1.3.3.sql \
	pg_hint_plan--1.3.3--1.3.4.sql \
	pg_hint_plan--1.3.5--1.3.6.sql \
	pg_hint_plan--1.3.4--1.3.5.sql \
	pg_hint_plan--1.3.6--1.3.7.sql \
	pg_hint_plan--1.3.7--1.3.8.sql \
	pg_hint_plan--1.3.8--1.3.9.sql \
	pg_hint_plan--1.3.9--1.3.10.sql \
	pg_hint_plan--1.3.10--1.3.11.sql \
	pg_hint_plan--1.3.11--1.4.sql \
	pg_hint_plan--1.4--1.4.1.sql \
	pg_hint_plan--1.4.1--1.4.2.sql \
	pg_hint_plan--1.4.2--1.4.3.sql \
	pg_hint_plan--1.4.3--1.4.4.sql \
	pg_hint_plan--1.4.4--1.4.5.sql \
	pg_hint_plan--1.4.5--1.5.sql \
	pg_hint_plan--1.5--1.5.1.sql \
	pg_hint_plan--1.5.1--1.5.2.sql \
	pg_hint_plan--1.5.2--1.5.3.sql \
	pg_hint_plan--1.5.3--1.5.4.sql \
	pg_hint_plan--1.5.4--1.6.0.sql \
	pg_hint_plan--1.6.0--1.6.1.sql \
	pg_hint_plan--1.6.1--1.6.2.sql \
	pg_hint_plan--1.6.2--1.6.3.sql \
	pg_hint_plan--1.6.3--1.7.0.sql \
	pg_hint_plan--1.7.0--1.7.1.sql \
	pg_hint_plan--1.7.1--1.7.2.sql \
	pg_hint_plan--1.7.2--1.8.0.sql \
	pg_hint_plan--1.8.0--1.8.1.sql \
	pg_hint_plan--1.8.1--1.9.0.sql \
	pg_hint_plan--1.5.1--1.5.1-yb-1.0.sql \
	pg_hint_plan--1.9.0--1.9.0-yb-1.0.sql

EXTRA_CLEAN = RPMS

# Switch environment between standalone make and make check with
# EXTRA_INSTALL from PostgreSQL tree
# run the following command in the PG tree to run a regression test
# loading this module.
# $ make check EXTRA_INSTALL=<this directory> EXTRA_REGRESS_OPTS="--temp-config <this directory>/pg_hint_plan.conf"
ifeq ($(wildcard $(DESTDIR)/../src/Makefile.global),)
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = `pwd`
top_builddir = $(DESTDIR)/..
include $(DESTDIR)/../src/Makefile.global
include $(DESTDIR)/../contrib/contrib-global.mk
endif

STARBALL19 = pg_hint_plan19-$(HINTPLANVER).tar.gz
STARBALLS = $(STARBALL19)

TARSOURCES = Makefile *.c  *.h COPYRIGHT* \
	pg_hint_plan--*.sql \
	pg_hint_plan.control \
	docs/* expected/*.out sql/*.sql \
	data/data.csv SPECS/*.spec

LDFLAGS += -lyb_pggate_util
LDFLAGS += -lyb_pggate
LDFLAGS += -L${BUILD_ROOT}/lib

rpms: rpm19

$(STARBALLS): $(TARSOURCES)
	if [ -h $(subst .tar.gz,,$@) ]; then rm $(subst .tar.gz,,$@); fi
	if [ -e $(subst .tar.gz,,$@) ]; then \
	  echo "$(subst .tar.gz,,$@) is not a symlink. Stop."; \
	  exit 1; \
	fi
	ln -s . $(subst .tar.gz,,$@)
	tar -chzf $@ $(addprefix $(subst .tar.gz,,$@)/, $^)
	rm $(subst .tar.gz,,$@)

rpm19: $(STARBALL19)
	MAKE_ROOT=`pwd` rpmbuild -bb SPECS/pg_hint_plan19.spec

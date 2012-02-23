# contrib/pg_hint_plan/Makefile

MODULES = pg_hint_plan
DATA = pg_hint_plan--1.0.sql \
	pg_hint_plan--unpackaged--1.0.sql

REGRESS = pg_hint_plan

EXTENSION = pg_hint_plan

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_hint_plan
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

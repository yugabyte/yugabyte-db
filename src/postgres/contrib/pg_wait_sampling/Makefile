# contrib/pg_wait_sampling/Makefile

MODULE_big = pg_wait_sampling
OBJS = pg_wait_sampling.o collector.o

EXTENSION = pg_wait_sampling
DATA = pg_wait_sampling--1.1.sql pg_wait_sampling--1.0--1.1.sql

REGRESS = load queries

EXTRA_REGRESS_OPTS=--temp-config=$(top_srcdir)/$(subdir)/conf.add

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_wait_sampling
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# Prepare the package for PGXN submission
package: dist .git
	$(eval DISTVERSION := $(shell git tag -l | tail -n 1 | cut -d 'v' -f 2))
	$(info Generating zip file for version $(DISTVERSION)...)
	git archive --format zip --prefix=$(EXTENSION)-${DISTVERSION}/ --output dist/$(EXTENSION)-${DISTVERSION}.zip HEAD

dist:
	mkdir -p dist

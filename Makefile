# $Id$
TESTS = $(wildcard sql/*.sql)
EXTRA_CLEAN = test_setup.sql
DATA_built = pgtap.sql uninstall_pgtap.sql
MODULES = pgtap
DOCS = README.pgtap
SCRIPTS = bin/pg_prove
REGRESS = $(patsubst sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --load-language plpgsql

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/citext
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# We need to do various things with various versions of PostgreSQL.
PGVER_MAJOR = $(shell echo $(VERSION) | awk -F. '{ print ($$1 + 0) }')
PGVER_MINOR = $(shell echo $(VERSION) | awk -F. '{ print ($$2 + 0) }')
PGVER_PATCH = $(shell echo $(VERSION) | awk -F. '{ print ($$3 + 0) }')

# We support 8.0 and later.
ifneq ($(PGVER_MAJOR), 8)
$(error pgTAP requires PostgreSQL 8.0 or later. This is $(VERSION))
endif

# Set up extra substitutions based on version numbers.
ifeq ($(PGVER_MAJOR), 8)
ifeq ($(PGVER_MINOR), 0)
# Hack for E'' syntax (<= PG8.0)
EXTRAS := -e "s/ E'/ '/g"
# Throw isn't supported in 8.0.
TESTS := $(filter-out sql/throwtap.sql,$(TESTS))
REGRESS := $(filter-out throwtap,$(REGRESS))
endif
endif

# Override how .sql targets are processed to add the schema info, if
# necessary. Otherwise just copy the files.
test_setup.sql: test_setup.sql.in
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' $(EXTRAS) $< >$@
	cp $< $@
endif

pgtap.sql: pgtap.sql.in test_setup.sql
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' $(EXTRAS) pgtap.sql.in > pgtap.sql
else
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' $(EXTRAS) pgtap.sql.in > pgtap.sql
endif
ifeq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MINOR), 3)
	cat compat/install-8.2.sql >> pgtap.sql
ifneq ($(PGVER_MINOR), 2)
ifneq ($(PGVER_MINOR), 1)
	patch -p0 < compat/install-8.0.patch
endif
endif
endif
endif

uninstall_pgtap.sql: uninstall_pgtap.sql.in test_setup.sql
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' $(EXTRAS) uninstall_pgtap.sql.in > uninstall_pgtap.sql
else
ifdef EXTRAS
	sed $(EXTRAS) uninstall_pgtap.sql.in > uninstall_pgtap.sql
else
	cp uninstall_pgtap.sql.in uninstall_pgtap.sql
endif
endif
ifeq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MINOR), 3)
	mv uninstall_pgtap.sql uninstall_pgtap.tmp
	cat compat/uninstall-8.2.sql uninstall_pgtap.tmp >> uninstall_pgtap.sql
	rm uninstall_pgtap.tmp
endif
endif

# Make sure that we build the regression tests.
installcheck: test_setup.sql

# In addition to installcheck, one can also run the tests through pg_prove.
test: test_setup.sql
	./bin/pg_prove --pset tuples_only=1 $(TESTS)

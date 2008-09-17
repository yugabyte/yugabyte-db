# $Id$
DATA_built = pgtap.sql uninstall_pgtap.sql $(patsubst %.sql.in,%.sql,$(wildcard sql/*.sql.in))
MODULES = pgtap
DOCS = README.pgtap
SCRIPTS = bin/pg_prove
REGRESS = moretap istap pg73 todotap matching throwtap hastap coltap pktap fktap unique check cmpok

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

# We support 8.1 and later.
ifneq ($(PGVER_MAJOR), 8)
$(error pgTAP requires PostgreSQL 8.1 or later. This is $(VERSION))
endif

# Override how .sql targets are processed to add the schema info, if
# necessary. Otherwise just copy the files.
%.sql: %.sql.in
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g'  $< >$@
else
	cp $< $@
endif

pgtap.sql: pgtap.sql.in
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' pgtap.sql.in > pgtap.sql
else
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' pgtap.sql.in > pgtap.sql
endif
ifeq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MINOR), 3)
	cat compat/install-8.2.sql >> pgtap.sql
endif
endif

uninstall_pgtap.sql: uninstall_pgtap.sql.in
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' uninstall_pgtap.sql.in > uninstall_pgtap.sql
else
	cp uninstall_pgtap.sql.in uninstall_pgtap.sql
endif
ifeq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MINOR), 3)
	mv uninstall_pgtap.sql uninstall_pgtap.tmp
	cat compat/uninstall-8.2.sql uninstall_pgtap.tmp >> uninstall_pgtap.sql
	rm uninstall_pgtap.tmp
endif
endif

# In addition to installcheck, one can also run the tests through pg_prove.
test:
	./bin/pg_prove sql/*.sql


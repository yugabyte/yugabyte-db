# $Id$
TESTS = $(wildcard sql/*.sql)
EXTRA_CLEAN = test_setup.sql *.html
DATA_built = pgtap.sql uninstall_pgtap.sql
MODULES = pgtap
DOCS = README.pgtap
SCRIPTS = bin/pg_prove
REGRESS = $(patsubst sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --load-language=plpgsql

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
PGTAP_VERSION = 0.15

# We support 8.0 and later.
ifneq ($(PGVER_MAJOR), 8)
$(error pgTAP requires PostgreSQL 8.0 or later. This is $(VERSION))
endif

# Set up extra substitutions based on version numbers.
ifeq ($(PGVER_MAJOR), 8)
ifeq ($(PGVER_MINOR), 0)
# Hack for E'' syntax (<= PG8.0)
REMOVE_E := -e "s/ E'/ '/g"
# Throw and runtests aren't supported in 8.0.
TESTS := $(filter-out sql/throwtap.sql sql/runtests.sql,$(TESTS))
REGRESS := $(filter-out throwtap runtests,$(REGRESS))
endif
endif

# Determine the OS. Borrowed from Perl's Configure.
ifeq ($(wildcard /irix), /irix)
OSNAME=irix
endif
ifeq ($(wildcard /xynix), /xynix)
OSNAME=sco_xenix
endif
ifeq ($(wildcard /dynix), /dynix)
OSNAME=dynix
endif
ifeq ($(wildcard /dnix), /dnix)
OSNAME=dnxi
endif
ifeq ($(wildcard /lynx.os), /lynx.os)
OSNAME=lynxos
endif
ifeq ($(wildcard /unicos), /unicox)
OSNAME=unicos
endif
ifeq ($(wildcard /unicosmk), /unicosmk)
OSNAME=unicosmk
endif
ifeq ($(wildcard /unicosmk.ar), /unicosmk.ar)
OSNAME=unicosmk
endif
ifeq ($(wildcard /bin/mips), /bin/mips)
OSNAME=mips
endif
ifeq ($(wildcard /usr/apollo/bin), /usr/apollo/bin)
OSNAME=apollo
endif
ifeq ($(wildcard /etc/saf/_sactab), /etc/saf/_sactab)
OSNAME=svr4
endif
ifeq ($(wildcard /usr/include/minix), /usr/include/minix)
OSNAME=minix
endif
ifeq ($(wildcard /system/gnu_library/bin/ar.pm), /system/gnu_library/bin/ar.pm)
OSNAME=vos
endif
ifeq ($(wildcard /MachTen), /MachTen)
OSNAME=machten
endif
ifeq ($(wildcard /sys/posix.dll), /sys/posix.dll)
OSNAME=uwin
endif

# Fallback on uname, if it's available.
ifndef OSNAME
OSNAME = $(shell uname | awk '{print tolower($1)}')
endif

# Override how .sql targets are processed to add the schema info, if
# necessary. Otherwise just copy the files.
test_setup.sql: test_setup.sql.in
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' $< >$@
	cp $< $@
endif

pgtap.sql: pgtap.sql.in test_setup.sql
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(PGTAP_VERSION),g' $(REMOVE_E) pgtap.sql.in > pgtap.sql
else
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(PGTAP_VERSION),g' $(REMOVE_E) pgtap.sql.in > pgtap.sql
endif
ifeq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MINOR), 3)
	cat compat/install-8.2.sql >> pgtap.sql
ifneq ($(PGVER_MINOR), 2)
ifeq ($(PGVER_MINOR), 1)
	patch -p0 < compat/install-8.1.patch
else
	patch -p0 < compat/install-8.0.patch
endif
endif
endif
endif

uninstall_pgtap.sql: uninstall_pgtap.sql.in test_setup.sql
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
ifeq ($(PGVER_MINOR), 0)
	mv uninstall_pgtap.sql uninstall_pgtap.tmp
	cat compat/uninstall-8.0.sql uninstall_pgtap.tmp >> uninstall_pgtap.sql
	rm uninstall_pgtap.tmp
endif
endif

# Make sure that we build the regression tests.
installcheck: test_setup.sql

# In addition to installcheck, one can also run the tests through pg_prove.
test: test_setup.sql
	./bin/pg_prove --pset tuples_only=1 $(TESTS)

html:
	markdown -F 0x1000 README.pgtap > pgtap.html
	perl -ne 'BEGIN { $$prev = 0; $$lab = ""; print "<h2>Table of Contents</h2>\n<ul>\n" } if (m{<h([123])\s+id="([^"]+)">((<code>[^(]+)?.+?)</h\1>}) { next if $$lab && $$lab eq $$4; $$lab = $$4; if ($$prev) { if ($$1 != $$prev) { print $$1 > $$prev ? $$1 - $$prev > 1 ? "<ul><li><ul>" : "<ul>\n" : $$prev - $$1 > 1 ? "</li></ul></li></ul></li>\n" : "</li></ul></li>\n"; $$prev = $$1; } else { print "</li>\n" } } else { $$prev = $$1; } print qq{<li><a href="#$$2">} . ($$4 ? "$$4()</code>" : $$3) . "</a>" } END { print "</li>\n</ul>\n" }' pgtap.html > toc.html
	perldoc -MPod::Simple::XHTML bin/pg_prove > pg_prove.html

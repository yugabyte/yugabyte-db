TESTS = $(wildcard test/sql/*.sql)
EXTRA_CLEAN = test/setup.sql doc/*.html
DATA_built = sql/pgtap.sql sql/pgtap-core.sql sql/pgtap-schema.sql sql/uninstall_pgtap.sql
DOCS = doc/pgtap.md
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test --load-language=plpgsql

ifdef NO_PGXS
top_builddir = ../..
PG_CONFIG := $(top_builddir)/src/bin/pg_config/pg_config
else
# Run pg_config to get the PGXS Makefiles
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
endif

# We need to do various things with various versions of PostgreSQL.
VERSION     = $(shell $(PG_CONFIG) --version | awk '{print $$2}')
PGVER_MAJOR = $(shell echo $(VERSION) | awk -F. '{ print ($$1 + 0) }')
PGVER_MINOR = $(shell echo $(VERSION) | awk -F. '{ print ($$2 + 0) }')
PGVER_PATCH = $(shell echo $(VERSION) | awk -F. '{ print ($$3 + 0) }')
PGTAP_VERSION = 0.26

# We support 8.0 and later.
ifneq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MAJOR), 9)
$(error pgTAP requires PostgreSQL 8.0 or later. This is $(VERSION))
endif
endif

# Compile the C code only if we're on 8.3 or older.
ifeq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MINOR), 4)
MODULES = src/pgtap
endif
endif

# We need Perl.
ifndef PERL
PERL := $(shell which perl)
endif

# Load PGXS now that we've set all the variables it might need.
ifdef NO_PGXS
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
else
include $(PGXS)
endif

# Is TAP::Parser::SourceHandler::pgTAP installed?
ifdef PERL
HAVE_HARNESS := $(shell $(PERL) -le 'eval { require TAP::Parser::SourceHandler::pgTAP }; print 1 unless $$@' )
endif

ifndef HAVE_HARNESS
    $(warning To use pg_prove, TAP::Parser::SourceHandler::pgTAP Perl module)
    $(warning must be installed from CPAN. To do so, simply run:)
    $(warning     cpan TAP::Parser::SourceHandler::pgTAP) 
endif

# Set up extra substitutions based on version numbers.
ifeq ($(PGVER_MAJOR), 8)
ifeq ($(PGVER_MINOR), 2)
# Enum tests not supported by 8.2 and earlier.
TESTS := $(filter-out sql/enumtap.sql,$(TESTS))
REGRESS := $(filter-out enumtap,$(REGRESS))
endif
ifeq ($(PGVER_MINOR), 1)
# Values tests not supported by 8.1 and earlier.
TESTS := $(filter-out sql/enumtap.sql sql/valueset.sql,$(TESTS))
REGRESS := $(filter-out enumtap valueset,$(REGRESS))
endif
ifeq ($(PGVER_MINOR), 0)
# Throw, runtests, enums, and roles aren't supported in 8.0.
TESTS := $(filter-out sql/throwtap.sql sql/runtests.sql sql/enumtap.sql sql/roletap.sql sql/valueset.sql,$(TESTS))
REGRESS := $(filter-out throwtap runtests enumtap roletap valueset,$(REGRESS))
endif
endif

# Determine the OS. Borrowed from Perl's Configure.
OSNAME := $(shell ./getos.sh)

# Override how .sql targets are processed to add the schema info, if
# necessary. Otherwise just copy the files.
test/setup.sql: test/setup.sql.in
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' $< >$@
else
	cp $< $@
endif

sql/pgtap.sql: sql/pgtap.sql.in test/setup.sql
	cp $< $@
ifeq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MINOR), 5)
ifneq ($(PGVER_MINOR), 4)
	patch -p0 < compat/install-8.3.patch
ifneq ($(PGVER_MINOR), 3)
	patch -p0 < compat/install-8.2.patch
ifneq ($(PGVER_MINOR), 2)
	patch -p0 < compat/install-8.1.patch
ifneq ($(PGVER_MINOR), 1)
	patch -p0 < compat/install-8.0.patch
#	Hack for E'' syntax (<= PG8.0)
	mv sql/pgtap.sql sql/pgtap.tmp
	sed -e "s/ E'/ '/g" sql/pgtap.tmp > sql/pgtap.sql
	rm sql/pgtap.tmp
endif
endif
endif
endif
endif
endif
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(PGTAP_VERSION),g' sql/pgtap.sql > sql/pgtap.tmp
else
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(PGTAP_VERSION),g' sql/pgtap.sql > sql/pgtap.tmp
endif
	mv sql/pgtap.tmp sql/pgtap.sql

sql/uninstall_pgtap.sql: sql/uninstall_pgtap.sql.in test/setup.sql
	cp sql/uninstall_pgtap.sql.in sql/uninstall_pgtap.sql
ifeq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MINOR), 5)
ifneq ($(PGVER_MINOR), 4)
	patch -p0 < compat/uninstall-8.3.patch
ifneq ($(PGVER_MINOR), 3)
	patch -p0 < compat/uninstall-8.2.patch
endif
ifeq ($(PGVER_MINOR), 0)
	patch -p0 < compat/uninstall-8.0.patch
endif
endif
endif
endif
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' sql/uninstall_pgtap.sql > sql/uninstall.tmp
	mv sql/uninstall.tmp sql/uninstall_pgtap.sql
endif

sql/pgtap-core.sql: sql/pgtap.sql.in
	cp $< $@
	sed -e 's,sql/pgtap,sql/pgtap-core,g' compat/install-8.3.patch | patch -p0
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(PGTAP_VERSION),g' sql/pgtap-core.sql > sql/pgtap-core.tmp
	$(PERL) compat/gencore 0 sql/pgtap-core.tmp > sql/pgtap-core.sql
	rm sql/pgtap-core.tmp

sql/pgtap-schema.sql: sql/pgtap.sql.in
	cp $< $@
	sed -e 's,sql/pgtap,sql/pgtap-schema,g' compat/install-8.3.patch | patch -p0
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(PGTAP_VERSION),g' sql/pgtap-schema.sql > sql/pgtap-schema.tmp
	$(PERL) compat/gencore 1 sql/pgtap-schema.tmp > sql/pgtap-schema.sql
	rm sql/pgtap-schema.tmp

# Make sure that we build the regression tests.
installcheck: test/setup.sql

# In addition to installcheck, one can also run the tests through pg_prove.
test: test/setup.sql
	pg_prove --pset tuples_only=1 $(TESTS)

html:
	markdown -ftoc doc/pgtap.md > doc/pgtap.html
	./tocgen doc/pgtap.html 2> doc/toc.html
	perl -MPod::Simple::XHTML -E "my \$$p = Pod::Simple::XHTML->new; \$$p->html_header_tags('<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">'); \$$p->strip_verbatim_indent(sub { (my \$$i = \$$_[0]->[0]) =~ s/\\S.*//; \$$i }); \$$p->parse_from_file('`perldoc -l pg_prove`')" > doc/pg_prove.html


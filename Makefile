MAINEXT      = pgtap
EXTENSION    = $(MAINEXT)
EXTVERSION   = $(shell grep default_version $(MAINEXT).control | \
			   sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")
NUMVERSION   = $(shell echo $(EXTVERSION) | sed -e 's/\([[:digit:]]*[.][[:digit:]]*\).*/\1/')
DATA         = $(wildcard sql/*--*.sql)
TESTS        = $(wildcard test/sql/*.sql)
EXTRA_CLEAN  = sql/pgtap.sql sql/uninstall_pgtap.sql sql/pgtap-core.sql sql/pgtap-schema.sql doc/*.html
DOCS         = doc/pgtap.mmd
REGRESS      = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test --load-language=plpgsql
PG_CONFIG   ?= pg_config

ifdef NO_PGXS
top_builddir = ../..
PG_CONFIG := $(top_builddir)/src/bin/pg_config/pg_config
else
# Run pg_config to get the PGXS Makefiles
PGXS := $(shell $(PG_CONFIG) --pgxs)
endif

# We need to do various things with the PostgreSQLl version.
VERSION = $(shell $(PG_CONFIG) --version | awk '{print $$2}')

# We support 8.1 and later.
ifeq ($(shell echo $(VERSION) | grep -qE " 7[.]|8[.]0" && echo yes || echo no),yes)
$(error pgTAP requires PostgreSQL 8.1 or later. This is $(VERSION))
endif

# Compile the C code only if we're on 8.3 or older.
ifeq ($(shell echo $(VERSION) | grep -qE "8[.][123]" && echo yes || echo no),yes)
MODULES = src/pgtap
endif

# Load PGXS now that we've set all the variables it might need.
ifdef NO_PGXS
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
else
include $(PGXS)
endif

# We need Perl.
ifndef PERL
PERL := $(shell which perl)
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

# Enum tests not supported by 8.2 and earlier.
ifeq ($(shell echo $(VERSION) | grep -qE "8[.][12]" && echo yes || echo no),yes)
TESTS   := $(filter-out test/sql/enumtap.sql,$(TESTS))
REGRESS := $(filter-out enumtap,$(REGRESS))
endif

# Values tests not supported by 8.1 and earlier.
ifeq ($(shell echo $(VERSION) | grep -qE "8[.][1]" && echo yes || echo no),yes)
TESTS   := $(filter-out test/sql/enumtap.sql sql/valueset.sql,$(TESTS))
REGRESS := $(filter-out enumtap valueset,$(REGRESS))
endif

# Determine the OS. Borrowed from Perl's Configure.
OSNAME := $(shell $(SHELL) ./getos.sh)

# Make sure we build these.
all: sql/pgtap.sql sql/uninstall_pgtap.sql sql/pgtap-core.sql sql/pgtap-schema.sql

# Add extension build targets on 9.1 and up.
ifeq ($(shell echo $(VERSION) | grep -qE "8[.]|9[.]0" && echo no || echo yes),yes)
all: sql/$(MAINEXT)--$(EXTVERSION).sql sql/$(MAINEXT)-core--$(EXTVERSION).sql sql/$(MAINEXT)-schema--$(EXTVERSION).sql

sql/$(MAINEXT)--$(EXTVERSION).sql: sql/$(MAINEXT).sql
	cp $< $@

sql/$(MAINEXT)-core--$(EXTVERSION).sql: sql/$(MAINEXT)-core.sql
	cp $< $@

sql/$(MAINEXT)-schema--$(EXTVERSION).sql: sql/$(MAINEXT)-schema.sql
	cp $< $@

DATA = $(wildcard sql/*--*.sql)
EXTRA_CLEAN += sql/$(MAINEXT)--$(EXTVERSION).sql sql/$(MAINEXT)-core--$(EXTVERSION).sql sql/$(MAINEXT)-schema--$(EXTVERSION).sql
endif

sql/pgtap.sql: sql/pgtap.sql.in test/setup.sql
	cp $< $@
ifeq ($(shell echo $(VERSION) | grep -qE "9[.]0|8[.][1234]" && echo yes || echo no),yes)
	patch -p0 < compat/install-9.0.patch
endif
ifeq ($(shell echo $(VERSION) | grep -qE "8[.][1234]" && echo yes || echo no),yes)
	patch -p0 < compat/install-8.4.patch
endif
ifeq ($(shell echo $(VERSION) | grep -qE "8[.][123]" && echo yes || echo no),yes)
	patch -p0 < compat/install-8.3.patch
endif
ifeq ($(shell echo $(VERSION) | grep -qE "8[.][12]" && echo yes || echo no),yes)
	patch -p0 < compat/install-8.2.patch
endif
ifeq ($(shell echo $(VERSION) | grep -qE "8[.][1]" && echo yes || echo no),yes)
	patch -p0 < compat/install-8.1.patch
endif
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(NUMVERSION),g' sql/pgtap.sql > sql/pgtap.tmp
	mv sql/pgtap.tmp sql/pgtap.sql

sql/uninstall_pgtap.sql: sql/uninstall_pgtap.sql.in test/setup.sql
	cp sql/uninstall_pgtap.sql.in sql/uninstall_pgtap.sql
ifeq ($(shell echo $(VERSION) | grep -qE "8[.][123]" && echo yes || echo no),yes)
	patch -p0 < compat/uninstall-8.3.patch
endif
ifeq ($(shell echo $(VERSION) | grep -qE "8[.][12]" && echo yes || echo no),yes)
	patch -p0 < compat/uninstall-8.2.patch
endif

sql/pgtap-core.sql: sql/pgtap.sql.in
	cp $< $@
	sed -e 's,sql/pgtap,sql/pgtap-core,g' compat/install-8.4.patch | patch -p0
	sed -e 's,sql/pgtap,sql/pgtap-core,g' compat/install-8.3.patch | patch -p0
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(NUMVERSION),g' sql/pgtap-core.sql > sql/pgtap-core.tmp
	$(PERL) compat/gencore 0 sql/pgtap-core.tmp > sql/pgtap-core.sql
	rm sql/pgtap-core.tmp

sql/pgtap-schema.sql: sql/pgtap.sql.in
	cp $< $@
	sed -e 's,sql/pgtap,sql/pgtap-schema,g' compat/install-9.0.patch | patch -p0
	sed -e 's,sql/pgtap,sql/pgtap-schema,g' compat/install-8.4.patch | patch -p0
	sed -e 's,sql/pgtap,sql/pgtap-schema,g' compat/install-8.3.patch | patch -p0
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(NUMVERSION),g' sql/pgtap-schema.sql > sql/pgtap-schema.tmp
	$(PERL) compat/gencore 1 sql/pgtap-schema.tmp > sql/pgtap-schema.sql
	rm sql/pgtap-schema.tmp

# Make sure that we build the regression tests.
installcheck: test/setup.sql

# In addition to installcheck, one can also run the tests through pg_prove.
test: test/setup.sql
	pg_prove --pset tuples_only=1 $(TESTS)

html:
	multimarkdown doc/pgtap.mmd > doc/pgtap.html
	./tocgen doc/pgtap.html 2> doc/toc.html
	perl -MPod::Simple::XHTML -E "my \$$p = Pod::Simple::XHTML->new; \$$p->html_header_tags('<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">'); \$$p->strip_verbatim_indent(sub { my \$$l = shift; (my \$$i = \$$l->[0]) =~ s/\\S.*//; \$$i }); \$$p->parse_from_file('`perldoc -l pg_prove`')" > doc/pg_prove.html

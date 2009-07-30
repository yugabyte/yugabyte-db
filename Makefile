TESTS = $(wildcard sql/*.sql)
EXTRA_CLEAN = test_setup.sql *.html
DATA_built = pgtap.sql uninstall_pgtap.sql
DOCS = README.pgtap
SCRIPTS = bbin/pg_prove
REGRESS = $(patsubst sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --load-language=plpgsql

# Figure out where pg_config is, and set other vars we'll need depending on
# whether or not we're in the source tree.
ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
else
top_builddir = ../..
PG_CONFIG := $(top_builddir)/src/bin/pg_config/pg_config
endif

# We need to do various things with various versions of PostgreSQL.
VERSION     = $(shell $(PG_CONFIG) --version | awk '{print $$2}')
PGVER_MAJOR = $(shell echo $(VERSION) | awk -F. '{ print ($$1 + 0) }')
PGVER_MINOR = $(shell echo $(VERSION) | awk -F. '{ print ($$2 + 0) }')
PGVER_PATCH = $(shell echo $(VERSION) | awk -F. '{ print ($$3 + 0) }')
PGTAP_VERSION = 0.22

# Compile the C code only if we're on 8.3 or older.
ifneq ($(PGVER_MINOR), 4)
MODULES = pgtap
endif

# Load up the PostgreSQL makefiles.
ifdef PGXS
include $(PGXS)
else
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# We need Perl.
ifndef PERL
PERL := $(shell which foo)
endif

# Is TAP::Harness installed?
ifdef PERL
HAVE_HARNESS := $(shell $(PERL) -le 'eval { require TAP::Harness }; print 1 unless $$@' )
endif

# We support 8.0 and later.
ifneq ($(PGVER_MAJOR), 8)
$(error pgTAP requires PostgreSQL 8.0 or later. This is $(VERSION))
endif

# Set up extra substitutions based on version numbers.
ifeq ($(PGVER_MAJOR), 8)
ifeq ($(PGVER_MINOR), 0)
# Hack for E'' syntax (<= PG8.0)
EXTRA_SUBS := -e "s/ E'/ '/g"
# Throw, runtests, enums, and roles aren't supported in 8.0.
TESTS := $(filter-out sql/throwtap.sql sql/runtests.sql sql/enumtap.sql sql/roletap.sql,$(TESTS))
REGRESS := $(filter-out throwtap runtests enumtap roletap,$(REGRESS))
endif
ifeq ($(PGVER_MINOR), 4)
# Do nothing.
else
ifneq ($(PGVER_MINOR), 3)
# Enum tests not supported by 8.2 and earlier.
TESTS := $(filter-out sql/enumtap.sql,$(TESTS))
REGRESS := $(filter-out enumtap,$(REGRESS))
endif
ifneq ($(PGVER_MINOR), 2)
# Values tests not supported by 8.1 and earlier.
TESTS := $(filter-out sql/valueset.sql,$(TESTS))
REGRESS := $(filter-out valueset,$(REGRESS))
endif
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
OSNAME = $(shell uname | awk '{print tolower($$1)}')
endif

# Override how .sql targets are processed to add the schema info, if
# necessary. Otherwise just copy the files.
test_setup.sql: test_setup.sql.in
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' $< >$@
else
	cp $< $@
endif

pgtap.sql: pgtap.sql.in test_setup.sql
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(PGTAP_VERSION),g' $(EXTRA_SUBS) pgtap.sql.in > pgtap.sql
else
	sed -e 's,MODULE_PATHNAME,$$libdir/pgtap,g' -e 's,__OS__,$(OSNAME),g' -e 's,__VERSION__,$(PGTAP_VERSION),g' $(EXTRA_SUBS) pgtap.sql.in > pgtap.sql
endif
ifeq ($(PGVER_MAJOR), 8)
ifneq ($(PGVER_MINOR), 4)
ifneq ($(PGVER_MINOR), 0)
	patch -p0 < compat/install-8.3.patch
endif
ifneq ($(PGVER_MINOR), 3)
	cat compat/install-8.2.sql >> pgtap.sql
ifeq ($(PGVER_MINOR), 2)
	patch -p0 < compat/install-8.2.patch
else
ifeq ($(PGVER_MINOR), 1)
	patch -p0 < compat/install-8.2.patch
	patch -p0 < compat/install-8.1.patch
else
	patch -p0 < compat/install-8.0.patch
endif
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
ifneq ($(PGVER_MINOR), 4)
	patch -p0 < compat/uninstall-8.3.patch
ifneq ($(PGVER_MINOR), 3)
	patch -p0 < compat/uninstall-8.2.patch
	mv uninstall_pgtap.sql uninstall_pgtap.tmp
	cat compat/uninstall-8.2.sql uninstall_pgtap.tmp >> uninstall_pgtap.sql
	rm uninstall_pgtap.tmp
endif
ifeq ($(PGVER_MINOR), 0)
#	patch -p0 < compat/uninstall-8.0.patch
	mv uninstall_pgtap.sql uninstall_pgtap.tmp
	cat compat/uninstall-8.0.sql uninstall_pgtap.tmp >> uninstall_pgtap.sql
	rm uninstall_pgtap.tmp
endif
endif
endif

# Build pg_prove and holler if there's no Perl or TAP::Harness.
bbin/pg_prove:
	mkdir bbin
#	sed -e "s,\\('\\|F<\\)psql\\(['>]\\),\\1$(bindir)/psql\\2,g" bin/pg_prove > bbin/pg_prove
	sed -e "s,\\'psql\\',\\'$(bindir)/psql\\'," -e 's,F<psql>,F<$(bindir)/psql>,' bin/pg_prove > bbin/pg_prove
ifdef PERL
	$(PERL) '-MExtUtils::MY' -e 'MY->fixin(shift)' bbin/pg_prove
ifndef HAVE_HARNESS
	$(warning To use pg_prove, TAP::Harness Perl module must be installed from CPAN.)
endif
else
	$(warning Could not find perl (required by pg_prove). Install it or set the PERL variable.)
endif
	chmod +x bbin/pg_prove

# Make sure that we build the regression tests.
installcheck: test_setup.sql

# Make sure we build pg_prove.
all: bbin/pg_prove

# Make sure we remove bbin.
clean: extraclean

extraclean:
	rm -rf bbin

# In addition to installcheck, one can also run the tests through pg_prove.
test: test_setup.sql bbin/pg_prove
	./bbin/pg_prove --pset tuples_only=1 $(TESTS)

html:
	markdown -F 0x1000 README.pgtap > readme.html
	perl -ne 'BEGIN { $$prev = 0; $$lab = ""; print "<h1>Contents</h1>\n<ul>\n" } if (m{<h([123])\s+id="([^"]+)">((<code>[^(]+)?.+?)</h\1>}) { next if $$lab && $$lab eq $$4; $$lab = $$4; if ($$prev) { if ($$1 != $$prev) { print $$1 > $$prev ? $$1 - $$prev > 1 ? "<ul><li><ul>" : "<ul>\n" : $$prev - $$1 > 1 ? "</li></ul></li></ul></li>\n" : "</li></ul></li>\n"; $$prev = $$1; } else { print "</li>\n" } } else { $$prev = $$1; } print qq{<li><a href="#$$2">} . ($$4 ? "$$4()</code>" : $$3) . "</a>" } END { print "</li>\n</ul>\n" }' readme.html > toc.html
	PERL5LIB=/Users/david/dev/perl/Pod-Simple/lib perldoc -MPod::Simple::XHTML -d pg_prove.html -w html_header_tags:'<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">' bin/pg_prove

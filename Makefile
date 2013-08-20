MODULE_big = orafunc
OBJS= convert.o file.o datefce.o magic.o others.o plvstr.o plvdate.o shmmc.o plvsubst.o utility.o plvlex.o alert.o pipe.o sqlparse.o putline.o assert.o plunit.o random.o aggregate.o

EXTENSION = orafce

DATA_built = orafunc.sql
DATA = uninstall_orafunc.sql orafce--3.0.5.sql orafce--unpackaged--3.0.5.sql
DOCS = README.asciidoc COPYRIGHT.orafunc INSTALL.orafunc
REGRESS = orafunc dbms_output files dbms_utility
REGRESS_OPTS = --load-language=plpgsql --load-extension=orafce --schedule=parallel_schedule

EXTRA_CLEAN = sqlparse.c sqlparse.h sqlscan.c y.tab.c y.tab.h orafunc.sql.in

ifndef USE_PGXS
top_builddir = ../..
makefile_global = $(top_builddir)/src/Makefile.global
ifeq "$(wildcard $(makefile_global))" ""
USE_PGXS = 1	# use pgxs if not in contrib directory
endif
endif

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/$(MODULE_big)
include $(makefile_global)
include $(top_srcdir)/contrib/contrib-global.mk
endif

ifeq ($(enable_nls), yes)
ifeq ($(PORTNAME),win32)
SHLIB_LINK += -lintl
else
SHLIB_LINK += -L$(libdir)/gettextlib
endif
endif

# remove dependency to libxml2 and libxslt
LIBS := $(filter-out -lxml2, $(LIBS))
LIBS := $(filter-out -lxslt, $(LIBS))

plvlex.o: sqlparse.o

sqlparse.o: $(srcdir)/sqlscan.c

$(srcdir)/sqlparse.h: $(srcdir)/sqlparse.c ;

$(srcdir)/sqlparse.c: sqlparse.y
ifdef BISON
	$(BISON) -d $(BISONFLAGS) -o $@ $<
else
ifdef YACC
	$(YACC) -d $(YFLAGS) -p cube_yy $<
	mv -f y.tab.c sqlparse.c
	mv -f y.tab.h sqlparse.h
else
	bison -d $(BISONFLAGS) -o $@ $<
endif
endif

$(srcdir)/sqlscan.c: sqlscan.l
ifdef FLEX
	$(FLEX) $(FLEXFLAGS) -o'$@' $<
else
	flex $(FLEXFLAGS) -o'$@' $<
endif

distprep: $(srcdir)/sqlparse.c $(srcdir)/sqlscan.c

maintainer-clean:
	rm -f $(srcdir)/sqlparse.c $(srcdir)/sqlscan.c

ifndef MAJORVERSION
MAJORVERSION := $(basename $(VERSION))
endif

orafunc.sql.in:
	cat orafunc-common.sql orafunc-$(MAJORVERSION).sql > orafunc.sql.in

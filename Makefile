MODULE_big = orafunc
OBJS= convert.o file.o datefce.o magic.o others.o plvstr.o plvdate.o shmmc.o plvsubst.o utility.o plvlex.o alert.o pipe.o sqlparse.o putline.o assert.o plunit.o random.o aggregate.o

EXTENSION = orafce

DATA_built = orafunc.sql
DATA = uninstall_orafunc.sql orafce--3.0.6.sql orafce--unpackaged--3.0.6.sql
DOCS = README.asciidoc COPYRIGHT.orafunc INSTALL.orafunc

PG_CONFIG ?= pg_config

# version as a number, e.g. 9.1.4 -> 901
VERSION := $(shell $(PG_CONFIG) --version | awk '{print $$2}')
INTVERSION := $(shell echo $$(($$(echo $(VERSION) | sed 's/\([[:digit:]]\{1,\}\)\.\([[:digit:]]\{1,\}\).*/\1*100+\2/' ))))

REGRESS = orafunc dbms_output dbms_utility files

ifeq ($(shell echo $$(($(INTVERSION) >= 804))),1)
REGRESS += aggregates nlssort dbms_random
endif

REGRESS_OPTS = --load-language=plpgsql --schedule=parallel_schedule
REGRESSION_EXPECTED = expected/orafunc.out expected/dbms_pipe_session_B.out

ifeq ($(shell echo $$(($(INTVERSION) <= 802))),1)
$(REGRESSION_EXPECTED): %.out: %1.out
	cp $< $@
else
$(REGRESSION_EXPECTED): %.out: %2.out
	cp $< $@
endif

installcheck: $(REGRESSION_EXPECTED)

EXTRA_CLEAN = sqlparse.c sqlparse.h sqlscan.c y.tab.c y.tab.h orafunc.sql.in expected/orafunc.out expected/dbms_pipe_session_B.out

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
	if [ -f orafunc-$(MAJORVERSION).sql ] ; \
	then \
	cat orafunc-common.sql orafunc-$(MAJORVERSION).sql > orafunc.sql.in; \
	else \
	cat orafunc-common.sql orafunc-common-2.sql > orafunc.sql.in; \
	fi

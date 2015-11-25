MODULE_big = orafce
OBJS= convert.o file.o datefce.o magic.o others.o plvstr.o plvdate.o shmmc.o plvsubst.o utility.o plvlex.o alert.o pipe.o sqlparse.o putline.o assert.o plunit.o random.o aggregate.o orafce.o varchar2.o nvarchar2.o charpad.o charlen.o

EXTENSION = orafce

DATA = orafce--3.1.sql orafce--unpackaged--3.1.sql
DOCS = README.asciidoc COPYRIGHT.orafce INSTALL.orafce

PG_CONFIG ?= pg_config

# version as a number, e.g. 9.1.4 -> 901
VERSION := $(shell $(PG_CONFIG) --version | awk '{print $$2}')
INTVERSION := $(shell echo $$(($$(echo $(VERSION) | sed 's/\([[:digit:]]\{1,\}\)\.\([[:digit:]]\{1,\}\).*/\1*100+\2/' ))))

# make "all" the default target
all:

REGRESS = orafce dbms_output dbms_utility files varchar2 nvarchar2 aggregates nlssort dbms_random

REGRESS_OPTS = --load-language=plpgsql --schedule=parallel_schedule --encoding=utf8

EXTRA_CLEAN = sqlparse.c sqlparse.h sqlscan.c y.tab.c y.tab.h

#override CFLAGS += -pedantic

ifdef NO_PGXS
subdir = contrib/$(MODULE_big)
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
else
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
endif

ifeq ($(enable_nls), yes)
SHLIB_LINK += $(filter -lintl,$(LIBS))
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

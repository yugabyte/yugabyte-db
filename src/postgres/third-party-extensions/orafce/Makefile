MODULE_big = orafce
OBJS= regexp.o\
		parse_keyword.o\
		convert.o file.o\
		datefce.o\
		magic.o\
		others.o\
		plvstr.o\
		plvdate.o\
		shmmc.o\
		plvsubst.o\
		utility.o\
		plvlex.o\
		alert.o\
		pipe.o\
		sqlparse.o\
		putline.o\
		assert.o\
		plunit.o\
		random.o\
		aggregate.o\
		orafce.o\
		varchar2.o\
		nvarchar2.o\
		charpad.o\
		charlen.o\
		replace_empty_string.o\
		math.o\
		dbms_sql.o

EXTENSION = orafce

DATA = orafce--4.9.sql\
		orafce--3.2--3.3.sql\
		orafce--3.3--3.4.sql\
		orafce--3.4--3.5.sql\
		orafce--3.5--3.6.sql\
		orafce--3.6--3.7.sql\
		orafce--3.7--3.8.sql\
		orafce--3.8--3.9.sql\
		orafce--3.9--3.10.sql\
		orafce--3.10--3.11.sql\
		orafce--3.11--3.12.sql\
		orafce--3.12--3.13.sql\
		orafce--3.13--3.14.sql\
		orafce--3.14--3.15.sql\
		orafce--3.15--3.16.sql\
		orafce--3.16--3.17.sql\
		orafce--3.17--3.18.sql\
		orafce--3.18--3.19.sql\
		orafce--3.19--3.20.sql\
		orafce--3.20--3.21.sql\
		orafce--3.21--3.22.sql\
		orafce--3.22--3.23.sql\
		orafce--3.23--3.24.sql\
		orafce--3.24--3.25.sql\
		orafce--3.25--4.0.sql\
		orafce--4.0--4.1.sql\
		orafce--4.1--4.2.sql\
		orafce--4.2--4.3.sql\
		orafce--4.3--4.4.sql\
		orafce--4.4--4.5.sql\
		orafce--4.5--4.6.sql\
		orafce--4.6--4.7.sql\
		orafce--4.7--4.8.sql\
		orafce--4.8--4.9.sql


DOCS = README.asciidoc COPYRIGHT.orafce INSTALL.orafce

PG_CONFIG ?= pg_config

# make "all" the default target
all:

REGRESS = orafce\
		orafce2\
		dbms_output\
		dbms_utility\
		files\
		varchar2\
		nvarchar2\
		aggregates\
		nlssort\
		dbms_random\
		regexp_func\
		dbms_sql

#REGRESS_OPTS = --load-language=plpgsql --schedule=parallel_schedule --encoding=utf8
REGRESS_OPTS = --schedule=parallel_schedule --encoding=utf8

SHLIB_LINK += -L$(YB_BUILD_ROOT)/lib -lyb_pggate

# override CFLAGS += -Wextra -Wimplicit-fallthrough=0

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
	rm -f $(srcdir)/sqlparse.c $(srcdir)/sqlscan.c $(srcdir)/sqlparse.h $(srcdir)/y.tab.c $(srcdir)/y.tab.h

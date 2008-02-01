# $PostgreSQL: pgsql/contrib/spi/Makefile,v 1.28 2007/12/03 04:22:54 tgl Exp $

MODULES = ora_date ora_string ora_lex ora_basic ora_file ora_interprocess
DATA_built = $(addsuffix .sql, $(MODULES))
DOCS = $(addsuffix .example, $(MODULES))

ora_date: plvdate.o datefce.o

ora_basic: putline.o

ora_file: file.o

ora_interprocess: alert.o pipe.o shmc.o

ora_lex: plvlex.o

ora_string: plvstr.o plvsubst.o others.o



# this is needed for the regression tests;
# comment out if you want a quieter refint package for other uses
PG_CPPFLAGS = -DREFINT_VERBOSE

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/spi
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

SHLIB_LINK += -L$(top_builddir)/src/port -lpgport

plvlex.o: sqlparse.o

sqlparse.o: sqlscan.c

sqlparse.c: sqlparse.h ;

sqlparse.h: sqlparse.y
	$(YACC) -d $(YFLAGS) -p orafce_sql_yy $<
	mv -f y.tab.c sqlparse.c
	mv -f y.tab.h sqlparse.h

sqlscan.c: sqlscan.l
	$(FLEX) $(FLEXFLAGS) -o'$@' $<

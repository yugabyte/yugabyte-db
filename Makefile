MODULE_big = orafunc
OBJS= convert.o file.o datefce.o magic.o others.o plvstr.o plvdate.o shmmc.o plvsubst.o utility.o plvlex.o alert.o pipe.o sqlparse.o putline.o assert.o plunit.o

DATA_built = orafunc.sql
DATA = uninstall_orafunc.sql 
DOCS = README.orafunc COPYRIGHT.orafunc INSTALL.orafunc
REGRESS = orafunc dbms_output files
REGRESS_OPTS = --load-language=plpgsql

EXTRA_CLEAN = sqlparse.c sqlparse.h sqlscan.c y.tab.c y.tab.h 

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/orafce
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

ifeq (,$(FLEX))
FLEX = flex
endif

ifeq (,$(YACC))
YACC = yacc
endif

plvlex.o: sqlparse.o

sqlparse.o: sqlscan.c                                                                                                                      
                                                                                                                                               
sqlparse.c: sqlparse.h ;                                                                                                                   
                                                                                                                                               
sqlparse.h: sqlparse.y                                                                                                                     
	$(YACC) -d $(YFLAGS) -p orafce_sql_yy $<                                                                                                    
	mv -f y.tab.c sqlparse.c                                                                                                             
	mv -f y.tab.h sqlparse.h                                                                                                             
                                                                                                                                               
sqlscan.c: sqlscan.l                                                                                                                       
	$(FLEX) $(FLEXFLAGS) -o'$@' $<                                                                                                         

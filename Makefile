ifdef XSLT
xslt=xslt.c
endif

MODULE_big = orafunc
OBJS= backport.o file.o datefce.o others.o plvstr.o plvdate.o shmmc.o plvsubst.o utility.o plvlex.o alert.o pipe.o magic.o   sqlparse.o putline.o xslprocessor.o

DATA_built = orafunc.sql
DOCS = README.orafunc COPYRIGHT.orafunc INSTALL.orafunc
REGRESS = orafunc files
REGRESS_OPTS = --load-language=plpgsql

EXTRA_CLEAN = sqlparse.c sqlparse.h sqlscan.c y.tab.c y.tab.h 

ifdef USE_PGXS
PGXS = $(shell pg_config --pgxs)
include $(PGXS)
else
subdir = contrib/orafce
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

plvlex.o: sqlparse.o

sqlparse.o: sqlscan.c                                                                                                                      
                                                                                                                                               
sqlparse.c: sqlparse.h ;                                                                                                                   
                                                                                                                                               
sqlparse.h: sqlparse.y                                                                                                                     
ifdef YACC                                                                                                                                     
	$(YACC) -d $(YFLAGS) -p orafce_sql_yy $<                                                                                                    
	mv -f y.tab.c sqlparse.c                                                                                                             
	mv -f y.tab.h sqlparse.h                                                                                                             
else                                                                                                                                           
	@$(missing) bison $< $@                                                                                                                
endif                                                                                                                                          
                                                                                                                                               
sqlscan.c: sqlscan.l                                                                                                                       
ifdef FLEX                                                                                                                                     
	$(FLEX) $(FLEXFLAGS) -o'$@' $<                                                                                                         
else                                                                                                                                           
	@$(missing) flex $< $@                                                                                                                 
endif                                           


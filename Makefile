MODULE_big = orafunc
OBJS= file.o datefce.o others.o putline.o pipe.o plvdate.o shmmc.o plvstr.o alert.o magic.o plvsubst.o plvlex.o utility.o sqlparse.o

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
MODULE_big = orafunc
OBJS= datefce.o protectable.o putline.o others.o

DATA_built = orafunc.sql
DOCS = README.orafunc
REGRESS = orafunc

EXTRA_CLEAN = 

ifdef USE_PGXS
PGXS = $(shell pg_config --pgxs)
include $(PGXS)
else
subdir = contrib/orafunc
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

orafunc.o: datefunc.c 

protectable.o: protectable.c

putline.o: putline.c

others.o: others.c
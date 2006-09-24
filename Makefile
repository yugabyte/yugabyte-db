MODULE_big = orafunc
OBJS= datefce.o others.o putline.o pipe.o plvdate.o shmmc.o plvstr.o alert.o magic.o plvsubst.o

DATA_built = orafunc.sql
DOCS = README.orafunc
REGRESS = orafunc

EXTRA_CLEAN = 

ifdef USE_PGXS
PGXS = $(shell pg_config --pgxs)
include $(PGXS)
else
subdir = contrib/orafce
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif


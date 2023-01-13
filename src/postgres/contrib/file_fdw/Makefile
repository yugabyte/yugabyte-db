# contrib/file_fdw/Makefile

MODULE_big = file_fdw

OBJS = file_fdw.o $(WIN32RES)

EXTENSION = file_fdw
DATA = file_fdw--1.0.sql
PGFILEDESC = "file_fdw - foreign data wrapper for files"

REGRESS = file_fdw

SHLIB_LINK = -L$(YB_BUILD_ROOT)/lib -lyb_pggate

EXTRA_CLEAN = sql/file_fdw.sql expected/file_fdw.out

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/file_fdw
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

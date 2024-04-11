# contrib/yb_ycql_utils/Makefile

PGFILEDESC = "yb_ycql_utils - An extension for fetching YCQL data in PG side."

MODULE_big = yb_ycql_utils
OBJS = yb_ycql_utils.o $(WIN32RES)

EXTENSION = yb_ycql_utils
DATA = yb_ycql_utils--1.0.sql
SHLIB_LINK += -L$(YB_BUILD_ROOT)/lib -lyb_pggate -lyb_pggate_util

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/yb_ycql_utils
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

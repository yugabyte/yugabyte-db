# contrib/yb_fdw/Makefile

MODULES = yb_fdw

EXTENSION = yb_fdw
DATA = yb_fdw--1.0.sql
PGFILEDESC = "yb_fdw - foreign data wrapper for files"

REGRESS = yb_fdw

EXTRA_CLEAN = sql/yb_fdw.sql expected/yb_fdw.out

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/yb_fdw
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

LDFLAGS += -L$(YB_BUILD_ROOT)/lib -lyb_util -lyb_pggate

# contrib/yb_ash/Makefile

MODULE_big = yb_ash
OBJS = yb_ash.o $(WIN32RES)

EXTENSION = yb_ash
DATA =
PGFILEDESC = "yb_ash - Yugabyte Active Session History"
SHLIB_LINK += -L$(YB_BUILD_ROOT)/lib -lyb_pggate -lyb_pggate_util

LDFLAGS_SL += $(filter -lm, $(LIBS))

REGRESS_OPTS = --temp-config $(top_srcdir)/contrib/yb_ash/yb_ash.conf
REGRESS = yb_ash

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/yb_ash
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

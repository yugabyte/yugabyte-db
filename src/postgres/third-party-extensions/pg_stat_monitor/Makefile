# contrib/pg_stat_monitor/Makefile

MODULE_big = pg_stat_monitor
OBJS = hash_query.o guc.o pg_stat_monitor.o $(WIN32RES)

EXTENSION = pg_stat_monitor
DATA = pg_stat_monitor--1.0.sql

PGFILEDESC = "pg_stat_monitor - execution statistics of SQL statements"
SHLIB_LINK += -L$(YB_BUILD_ROOT)/lib -lserver_process -lyb_pggate_webserver

LDFLAGS_SL += $(filter -lm, $(LIBS)) 

REGRESS_OPTS = --temp-config $(top_srcdir)/contrib/pg_stat_monitor/pg_stat_monitor.conf --inputdir=regression
REGRESS = basic version guc counters relations database top_query application_name cmd_type error state rows 

# Disabled because these tests require "shared_preload_libraries=pg_stat_statements",
# which typical installcheck users do not have (e.g. buildfarm clients).
# NO_INSTALLCHECK = 1

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_stat_monitor
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

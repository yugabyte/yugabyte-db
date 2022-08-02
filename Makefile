# contrib/pg_stat_monitor/Makefile

MODULE_big = pg_stat_monitor
OBJS = hash_query.o guc.o pg_stat_monitor.o $(WIN32RES)

EXTENSION = pg_stat_monitor
DATA = pg_stat_monitor--1.0.sql

PGFILEDESC = "pg_stat_monitor - execution statistics of SQL statements"

LDFLAGS_SL += $(filter -lm, $(LIBS)) 

TAP_TESTS = 1
REGRESS_OPTS = --temp-config $(top_srcdir)/contrib/pg_stat_monitor/pg_stat_monitor.conf --inputdir=regression
REGRESS = basic version guc counters relations database error_insert application_name application_name_unique top_query cmd_type error rows tags histogram

# Disabled because these tests require "shared_preload_libraries=pg_stat_statements",
# which typical installcheck users do not have (e.g. buildfarm clients).
# NO_INSTALLCHECK = 1

PG_CONFIG = pg_config
PGSM_INPUT_SQL_VERSION := 1.0

ifdef USE_PGXS
MAJORVERSION := $(shell pg_config --version | awk {'print $$2'} | cut -f1 -d".")
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_stat_monitor
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

ifeq ($(shell test $(MAJORVERSION) -gt 12; echo $$?),0)
PGSM_INPUT_SQL_VERSION := ${PGSM_INPUT_SQL_VERSION}.${MAJORVERSION}
endif

$(info Using pg_stat_monitor--${PGSM_INPUT_SQL_VERSION}.sql.in file to generate sql filea.)

ifneq (,$(wildcard ../pg_stat_monitor--${PGSM_INPUT_SQL_VERSION}.sql.in))
  CP := $(shell cp -v ../pg_stat_monitor--${PGSM_INPUT_SQL_VERSION}.sql.in ../pg_stat_monitor--1.0.sql)
endif
ifneq (,$(wildcard pg_stat_monitor--${PGSM_INPUT_SQL_VERSION}.sql.in))
  CP := $(shell cp -v pg_stat_monitor--${PGSM_INPUT_SQL_VERSION}.sql.in pg_stat_monitor--1.0.sql)
endif

clean:
	rm -rf ${DATA}
	rm -rf t/results

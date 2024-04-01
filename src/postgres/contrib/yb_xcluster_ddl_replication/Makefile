# contrib/yb_xcluster_ddl_replication/Makefile

PGFILEDESC = "yb_xcluster_ddl_replication - YugaByte xCluster DDL replication"

EXTENSION = yb_xcluster_ddl_replication
DATA = yb_xcluster_ddl_replication--1.0.sql

MODULES = yb_xcluster_ddl_replication

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/yb_xcluster_ddl_replication
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

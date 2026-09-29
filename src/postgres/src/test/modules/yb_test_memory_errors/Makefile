# src/test/modules/yb_test_memory_errors/Makefile

MODULES = yb_test_memory_errors
PGFILEDESC = "yb_test_memory_errors - inject memory errors to test their detection"

EXTENSION = yb_test_memory_errors
DATA = yb_test_memory_errors--1.0.sql

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = src/test/modules/yb_test_memory_errors
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# contrib/yb_test_extension/Makefile

PGFILEDESC = "yb_test_extension - An extension for testing purposes only. \
			  Different kinds of database objects supported by YB are created \
			  in this extension to improve test coverage."

EXTENSION = yb_test_extension
DATA = yb_test_extension--1.0.sql

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/yb_test_extension
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

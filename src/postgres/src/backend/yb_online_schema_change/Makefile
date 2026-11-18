#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for yb_online_schema_change
#
# IDENTIFICATION
#    src/backend/yb_online_schema_change/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/yb_online_schema_change
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

OBJS = \
	yb_osc_backfill.o \
	yb_osc_transform.o \
	yb_osc_trigger.o \
	yb_osc_util.o

include $(top_srcdir)/src/backend/common.mk

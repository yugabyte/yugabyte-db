#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for access/yb_scan
#
# IDENTIFICATION
#    src/backend/access/yb_scan/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/access/yb_scan
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

OBJS = \
	yb_catalog_scan.o \
	yb_sample_scan.o \
	yb_scan_core.o \
	yb_table_scan.o \
	yb_ybctid_scan.o

include $(top_srcdir)/src/backend/common.mk

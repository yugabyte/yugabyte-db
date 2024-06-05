#
# Makefile for the access methods module
#
# src/backend/access/Makefile
#

subdir = src/backend/access
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

# ybc folder contains utilites for YugaByte access method (used for sys catalog)
SUBDIRS	    = brin common gin gist hash heap index nbtree rmgrdesc spgist \
			  table tablesample transam yb_access ybgin

include $(top_srcdir)/src/backend/common.mk

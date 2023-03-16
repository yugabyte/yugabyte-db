#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for ybgate
#
# IDENTIFICATION
#    src/backend/ybgate/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/ybgate
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

OBJS = ybgate_api.o ybgate_status.o
ifneq ($(YB_DO_NOT_BUILD_TESTS), 1)
OBJS += ybgate_api-test.o
endif

include $(top_srcdir)/src/backend/common.mk

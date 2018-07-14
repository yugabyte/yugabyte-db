# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.

MODULES = yb_fdw

EXTENSION = yb_fdw
DATA = yb_fdw--1.0.sql
PGFILEDESC = "yb_fdw - foreign data wrapper for files"

REGRESS = yb_fdw

EXTRA_CLEAN = sql/yb_fdw.sql expected/yb_fdw.out

YB_APPEND_CFLAGS = \
	-Werror=implicit-function-declaration \
	-Werror=unused-result

ifndef YB_COMPILER_TYPE
$(error YB_COMPILER_TYPE is not set)
endif

ifndef YB_BUILD_TYPE
$(error YB_BUILD_TYPE is not set)
endif

ifeq ($(YB_COMPILER_TYPE), "clang")
YB_APPEND_CFLAGS += -Werror=unknown-warning-option
endif

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/yb_fdw
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

	LDFLAGS += -L$(YB_BUILD_ROOT)/lib -lyb_util -lyb_pggate

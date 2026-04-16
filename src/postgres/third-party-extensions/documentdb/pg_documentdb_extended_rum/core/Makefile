MODULE_big = pg_documentdb_extended_rum_core
EXTVERSION = 1.0
PGFILEDESC = "DocumentDB Extended RUM index access method core implementation"

OSS_SRC_DIR = ../../

SOURCES = $(wildcard src/*.c)
OBJS = $(patsubst %.c,%.o,$(SOURCES)) $(WIN32RES)

# INCLUDES = rum.h rumsort.h
RELATIVE_INCLUDES = $(addprefix src/, $(INCLUDES))

LDFLAGS_SL += $(filter -lm, $(LIBS))

ifeq ($(CC),gcc)
    PG_CFLAGS = -std=gnu99 -Wall -Wextra -Werror -Wno-declaration-after-statement -Wno-unused-parameter -Wno-implicit-fallthrough -Wno-missing-variable-declarations -fno-lto $(LIBBSON) $(INTEL_DECIMAL_MATH_LIB) $(PCRE2_LIB)
else
    PG_CFLAGS = -std=gnu99 -Wall -Wextra -Werror -Wno-declaration-after-statement -Wno-unused-parameter -Wno-implicit-fallthrough -Wno-missing-variable-declarations $(LIBBSON) $(INTEL_DECIMAL_MATH_LIB) $(PCRE2_LIB)
endif

PG_CFLAGS += -Wno-sign-compare -Wno-type-limits
PG_LDFLAGS += -Wl,-rpath=$(shell $(PG_CONFIG) --pkglibdir) -fno-lto

include $(OSS_SRC_DIR)/Makefile.global

# Default to hidden visibility
override LDFLAGS_SL += -fvisibility=hidden -DHAVE_VISIBILITY_ATTRIBUTE=1
override CFLAGS += -fvisibility=hidden  -DHAVE_VISIBILITY_ATTRIBUTE=1
override CXXFLAGS += -fvisibility=hidden  -DHAVE_VISIBILITY_ATTRIBUTE=1

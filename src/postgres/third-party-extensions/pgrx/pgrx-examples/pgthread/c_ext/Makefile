MODULE_big = c_ext
OBJS = \
	$(WIN32RES) \
	c_ext.o

EXTENSION = c_ext
PGFILEDESC = "c_ext - pgrx example"

#FOO = $(shell set)
#$(error $(FOO))
ifdef USE_PGXS
PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
$(error requires USE_PGXS=1 to compile)
endif

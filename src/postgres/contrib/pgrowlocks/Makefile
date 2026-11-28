# contrib/pgrowlocks/Makefile

MODULE_big	= pgrowlocks
OBJS = \
	$(WIN32RES) \
	pgrowlocks.o

EXTENSION = pgrowlocks
DATA = pgrowlocks--1.2.sql pgrowlocks--1.1--1.2.sql pgrowlocks--1.0--1.1.sql
PGFILEDESC = "pgrowlocks - display row locking information"

# YB: pgrowlocks reads heap pages and tuple headers directly, and YugabyteDB
# relations have none, so the extension is no longer offered.  Install only
# the library, not the control and SQL files, so that it can no longer be
# created.  The library stays so that databases that already have the
# extension, and backups of them, which recreate its C functions, keep
# working.
EXTENSION =
DATA =

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pgrowlocks
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

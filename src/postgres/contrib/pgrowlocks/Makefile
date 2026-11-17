# contrib/pgrowlocks/Makefile

MODULE_big	= pgrowlocks
OBJS = \
	$(WIN32RES) \
	pgrowlocks.o

EXTENSION = pgrowlocks
DATA = pgrowlocks--1.2.sql pgrowlocks--1.1--1.2.sql pgrowlocks--1.0--1.1.sql
PGFILEDESC = "pgrowlocks - display row locking information"

# YB: Install only the library, not the control and SQL files, so that the
# extension can no longer be created.  The library stays so that databases
# that already have the extension, and backups of them, keep working.
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

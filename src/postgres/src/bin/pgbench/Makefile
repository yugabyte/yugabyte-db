# src/bin/pgbench/Makefile

PGFILEDESC = "ysql_bench - a simple program for running benchmark tests"
PGAPPICON = win32

subdir = src/bin/pgbench
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

OBJS = \
	$(WIN32RES) \
	exprparse.o \
	exprscan.o \
	pgbench.o

override CPPFLAGS := -I. -I$(srcdir) -I$(libpq_srcdir) $(CPPFLAGS)
LDFLAGS_INTERNAL += -L$(top_builddir)/src/fe_utils -lpgfeutils $(libpq_pgport) -L$(YB_BUILD_ROOT)/lib -lserver_process -lyb_pggate_util -lysql_bench_metrics_handler

ifneq ($(PORTNAME), win32)
override CFLAGS += $(PTHREAD_CFLAGS)
else
override CPPFLAGS += -DFD_SETSIZE=1024
endif
LIBS += $(PTHREAD_LIBS) -lyb_util $(YB_PG_EXE_LD_FLAGS_AFTER_YB_LIBS)


all: ysql_bench

ysql_bench: $(OBJS) | submake-libpq submake-libpgport submake-libpgfeutils
	$(YB_CCLD) $(CFLAGS) $^ $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)

# See notes in src/backend/parser/Makefile about the following two rules
exprparse.h: exprparse.c
	touch $@

exprparse.c: BISONFLAGS += -d

# Force these dependencies to be known even without dependency info built:
exprparse.o exprscan.o: exprparse.h

install: all installdirs
	$(INSTALL_PROGRAM) ysql_bench$(X) '$(DESTDIR)$(bindir)/ysql_bench$(X)'

installdirs:
	$(MKDIR_P) '$(DESTDIR)$(bindir)'

uninstall:
	rm -f '$(DESTDIR)$(bindir)/ysql_bench$(X)'

clean distclean:
	rm -f ysql_bench$(X) $(OBJS)
	rm -rf tmp_check
	rm -f exprparse.h exprparse.c exprscan.c

check:
	$(prove_check)

installcheck:
	$(prove_installcheck)

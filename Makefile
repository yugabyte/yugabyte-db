DATA_built = pgtap.sql drop_pgtap.sql
DOCS = README.pgtap
SCRIPTS = pg_prove

top_builddir = ../..
in_contrib = $(wildcard $(top_builddir)/src/Makefile.global);

ifdef $(in_contrib)
	# Just include the local makefiles
	subdir = contrib/pgtap
	include $(top_builddir)/src/Makefile.global
	include $(top_srcdir)/contrib/contrib-global.mk
else
	# Use pg_config to find PGXS and include it.
	PGXS := $(shell pg_config --pgxs)
	include $(PGXS)
endif

%.sql: %.sql.in
	sed 's,MODULE_PATHNAME,$$libdir/$*,g' $< >$@

test:
	./pg_prove *test.sql
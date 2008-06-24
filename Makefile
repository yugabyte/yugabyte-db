# $Id$
DATA_built = pgtap.sql drop_pgtap.sql
DOCS = README.pgtap
SCRIPTS = pg_prove
REGRESS = pgtap

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

# Override how .sql targets are processed to add the schema info, if
# necessary. Otherwise just copy the files.
%.sql: %.sql.in
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' $< >$@
else
	cp $< $@
endif

test:
	./pg_prove sql/$(REGRESS).sql

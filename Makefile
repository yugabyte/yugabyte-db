# $Id$
DATA_built = pgtap.sql drop_pgtap.sql
DOCS = README.pgtap
SCRIPTS = pg_prove
REGRESS = pgtap pg73

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/citext
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# Override how .sql targets are processed to add the schema info, if
# necessary. Otherwise just copy the files.
%.sql: %.sql.in
ifdef TAPSCHEMA
	sed -e 's,TAPSCHEMA,$(TAPSCHEMA),g' -e 's/^-- ## //g' $< >$@
else
	cp $< $@
endif

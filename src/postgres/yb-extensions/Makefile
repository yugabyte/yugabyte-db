# YB_TODO_PG19MERGE: yb_xcluster_ddl_replication temporarily dropped while
# bringing up the PG19 merge. PG19's jsonb refactor changes pushJsonbValue
# signature (now takes JsonbInState* instead of JsonbParseState**) across ~20
# call sites, plus queryjumble.h moved/renamed and SPI_push/SPI_pop were
# removed. Restore once the extension is ported.
DIRS = \
	pg_dist_rag \
	yb_pg_metrics \
	yb_test_extension \
	yb_ycql_utils \

BUILDDIRS = $(DIRS:%=build-%)
INSTALLDIRS = $(DIRS:%=install-%)
CLEANDIRS = $(DIRS:%=clean-%)

all: $(BUILDDIRS)
$(DIRS): $(BUILDDIRS)
$(BUILDDIRS):
	$(MAKE) -C $(@:build-%=%)

clean: $(CLEANDIRS)
$(CLEANDIRS):
	$(MAKE) -C $(@:clean-%=%) clean

install: $(INSTALLDIRS)
$(INSTALLDIRS):
	$(MAKE) -C $(@:install-%=%) install

.PHONY: subdirs $(DIRS)
.PHONY: subdirs $(BUILDDIRS)
.PHONY: subdirs $(INSTALLDIRS)
.PHONY: subdirs $(CLEANDIRS)
.PHONY: all install clean

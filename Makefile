# contrib/wal2json/Makefile

MODULE_big = wal2json
OBJS = wal2json.o

# Note: because we don't tell the Makefile there are any regression tests,
# we have to clean those result files explicitly
EXTRA_CLEAN = -r $(pg_regress_clean_files)

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/wal2json
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# Disabled because these tests require "wal_level=logical", which
# typical installcheck users do not have (e.g. buildfarm clients).
installcheck:;

# But it can nonetheless be very helpful to run tests on preexisting
# installation, allow to do so, but only if requested explicitly.
installcheck-force: regresscheck-install-force

check: regresscheck

submake-regress:
	$(MAKE) -C $(top_builddir)/src/test/regress all

submake-test_decoding:
	$(MAKE) -C $(top_builddir)/contrib/test_decoding

REGRESSCHECKS=insert1 update1 update2 update3 update4 delete1 delete2 delete3 delete4 \
			  savepoint specialvalue toast bytea

regresscheck: all | submake-regress submake-test_decoding
	$(pg_regress_check) \
		--temp-config $(top_srcdir)/contrib/test_decoding/logical.conf \
		--temp-install=./tmp_check \
		--extra-install=contrib/wal2json \
		--extra-install=contrib/test_decoding \
		$(REGRESSCHECKS)

regresscheck-install-force: | submake-regress submake-test_decoding
	$(pg_regress_installcheck) \
		--extra-install=contrib/wal2json \
		--extra-install=contrib/test_decoding \
		$(REGRESSCHECKS)

PHONY: check submake-regress submake-test_decoding \
	regresscheck regresscheck-install-force

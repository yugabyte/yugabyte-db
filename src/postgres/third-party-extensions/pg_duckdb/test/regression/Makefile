# Makefile for regression test

ROOT_DIR = ../..

include $(ROOT_DIR)/Makefile.global

# Make sure foreign_data_wrapper tests fail as expected
unexport MOTHERDUCK_TOKEN
unexport motherduck_token

check-regression-duckdb:
	TEST_DIR=$(CURDIR) $(pg_regress_installcheck) \
	--encoding=UTF8 \
	--no-locale \
	--temp-instance=./tmp_check \
	--temp-config regression.conf \
	--load-extension=pg_duckdb \
	$(if $(TEST),$(TEST),--schedule=schedule)

clean-regression:
	rm -fr $(CURDIR)/tmp_check
	rm -fr $(CURDIR)/log
	rm -fr $(CURDIR)/results
	rm -f  $(CURDIR)/regression.diffs
	rm -f  $(CURDIR)/regression.out

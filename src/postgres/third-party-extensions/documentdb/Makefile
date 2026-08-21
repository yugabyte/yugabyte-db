# Delegate all rules to sub directories.

Makefile:;

.DEFAULT_GOAL := .DEFAULT

.PHONY: %

# YB: disable build if requested.
ifneq ($(findstring YB_ENABLE_YSQL_DOCUMENTDB_EXT, $(CXXFLAGS)),)
check-no-distributed:
	$(MAKE) -C pg_documentdb_core check
	$(MAKE) -C pg_documentdb check

install-no-distributed: install-documentdb

install-documentdb:
	$(MAKE) -C pg_documentdb_core install
	$(MAKE) -C pg_documentdb install
	$(MAKE) -C pg_documentdb_extended_rum install
# YB: Install pg_documentdb_gw_host extension.
	$(MAKE) -C pg_documentdb_gw_host install

.DEFAULT:
	$(MAKE) -C pg_documentdb_core
	$(MAKE) -C pg_documentdb
	$(MAKE) -C pg_documentdb_extended_rum
	$(MAKE) -C internal/pg_documentdb_distributed

%:
	$(MAKE) -C pg_documentdb_core $@
	$(MAKE) -C pg_documentdb $@
	$(MAKE) -C pg_documentdb_extended_rum $@
	$(MAKE) -C internal/pg_documentdb_distributed $@
# YB: Install pg_documentdb_gw_host extension.
	$(MAKE) -C pg_documentdb_gw_host $@
else	# YB
check-no-distributed:;
.DEFAULT:;
%:;
endif	# YB

# Delegate all rules to sub directories.

Makefile:;

.DEFAULT_GOAL := .DEFAULT

.PHONY: %

# YB: disable build if requested.
ifneq ($(findstring YB_ENABLE_YSQL_DOCUMENTDB_EXT, $(CXXFLAGS)),)
check-no-distributed:
	$(MAKE) -C pg_documentdb_core check
	$(MAKE) -C pg_documentdb check

.DEFAULT:
	$(MAKE) -C pg_documentdb_core
	$(MAKE) -C pg_documentdb
	$(MAKE) -C internal/pg_documentdb_distributed

%:
	$(MAKE) -C pg_documentdb_core $@
	$(MAKE) -C pg_documentdb $@
	$(MAKE) -C internal/pg_documentdb_distributed $@
else	# YB
check-no-distributed:;
.DEFAULT:;
%:;
endif	# YB

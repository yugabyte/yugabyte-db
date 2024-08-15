# Delegate all rules to sub directories.

Makefile:;

.DEFAULT_GOAL := .DEFAULT

.PHONY: %

.DEFAULT:
	$(MAKE) -C pg_helio_core
	$(MAKE) -C pg_helio_api

%:
	$(MAKE) -C pg_helio_core $@
	$(MAKE) -C pg_helio_api $@
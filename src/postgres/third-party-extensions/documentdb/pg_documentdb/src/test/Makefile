subdir = src/test

check-regress:
	$(MAKE) -C regress all

check-extended-rum:
	$(MAKE) -C extended_rum_tests all

installcheck:
	$(MAKE) -C regress installcheck

all: check-regress check-extended-rum

##############################################################################
#
# Makefile
#
###############################################################################

DEST?=../_build

ARTIFACTS=$(DEST)/linux/amd64/pg_dump_anon/pg_dump_anon $(DEST)/windows/amd64/pg_dump_anon/pg_dump_anon.exe

all: $(ARTIFACTS)

$(DEST)/linux/amd64/pg_dump_anon/pg_dump_anon: main.go
	env GOOS=linux GOARCH=amd64 go build -o $@ $^

$(DEST)/windows/amd64/pg_dump_anon/pg_dump_anon.exe: main.go
	env GOOS=windows GOARCH=amd64 go build -o $@ $^

.PHONY: lint-go
lint-go:
	go vet main.go

.PHONY: test
test:
	./tests/bats-core/bin/bats tests/pg_dump_anon.bats

.PHONY: release
release:
	goreleaser --skip-validate --skip-publish --rm-dist

.PHONY : clean
clean :
	rm -fr dist
	rm -fr $(ARTIFACTS)

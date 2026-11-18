
TAG?=latest
IMG?=registry.gitlab.com/dalibo/postgresql_anonymizer:$(TAG)
ANON=docker run --rm -i $(IMG) /dump.sh

all: clean test

test: results/basic.sql results/data_only.sql

results/basic.sql:
	cat sql/dump.sql sql/rules.sql | $(ANON) > $@
	sed -i 's/^--.*//' $@
	diff expected/$(notdir $@) $@

results/data_only.sql:
	cat sql/dump.sql sql/rules.sql | $(ANON) --data-only > $@
	sed -i 's/^--.*//' $@
	diff expected/$(notdir $@) $@

clean:
	rm -f results/*

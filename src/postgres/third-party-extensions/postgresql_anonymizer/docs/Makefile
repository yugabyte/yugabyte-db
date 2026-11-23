
PANDOC_CONTAINER_ID?=runbooks-anon-pandoc-1
PANDOC=docker exec $(PANDOC_CONTAINER_ID) pandoc

PG_CONTAINER_ID?=runbooks-anon-pg-1

TUTORIALS?=$(sort $(wildcard tutorials/*.md))

up:
	docker compose --project-directory runbooks  up --detach

down:
	docker compose --project-directory runbooks  down

clean:
	docker compose --project-directory runbooks rm

psql:
	docker exec -it $(PG_CONTAINER_ID) psql

.PHONY: tutorials
tutorials: $(TUTORIALS)

tutorials/%.md: runbooks/%.md
	@$(PANDOC) $^ \
    --to markdown-grid_tables-simple_tables-multiline_tables \
    --filter=pandoc-run-postgres \
    --output=$@
	@grep -vqz '{class="warning"}' $@ || echo '⚠️  Warning(s) in $@'

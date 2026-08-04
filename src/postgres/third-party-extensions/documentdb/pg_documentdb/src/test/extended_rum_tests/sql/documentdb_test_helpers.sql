\i ../regress/sql/documentdb_test_helpers.sql

CREATE OR REPLACE FUNCTION documentdb_api_internal.rum_prune_empty_entries_on_index(index_relid regclass)
RETURNS void
LANGUAGE c
AS '$libdir/pg_documentdb_extended_rum_core', 'documentdb_rum_prune_empty_entries_on_index';

CREATE OR REPLACE FUNCTION documentdb_api_internal.documentdb_rum_page_get_stats(page bytea)
RETURNS jsonb
LANGUAGE c
AS '$libdir/pg_documentdb_extended_rum_core', 'documentdb_rum_page_get_stats';

CREATE OR REPLACE FUNCTION documentdb_api_internal.documentdb_rum_get_meta_page_info(page bytea)
RETURNS jsonb
LANGUAGE c
AS '$libdir/pg_documentdb_extended_rum_core', 'documentdb_rum_get_meta_page_info';

CREATE OR REPLACE FUNCTION documentdb_api_internal.documentdb_rum_page_get_entries(page bytea, indexRelId Oid)
RETURNS SETOF jsonb
LANGUAGE c
AS '$libdir/pg_documentdb_extended_rum_core', 'documentdb_rum_page_get_entries';
/* src/test/modules/yb_test_memory_errors/yb_test_memory_errors--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION yb_test_memory_errors" to load this file. \quit

CREATE FUNCTION yb_test_write_past_chunk_end(context_kind pg_catalog.text,
											 free_chunk pg_catalog.bool)
	RETURNS pg_catalog.void
	AS 'MODULE_PATHNAME' LANGUAGE C STRICT;

CREATE FUNCTION yb_test_heap_buffer_overflow()
	RETURNS pg_catalog.void
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE FUNCTION yb_test_leak_malloc()
	RETURNS pg_catalog.void
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE FUNCTION yb_test_json_parse_exact(pg_catalog.text)
	RETURNS pg_catalog.bool
	AS 'MODULE_PATHNAME' LANGUAGE C STRICT;

CREATE FUNCTION yb_test_use_after_pfree()
	RETURNS pg_catalog.int4
	AS 'MODULE_PATHNAME' LANGUAGE C;

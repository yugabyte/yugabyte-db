/* src/test/modules/yb_test_memory_errors/yb_test_memory_errors--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION yb_test_memory_errors" to load this file. \quit

CREATE FUNCTION yb_test_write_past_chunk_end()
	RETURNS pg_catalog.void
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE FUNCTION yb_test_heap_buffer_overflow()
	RETURNS pg_catalog.void
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE FUNCTION yb_test_leak_malloc()
	RETURNS pg_catalog.void
	AS 'MODULE_PATHNAME' LANGUAGE C;

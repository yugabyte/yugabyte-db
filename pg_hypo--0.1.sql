-- This program is open source, licensed under the PostgreSQL License.
-- For license terms, see the LICENSE file.
--
-- Copyright (C) 2015: Julien ROuhaud

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_hypo" to load this file. \quit

SET client_encoding = 'UTF8';

CREATE FUNCTION pg_hypo_reset()
    RETURNS void
    LANGUAGE c COST 1000
    AS '$libdir/pg_hypo', 'pg_hypo_reset';

CREATE FUNCTION pg_hypo_add_index_internal(IN indexid oid, relid oid, IN indexname text)
    RETURNS bool
    LANGUAGE c COST 1000
    AS '$libdir/pg_hypo', 'pg_hypo_add_index_internal';

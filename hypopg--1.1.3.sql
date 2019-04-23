-- This program is open source, licensed under the PostgreSQL License.
-- For license terms, see the LICENSE file.
--
-- Copyright (C) 2015-2018: Julien Rouhaud

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION hypopg" to load this file. \quit

SET LOCAL client_encoding = 'UTF8';

CREATE FUNCTION hypopg_reset_index()
    RETURNS void
    LANGUAGE C VOLATILE COST 100
AS '$libdir/hypopg', 'hypopg_reset_index';

CREATE FUNCTION hypopg_reset()
    RETURNS void
    LANGUAGE C VOLATILE COST 100
AS '$libdir/hypopg', 'hypopg_reset';

CREATE FUNCTION
hypopg_create_index(IN sql_order text, OUT indexrelid oid, OUT indexname text)
    RETURNS SETOF record
    LANGUAGE C STRICT VOLATILE COST 100
AS '$libdir/hypopg', 'hypopg_create_index';

CREATE FUNCTION
hypopg_drop_index(IN indexid oid)
    RETURNS bool
    LANGUAGE C STRICT VOLATILE COST 100
AS '$libdir/hypopg', 'hypopg_drop_index';

CREATE FUNCTION hypopg(OUT indexname text, OUT indexrelid oid,
                       OUT indrelid oid, OUT innatts integer,
                       OUT indisunique boolean, OUT indkey int2vector,
                       OUT indcollation oidvector, OUT indclass oidvector,
                       OUT indoption oidvector, OUT indexprs pg_node_tree,
                       OUT indpred pg_node_tree, OUT amid oid)
    RETURNS SETOF record
    LANGUAGE c COST 100
AS '$libdir/hypopg', 'hypopg';

CREATE FUNCTION hypopg_list_indexes(OUT indexrelid oid, OUT indexname text, OUT nspname name, OUT relname name, OUT amname name)
    RETURNS SETOF record
AS
$_$
    SELECT h.indexrelid, h.indexname, n.nspname, c.relname, am.amname
    FROM hypopg() h
    JOIN pg_class c ON c.oid = h.indrelid
    JOIN pg_namespace n ON n.oid = c.relnamespace
    JOIN pg_am am ON am.oid = h.amid
$_$
LANGUAGE sql;

CREATE FUNCTION
hypopg_relation_size(IN indexid oid)
    RETURNS bigint
LANGUAGE C STRICT VOLATILE COST 100
AS '$libdir/hypopg', 'hypopg_relation_size';

CREATE FUNCTION
hypopg_get_indexdef(IN indexid oid)
    RETURNS text
LANGUAGE C STRICT VOLATILE COST 100
AS '$libdir/hypopg', 'hypopg_get_indexdef';

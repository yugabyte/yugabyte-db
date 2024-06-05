-- This program is open source, licensed under the PostgreSQL License.
-- For license terms, see the LICENSE file.
--
-- Copyright (C) 2015-2024: Julien Rouhaud

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

CREATE VIEW hypopg_list_indexes
AS
    SELECT h.indexrelid, h.indexname AS index_name, n.nspname AS schema_name,
    coalesce(c.relname, '<dropped>') AS table_name, am.amname AS am_name
    FROM hypopg() h
    LEFT JOIN pg_catalog.pg_class c ON c.oid = h.indrelid
    LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
    LEFT JOIN pg_catalog.pg_am am ON am.oid = h.amid;

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

CREATE FUNCTION
hypopg_hide_index(IN indexid oid)
    RETURNS bool
    LANGUAGE C STRICT VOLATILE COST 100
AS '$libdir/hypopg', 'hypopg_hide_index';

CREATE FUNCTION
hypopg_unhide_index(IN indexid oid)
    RETURNS bool
    LANGUAGE C STRICT VOLATILE COST 100
AS '$libdir/hypopg', 'hypopg_unhide_index';

CREATE FUNCTION
hypopg_unhide_all_indexes()
    RETURNS void
    LANGUAGE C VOLATILE COST 100
AS '$libdir/hypopg', 'hypopg_unhide_all_indexes';

CREATE FUNCTION hypopg_hidden_indexes()
    RETURNS TABLE (indexid oid)
    LANGUAGE C STRICT VOLATILE
AS '$libdir/hypopg', 'hypopg_hidden_indexes';

CREATE VIEW hypopg_hidden_indexes
AS
    SELECT h.indexid AS indexrelid,
           i.relname AS index_name,
           n.nspname AS schema_name,
           t.relname AS table_name,
           m.amname  AS am_name,
           false     AS is_hypo
    FROM hypopg_hidden_indexes() h
        JOIN pg_index x ON x.indexrelid = h.indexid
        JOIN pg_class i ON i.oid = h.indexid
        JOIN pg_namespace n ON n.oid = i.relnamespace
        JOIN pg_class t ON t.oid = x.indrelid
        JOIN pg_am m ON m.oid = i.relam
    UNION ALL
    SELECT hl.*, true AS is_hypo
    FROM hypopg_hidden_indexes() hi
        JOIN hypopg_list_indexes hl on hl.indexrelid = hi.indexid
    ORDER BY index_name;

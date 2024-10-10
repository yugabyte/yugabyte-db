-- This program is open source, licensed under the PostgreSQL License.
-- For license terms, see the LICENSE file.
--
-- Copyright (C) 2015-2024: Julien Rouhaud

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION hypopg" to load this file. \quit

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

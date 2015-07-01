-- This program is open source, licensed under the PostgreSQL License.
-- For license terms, see the LICENSE file.
--
-- Copyright (C) 2015: Julien Rouhaud

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION hypopg" to load this file. \quit

SET client_encoding = 'UTF8';

CREATE FUNCTION hypopg_reset()
    RETURNS void
    LANGUAGE c COST 100
AS '$libdir/hypopg', 'hypopg_reset';

CREATE FUNCTION
hypopg_add_index_internal(IN relid oid,
                           IN indexname text,
                           IN accessmethod text,
                           IN ncolumns int,
                           IN indexkeys smallint,
                           IN indexcollations Oid,
                           IN opfamily Oid,
                           IN opcintype Oid)
    RETURNS bool
    LANGUAGE c COST 100
AS '$libdir/hypopg', 'hypopg_add_index_internal';

CREATE FUNCTION
hypopg_create_index(IN sql_order text)
    RETURNS bool
    LANGUAGE c COST 100
AS '$libdir/hypopg', 'hypopg_create_index';

CREATE FUNCTION
hypopg_drop_index(IN indexid oid)
    RETURNS bool
    LANGUAGE c COST 100
AS '$libdir/hypopg', 'hypopg_drop_index';

CREATE FUNCTION hypopg(OUT indexname text, OUT indexrelid Oid,
                       OUT indrelid Oid, OUT innatts integer,
                       OUT indisunique boolean, OUT indkey int2vector,
                       OUT indcollation oidvector, OUT indclass oidvector,
                       OUT indoption oidvector, OUT indexprs pg_node_tree,
                       OUT indpred pg_node_tree, OUT amid Oid)
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
hypopg_add_index(IN _nspname name, IN _relname name, IN _attname name, IN _amname text)
    RETURNS bool
AS
$_$
    SELECT hypopg_add_index_internal(
        relid,
        indexname,
        _amname,
        ncolumns,
        attnum,
        indexcollations,
        opfoid,
        atttypid)
    FROM (
        SELECT DISTINCT c.oid AS relid, _amname || '_' || CASE WHEN nspname = 'public' THEN '' ELSE nspname || '_' END || relname || '_' || attname AS indexname, 1 AS ncolumns, a.attnum, 0 AS indexcollations, opf.oid AS opfoid, a.atttypid
        FROM pg_class c
        JOIN pg_namespace n on n.oid = c.relnamespace
        JOIN pg_attribute a ON a.attrelid = c.oid AND a.attnum > 0
        JOIN pg_type t ON t.oid = a.atttypid
        JOIN pg_amop amop ON amop.amoplefttype = t.oid
        JOIN pg_opfamily opf ON opf.oid = amop.amopfamily
        JOIN pg_am am ON am.oid = amop.amopmethod
        WHERE
            n.nspname = _nspname
            AND c.relname = _relname
            AND a.attname = _attname
            AND am.amname = _amname
    ) src;
$_$
LANGUAGE sql;

CREATE FUNCTION
hypopg_relation_size(IN indexid Oid)
    RETURNS bigint
LANGUAGE c COST 100
AS '$libdir/hypopg', 'hypopg_relation_size';

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

CREATE FUNCTION
pg_hypo_add_index_internal(IN indexid oid,
                           IN relid oid,
                           IN indexname text,
                           IN relam Oid,
                           IN ncolumns int,
                           IN indexkeys int,
                           IN indexcollations Oid,
                           IN opfamily Oid,
                           IN opcintype Oid)
    RETURNS bool
    LANGUAGE c COST 1000
AS '$libdir/pg_hypo', 'pg_hypo_add_index_internal';

CREATE FUNCTION
pg_hypo_add_index(IN _nspname name, IN _relname name, IN _attname name, IN _indtype text)
    RETURNS bool
AS
$_$
    SELECT pg_hypo_add_index_internal(
        id,
        relid,
        indexname,
        amoid,
        ncolumns,
        attnum,
        indexcollations,
        opfoid,
        atttypid)
    FROM (
        SELECT DISTINCT 1 AS id, c.oid AS relid, 'index_name' AS indexname, am.oid AS amoid, 1 AS ncolumns, a.attnum, 0 AS indexcollations, opf.oid AS opfoid, a.atttypid
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
            AND am.amname = _indtype
    ) src;
$_$
LANGUAGE sql;

--
-- TYPE_SANITY
-- Sanity checks for common errors in making type-related system tables:
-- pg_type, pg_class, pg_attribute, pg_range.
--
-- None of the SELECTs here should ever find any matching entries,
-- so the expected output is easy to maintain ;-).
-- A test failure indicates someone messed up an entry in the system tables.
--
-- NB: we assume the oidjoins test will have caught any dangling links,
-- that is OID or REGPROC fields that are not zero and do not match some
-- row in the linked-to table.  However, if we want to enforce that a link
-- field can't be 0, we have to check it here.

-- **************** pg_type ****************

-- Look for illegal values in pg_type fields.

SELECT t1.oid, t1.typname
FROM pg_type as t1
WHERE t1.typnamespace = 0 OR
    (t1.typlen <= 0 AND t1.typlen != -1 AND t1.typlen != -2) OR
    (t1.typtype not in ('b', 'c', 'd', 'e', 'm', 'p', 'r')) OR
    NOT t1.typisdefined OR
    (t1.typalign not in ('c', 's', 'i', 'd')) OR
    (t1.typstorage not in ('p', 'x', 'e', 'm'));

-- Look for "pass by value" types that can't be passed by value.

SELECT t1.oid, t1.typname
FROM pg_type as t1
WHERE t1.typbyval AND
    (t1.typlen != 1 OR t1.typalign != 'c') AND
    (t1.typlen != 2 OR t1.typalign != 's') AND
    (t1.typlen != 4 OR t1.typalign != 'i') AND
    (t1.typlen != 8 OR t1.typalign != 'd');

-- Look for "toastable" types that aren't varlena.

SELECT t1.oid, t1.typname
FROM pg_type as t1
WHERE t1.typstorage != 'p' AND
    (t1.typbyval OR t1.typlen != -1);

-- Look for complex types that do not have a typrelid entry,
-- or basic types that do.

SELECT t1.oid, t1.typname
FROM pg_type as t1
WHERE (t1.typtype = 'c' AND t1.typrelid = 0) OR
    (t1.typtype != 'c' AND t1.typrelid != 0);

-- Look for types that should have an array type but don't.
-- Generally anything that's not a pseudotype should have an array type.
-- However, we do have a small number of exceptions.

SELECT t1.oid, t1.typname
FROM pg_type as t1
WHERE t1.typtype not in ('p') AND t1.typname NOT LIKE E'\\_%'
    AND NOT EXISTS
    (SELECT 1 FROM pg_type as t2
     WHERE t2.typname = ('_' || t1.typname)::name AND
           t2.typelem = t1.oid and t1.typarray = t2.oid)
ORDER BY t1.oid;

-- Make sure typarray points to a "true" array type of our own base
SELECT t1.oid, t1.typname as basetype, t2.typname as arraytype,
       t2.typsubscript
FROM   pg_type t1 LEFT JOIN pg_type t2 ON (t1.typarray = t2.oid)
WHERE  t1.typarray <> 0 AND
       (t2.oid IS NULL OR
        t2.typsubscript <> 'array_subscript_handler'::regproc);

-- Look for range types that do not have a pg_range entry
SELECT t1.oid, t1.typname
FROM pg_type as t1
WHERE t1.typtype = 'r' AND
   NOT EXISTS(SELECT 1 FROM pg_range r WHERE rngtypid = t1.oid);

-- Look for range types whose typalign isn't sufficient
SELECT t1.oid, t1.typname, t1.typalign, t2.typname, t2.typalign
FROM pg_type as t1
     LEFT JOIN pg_range as r ON rngtypid = t1.oid
     LEFT JOIN pg_type as t2 ON rngsubtype = t2.oid
WHERE t1.typtype = 'r' AND
    (t1.typalign != (CASE WHEN t2.typalign = 'd' THEN 'd'::"char"
                          ELSE 'i'::"char" END)
     OR t2.oid IS NULL);

-- Text conversion routines must be provided.

SELECT t1.oid, t1.typname
FROM pg_type as t1
WHERE (t1.typinput = 0 OR t1.typoutput = 0);

-- Check for bogus typinput routines

SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typinput = p1.oid AND NOT
    ((p1.pronargs = 1 AND p1.proargtypes[0] = 'cstring'::regtype) OR
     (p1.pronargs = 2 AND p1.proargtypes[0] = 'cstring'::regtype AND
      p1.proargtypes[1] = 'oid'::regtype) OR
     (p1.pronargs = 3 AND p1.proargtypes[0] = 'cstring'::regtype AND
      p1.proargtypes[1] = 'oid'::regtype AND
      p1.proargtypes[2] = 'int4'::regtype));

-- Check for type of the variadic array parameter's elements.
-- provariadic should be ANYOID if the type of the last element is ANYOID,
-- ANYELEMENTOID if the type of the last element is ANYARRAYOID,
-- ANYCOMPATIBLEOID if the type of the last element is ANYCOMPATIBLEARRAYOID,
-- and otherwise the element type corresponding to the array type.

SELECT oid::regprocedure, provariadic::regtype, proargtypes::regtype[]
FROM pg_proc
WHERE provariadic != 0
AND case proargtypes[array_length(proargtypes, 1)-1]
	WHEN '"any"'::regtype THEN '"any"'::regtype
	WHEN 'anyarray'::regtype THEN 'anyelement'::regtype
	WHEN 'anycompatiblearray'::regtype THEN 'anycompatible'::regtype
	ELSE (SELECT t.oid
		  FROM pg_type t
		  WHERE t.typarray = proargtypes[array_length(proargtypes, 1)-1])
	END  != provariadic;

-- Check that all and only those functions with a variadic type have
-- a variadic argument.
SELECT oid::regprocedure, proargmodes, provariadic
FROM pg_proc
WHERE (proargmodes IS NOT NULL AND 'v' = any(proargmodes))
    IS DISTINCT FROM
    (provariadic != 0);

-- As of 8.0, this check finds refcursor, which is borrowing
-- other types' I/O routines
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typinput = p1.oid AND t1.typtype in ('b', 'p') AND NOT
    (t1.typelem != 0 AND t1.typlen < 0) AND NOT
    (p1.prorettype = t1.oid AND NOT p1.proretset)
ORDER BY 1;

-- Varlena array types will point to array_in
-- Exception as of 8.1: int2vector and oidvector have their own I/O routines
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typinput = p1.oid AND
    (t1.typelem != 0 AND t1.typlen < 0) AND NOT
    (p1.oid = 'array_in'::regproc)
ORDER BY 1;

-- typinput routines should not be volatile
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typinput = p1.oid AND p1.provolatile NOT IN ('i', 's');

-- Composites, domains, enums, multiranges, ranges should all use the same input routines
SELECT DISTINCT typtype, typinput
FROM pg_type AS t1
WHERE t1.typtype not in ('b', 'p')
ORDER BY 1;

-- Check for bogus typoutput routines

-- As of 8.0, this check finds refcursor, which is borrowing
-- other types' I/O routines
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typoutput = p1.oid AND t1.typtype in ('b', 'p') AND NOT
    (p1.pronargs = 1 AND
     (p1.proargtypes[0] = t1.oid OR
      (p1.oid = 'array_out'::regproc AND
       t1.typelem != 0 AND t1.typlen = -1)))
ORDER BY 1;

SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typoutput = p1.oid AND NOT
    (p1.prorettype = 'cstring'::regtype AND NOT p1.proretset);

-- typoutput routines should not be volatile
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typoutput = p1.oid AND p1.provolatile NOT IN ('i', 's');

-- Composites, enums, multiranges, ranges should all use the same output routines
SELECT DISTINCT typtype, typoutput
FROM pg_type AS t1
WHERE t1.typtype not in ('b', 'd', 'p')
ORDER BY 1;

-- Domains should have same typoutput as their base types
SELECT t1.oid, t1.typname, t2.oid, t2.typname
FROM pg_type AS t1 LEFT JOIN pg_type AS t2 ON t1.typbasetype = t2.oid
WHERE t1.typtype = 'd' AND t1.typoutput IS DISTINCT FROM t2.typoutput;

-- Check for bogus typreceive routines

SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typreceive = p1.oid AND NOT
    ((p1.pronargs = 1 AND p1.proargtypes[0] = 'internal'::regtype) OR
     (p1.pronargs = 2 AND p1.proargtypes[0] = 'internal'::regtype AND
      p1.proargtypes[1] = 'oid'::regtype) OR
     (p1.pronargs = 3 AND p1.proargtypes[0] = 'internal'::regtype AND
      p1.proargtypes[1] = 'oid'::regtype AND
      p1.proargtypes[2] = 'int4'::regtype));

-- As of 7.4, this check finds refcursor, which is borrowing
-- other types' I/O routines
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typreceive = p1.oid AND t1.typtype in ('b', 'p') AND NOT
    (t1.typelem != 0 AND t1.typlen < 0) AND NOT
    (p1.prorettype = t1.oid AND NOT p1.proretset)
ORDER BY 1;

-- Varlena array types will point to array_recv
-- Exception as of 8.1: int2vector and oidvector have their own I/O routines
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typreceive = p1.oid AND
    (t1.typelem != 0 AND t1.typlen < 0) AND NOT
    (p1.oid = 'array_recv'::regproc)
ORDER BY 1;

-- Suspicious if typreceive doesn't take same number of args as typinput
SELECT t1.oid, t1.typname, p1.oid, p1.proname, p2.oid, p2.proname
FROM pg_type AS t1, pg_proc AS p1, pg_proc AS p2
WHERE t1.typinput = p1.oid AND t1.typreceive = p2.oid AND
    p1.pronargs != p2.pronargs;

-- typreceive routines should not be volatile
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typreceive = p1.oid AND p1.provolatile NOT IN ('i', 's');

-- Composites, domains, enums, multiranges, ranges should all use the same receive routines
SELECT DISTINCT typtype, typreceive
FROM pg_type AS t1
WHERE t1.typtype not in ('b', 'p')
ORDER BY 1;

-- Check for bogus typsend routines

-- As of 7.4, this check finds refcursor, which is borrowing
-- other types' I/O routines
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typsend = p1.oid AND t1.typtype in ('b', 'p') AND NOT
    (p1.pronargs = 1 AND
     (p1.proargtypes[0] = t1.oid OR
      (p1.oid = 'array_send'::regproc AND
       t1.typelem != 0 AND t1.typlen = -1)))
ORDER BY 1;

SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typsend = p1.oid AND NOT
    (p1.prorettype = 'bytea'::regtype AND NOT p1.proretset);

-- typsend routines should not be volatile
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typsend = p1.oid AND p1.provolatile NOT IN ('i', 's');

-- Composites, enums, multiranges, ranges should all use the same send routines
SELECT DISTINCT typtype, typsend
FROM pg_type AS t1
WHERE t1.typtype not in ('b', 'd', 'p')
ORDER BY 1;

-- Domains should have same typsend as their base types
SELECT t1.oid, t1.typname, t2.oid, t2.typname
FROM pg_type AS t1 LEFT JOIN pg_type AS t2 ON t1.typbasetype = t2.oid
WHERE t1.typtype = 'd' AND t1.typsend IS DISTINCT FROM t2.typsend;

-- Check for bogus typmodin routines

SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typmodin = p1.oid AND NOT
    (p1.pronargs = 1 AND
     p1.proargtypes[0] = 'cstring[]'::regtype AND
     p1.prorettype = 'int4'::regtype AND NOT p1.proretset);

-- typmodin routines should not be volatile
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typmodin = p1.oid AND p1.provolatile NOT IN ('i', 's');

-- Check for bogus typmodout routines

SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typmodout = p1.oid AND NOT
    (p1.pronargs = 1 AND
     p1.proargtypes[0] = 'int4'::regtype AND
     p1.prorettype = 'cstring'::regtype AND NOT p1.proretset);

-- typmodout routines should not be volatile
SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typmodout = p1.oid AND p1.provolatile NOT IN ('i', 's');

-- Array types should have same typmodin/out as their element types

SELECT t1.oid, t1.typname, t2.oid, t2.typname
FROM pg_type AS t1, pg_type AS t2
WHERE t1.typelem = t2.oid AND NOT
    (t1.typmodin = t2.typmodin AND t1.typmodout = t2.typmodout);

-- Array types should have same typdelim as their element types

SELECT t1.oid, t1.typname, t2.oid, t2.typname
FROM pg_type AS t1, pg_type AS t2
WHERE t1.typarray = t2.oid AND NOT (t1.typdelim = t2.typdelim);

-- Look for array types whose typalign isn't sufficient

SELECT t1.oid, t1.typname, t1.typalign, t2.typname, t2.typalign
FROM pg_type AS t1, pg_type AS t2
WHERE t1.typarray = t2.oid AND
    t2.typalign != (CASE WHEN t1.typalign = 'd' THEN 'd'::"char"
                         ELSE 'i'::"char" END);

-- Check for typelem set without a handler

SELECT t1.oid, t1.typname, t1.typelem
FROM pg_type AS t1
WHERE t1.typelem != 0 AND t1.typsubscript = 0;

-- Check for misuse of standard subscript handlers

SELECT t1.oid, t1.typname,
       t1.typelem, t1.typlen, t1.typbyval
FROM pg_type AS t1
WHERE t1.typsubscript = 'array_subscript_handler'::regproc AND NOT
    (t1.typelem != 0 AND t1.typlen = -1 AND NOT t1.typbyval);

SELECT t1.oid, t1.typname,
       t1.typelem, t1.typlen, t1.typbyval
FROM pg_type AS t1
WHERE t1.typsubscript = 'raw_array_subscript_handler'::regproc AND NOT
    (t1.typelem != 0 AND t1.typlen > 0 AND NOT t1.typbyval);

-- Check for bogus typanalyze routines

SELECT t1.oid, t1.typname, p1.oid, p1.proname
FROM pg_type AS t1, pg_proc AS p1
WHERE t1.typanalyze = p1.oid AND NOT
    (p1.pronargs = 1 AND
     p1.proargtypes[0] = 'internal'::regtype AND
     p1.prorettype = 'bool'::regtype AND NOT p1.proretset);

-- there does not seem to be a reason to care about volatility of typanalyze

-- domains inherit their base type's typanalyze

SELECT d.oid, d.typname, d.typanalyze, t.oid, t.typname, t.typanalyze
FROM pg_type d JOIN pg_type t ON d.typbasetype = t.oid
WHERE d.typanalyze != t.typanalyze;

-- range_typanalyze should be used for all and only range types
-- (but exclude domains, which we checked above)

SELECT t.oid, t.typname, t.typanalyze
FROM pg_type t LEFT JOIN pg_range r on t.oid = r.rngtypid
WHERE t.typbasetype = 0 AND
    (t.typanalyze = 'range_typanalyze'::regproc) != (r.rngtypid IS NOT NULL);

-- array_typanalyze should be used for all and only array types
-- (but exclude domains, which we checked above)
-- As of 9.2 this finds int2vector and oidvector, which are weird anyway

SELECT t.oid, t.typname, t.typanalyze
FROM pg_type t
WHERE t.typbasetype = 0 AND
    (t.typanalyze = 'array_typanalyze'::regproc) !=
    (t.typsubscript = 'array_subscript_handler'::regproc)
ORDER BY 1;

-- **************** pg_class ****************

-- Look for illegal values in pg_class fields

SELECT c1.oid, c1.relname
FROM pg_class as c1
WHERE relkind NOT IN ('r', 'i', 'S', 't', 'v', 'm', 'c', 'f', 'p') OR
    relpersistence NOT IN ('p', 'u', 't') OR
    relreplident NOT IN ('d', 'n', 'f', 'i');

-- All tables and indexes should have an access method.
SELECT c1.oid, c1.relname
FROM pg_class as c1
WHERE c1.relkind NOT IN ('S', 'v', 'f', 'c') and
    c1.relam = 0;

-- Conversely, sequences, views, types shouldn't have them
SELECT c1.oid, c1.relname
FROM pg_class as c1
WHERE c1.relkind IN ('S', 'v', 'f', 'c') and
    c1.relam != 0;

-- Indexes should have AMs of type 'i'
SELECT pc.oid, pc.relname, pa.amname, pa.amtype
FROM pg_class as pc JOIN pg_am AS pa ON (pc.relam = pa.oid)
WHERE pc.relkind IN ('i') and
    pa.amtype != 'i';

-- Tables, matviews etc should have AMs of type 't'
SELECT pc.oid, pc.relname, pa.amname, pa.amtype
FROM pg_class as pc JOIN pg_am AS pa ON (pc.relam = pa.oid)
WHERE pc.relkind IN ('r', 't', 'm') and
    pa.amtype != 't';

-- **************** pg_attribute ****************

-- Look for illegal values in pg_attribute fields

SELECT a1.attrelid, a1.attname
FROM pg_attribute as a1
WHERE a1.attrelid = 0 OR a1.atttypid = 0 OR a1.attnum = 0 OR
    a1.attcacheoff != -1 OR a1.attinhcount < 0 OR
    (a1.attinhcount = 0 AND NOT a1.attislocal);

-- Cross-check attnum against parent relation

SELECT a1.attrelid, a1.attname, c1.oid, c1.relname
FROM pg_attribute AS a1, pg_class AS c1
WHERE a1.attrelid = c1.oid AND a1.attnum > c1.relnatts;

-- Detect missing pg_attribute entries: should have as many non-system
-- attributes as parent relation expects

SELECT c1.oid, c1.relname
FROM pg_class AS c1
WHERE c1.relnatts != (SELECT count(*) FROM pg_attribute AS a1
                      WHERE a1.attrelid = c1.oid AND a1.attnum > 0);

-- Cross-check against pg_type entry
-- NOTE: we allow attstorage to be 'plain' even when typstorage is not;
-- this is mainly for toast tables.

SELECT a1.attrelid, a1.attname, t1.oid, t1.typname
FROM pg_attribute AS a1, pg_type AS t1
WHERE a1.atttypid = t1.oid AND
    (a1.attlen != t1.typlen OR
     a1.attalign != t1.typalign OR
     a1.attbyval != t1.typbyval OR
     (a1.attstorage != t1.typstorage AND a1.attstorage != 'p'));

-- **************** pg_range ****************

-- Look for illegal values in pg_range fields.

SELECT r.rngtypid, r.rngsubtype
FROM pg_range as r
WHERE r.rngtypid = 0 OR r.rngsubtype = 0 OR r.rngsubopc = 0;

-- rngcollation should be specified iff subtype is collatable

SELECT r.rngtypid, r.rngsubtype, r.rngcollation, t.typcollation
FROM pg_range r JOIN pg_type t ON t.oid = r.rngsubtype
WHERE (rngcollation = 0) != (typcollation = 0);

-- opclass had better be a btree opclass accepting the subtype.
-- We must allow anyarray matches, cf IsBinaryCoercible()

SELECT r.rngtypid, r.rngsubtype, o.opcmethod, o.opcname
FROM pg_range r JOIN pg_opclass o ON o.oid = r.rngsubopc
WHERE o.opcmethod != 403 OR
    ((o.opcintype != r.rngsubtype) AND NOT
     (o.opcintype = 'pg_catalog.anyarray'::regtype AND
      EXISTS(select 1 from pg_catalog.pg_type where
             oid = r.rngsubtype and typelem != 0 and
             typsubscript = 'array_subscript_handler'::regproc)));

-- canonical function, if any, had better match the range type

SELECT r.rngtypid, r.rngsubtype, p.proname
FROM pg_range r JOIN pg_proc p ON p.oid = r.rngcanonical
WHERE pronargs != 1 OR proargtypes[0] != rngtypid OR prorettype != rngtypid;

-- subdiff function, if any, had better match the subtype

SELECT r.rngtypid, r.rngsubtype, p.proname
FROM pg_range r JOIN pg_proc p ON p.oid = r.rngsubdiff
WHERE pronargs != 2
    OR proargtypes[0] != rngsubtype OR proargtypes[1] != rngsubtype
    OR prorettype != 'pg_catalog.float8'::regtype;

-- every range should have a valid multirange

SELECT r.rngtypid, r.rngsubtype, r.rngmultitypid
FROM pg_range r
WHERE r.rngmultitypid IS NULL OR r.rngmultitypid = 0;

-- Create a table that holds all the known in-core data types and leave it
-- around so as pg_upgrade is able to test their binary compatibility.
CREATE TABLE tab_core_types AS SELECT
  '(11,12)'::point,
  '(1,1),(2,2)'::line,
  '((11,11),(12,12))'::lseg,
  '((11,11),(13,13))'::box,
  '((11,12),(13,13),(14,14))'::path AS openedpath,
  '[(11,12),(13,13),(14,14)]'::path AS closedpath,
  '((11,12),(13,13),(14,14))'::polygon,
  '1,1,1'::circle,
  'today'::date,
  'now'::time,
  'now'::timestamp,
  'now'::timetz,
  'now'::timestamptz,
  '12 seconds'::interval,
  '{"reason":"because"}'::json,
  '{"when":"now"}'::jsonb,
  '$.a[*] ? (@ > 2)'::jsonpath,
  '127.0.0.1'::inet,
  '127.0.0.0/8'::cidr,
  '00:01:03:86:1c:ba'::macaddr8,
  '00:01:03:86:1c:ba'::macaddr,
  2::int2, 4::int4, 8::int8,
  4::float4, '8'::float8, pi()::numeric,
  'foo'::"char",
  'c'::bpchar,
  'abc'::varchar,
  'name'::name,
  'txt'::text,
  true::bool,
  E'\\xDEADBEEF'::bytea,
  B'10001'::bit,
  B'10001'::varbit AS varbit,
  '12.34'::money,
  'abc'::refcursor,
  '1 2'::int2vector,
  '1 2'::oidvector,
  format('%I=UC/%I', USER, USER)::aclitem AS aclitem,
  'a fat cat sat on a mat and ate a fat rat'::tsvector,
  'fat & rat'::tsquery,
  'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'::uuid,
  '11'::xid8,
  'pg_class'::regclass,
  'regtype'::regtype type,
  'pg_monitor'::regrole,
  'pg_class'::regclass::oid,
  '(1,1)'::tid, '2'::xid, '3'::cid,
  '10:20:10,14,15'::txid_snapshot,
  '10:20:10,14,15'::pg_snapshot,
  '16/B374D848'::pg_lsn,
  1::information_schema.cardinal_number,
  'l'::information_schema.character_data,
  'n'::information_schema.sql_identifier,
  'now'::information_schema.time_stamp,
  'YES'::information_schema.yes_or_no,
  '(1,2)'::int4range, '{(1,2)}'::int4multirange,
  '(3,4)'::int8range, '{(3,4)}'::int8multirange,
  '(3,4)'::numrange, '{(3,4)}'::nummultirange,
  '(2020-01-02, 2021-02-03)'::daterange,
  '{(2020-01-02, 2021-02-03)}'::datemultirange,
  '(2020-01-02 03:04:05, 2021-02-03 06:07:08)'::tsrange,
  '{(2020-01-02 03:04:05, 2021-02-03 06:07:08)}'::tsmultirange,
  '(2020-01-02 03:04:05, 2021-02-03 06:07:08)'::tstzrange,
  '{(2020-01-02 03:04:05, 2021-02-03 06:07:08)}'::tstzmultirange;

-- Sanity check on the previous table, checking that all core types are
-- included in this table.
SELECT oid, typname, typtype, typelem, typarray
  FROM pg_type t
  WHERE oid < 16384 AND
    -- Exclude pseudotypes and composite types.
    typtype NOT IN ('p', 'c') AND
    -- These reg* types cannot be pg_upgraded, so discard them.
    oid != ALL(ARRAY['regproc', 'regprocedure', 'regoper',
                     'regoperator', 'regconfig', 'regdictionary',
                     'regnamespace', 'regcollation']::regtype[]) AND
    -- Discard types that do not accept input values as these cannot be
    -- tested easily.
    -- Note: XML might be disabled at compile-time.
    oid != ALL(ARRAY['gtsvector', 'pg_node_tree',
                     'pg_ndistinct', 'pg_dependencies', 'pg_mcv_list',
                     'pg_brin_bloom_summary',
                     'pg_brin_minmax_multi_summary', 'xml']::regtype[]) AND
    -- Discard arrays.
    NOT EXISTS (SELECT 1 FROM pg_type u WHERE u.typarray = t.oid)
    -- Exclude everything from the table created above.  This checks
    -- that no in-core types are missing in tab_core_types.
    AND NOT EXISTS (SELECT 1
                    FROM pg_attribute a
                    WHERE a.atttypid=t.oid AND
                          a.attnum > 0 AND
                          a.attrelid='tab_core_types'::regclass);

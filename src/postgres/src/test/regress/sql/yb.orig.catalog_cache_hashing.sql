/*
 * This test checks if the hash function used for the catalog cache has changed.
 * It runs the hash function on a set of up to 100 keys for each catalog cache--
 * the expected output has the hash values from when this test was created.
 *
 * Because some system tables have a large number of rows, this test limits the
 * number of rows checked to 100 for each catalog cache.
 *
 * To prevent this test from failing when a system object is added, we store a list
 * of all the keys we expect to test for each catalog cache in yb_catcache_keys.data.
 * The test loads this file and hashes these keys, comparing the output with the expected
 * output from when this test was created. This ensures that any changes to the system
 * catalog will not affect this test.
 */

-- Table for the keys we will hash in the test.
CREATE TEMP TABLE catcache_keys (
  catcache_name text,
  keys          jsonb
);

-- Load the keys from the data file.
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/data/yb_catcache_keys.data'
COPY catcache_keys FROM :'filename';

-- This table will store the results of hashing the keys.
DROP TABLE IF EXISTS catcache_hash_results;
CREATE TEMP TABLE catcache_hash_results (
  catcache_name text,
  keys          jsonb,
  hashval       bigint
);

DO $$
DECLARE
  info      record;
  attrec    record;
  attnums   int[]  := ARRAY[0, 0, 0, 0];
  cache_key text[] := ARRAY['NULL','NULL','NULL','NULL'];
  fkey      text;
  casttxt   text;
  i         int;
  fkeys     record;
BEGIN
  -- Outer loop: iterate over the catalog caches.
  FOR info IN
    SELECT
      c.cache_id,
      c.name AS catcache_name,
      c.reloid,
      c.nkeys,
      c.key1,
      c.key2,
      c.key3,
      c.key4
    FROM yb_cacheinfo() c
    ORDER BY c.name
  LOOP
    attnums[1] := info.key1;
    attnums[2] := info.key2;
    attnums[3] := info.key3;
    attnums[4] := info.key4;

    -- Middle loop: Iterate over the keys for the current catalog cache.
    FOR fkeys IN
      SELECT keys
      FROM catcache_keys
      WHERE catcache_name = info.catcache_name
    LOOP
      FOR i IN 1..4 LOOP
        /* 
         * Construct the the expressions for the keys we will hash,
         * as an array of text. Each element is the name of a column
         * in the table. The array always has 4 elements. If there are 
         * fewer than 4 keys, the remaining elements are 'NULL'.
         *
         * Example key:
         * {(('pg_ddl_command_send')::name),(('32')::oidvector),(('11')::oid),"NULL"}
         */
        IF attnums[i] = 0 OR i > info.nkeys THEN
          cache_key[i] := 'NULL';
        ELSE
          SELECT attname, atttypid
            INTO attrec
            FROM pg_attribute
            WHERE attrelid    = info.reloid
              AND attnum      = attnums[i]
              AND NOT attisdropped;

          fkey := 'f' || i;  -- JSON fields are f1, f2, f3, f4, etc.

          IF attrec.atttypid = 'pg_catalog.oidvector'::regtype THEN
            -- oidvector expects a space-separated list of numeric OIDs.
            -- The JSON is typically an array like ["2281","2282"].
            -- We'll extract that array and join with a space.
            BEGIN
              SELECT COALESCE(string_agg(elem, ' '), '')
                INTO casttxt
                FROM jsonb_array_elements_text(fkeys.keys->fkey) elem;
            EXCEPTION
              -- If the JSON is not an array (e.g. single OID), just cast directly.
              WHEN others THEN
                casttxt := fkeys.keys->>fkey;
            END;

            casttxt := '((' || quote_literal(casttxt) || ')::oidvector)';

          -- For some objects, PostgreSQL automatically converts the OID to the object name as a string. 
          -- This is an issue because multiple functions can have the same name, so we need to use the 
          -- raw OID as an integer to disambiguate.
          ELSIF attrec.atttypid IN (
                 'pg_catalog.regclass'::regtype,
                 'pg_catalog.regproc'::regtype,
                 'pg_catalog.regtype'::regtype,
                 'pg_catalog.regoper'::regtype,
                 'pg_catalog.regprocedure'::regtype,
                 'pg_catalog.regrole'::regtype
               )
          THEN
            casttxt := '((' || quote_literal(fkeys.keys->>fkey) || ')::oid)';
          ELSE
            -- For other types, we just cast to the type of the column.
            casttxt := '((' 
                       || quote_literal(fkeys.keys->>fkey) 
                       || ')::' 
                       || format_type(attrec.atttypid, NULL) 
                       || ')';
          END IF;

          cache_key[i] := casttxt;
        END IF;
      END LOOP;

      /*
       * Construct the query to hash the keys and insert the results into the catcache_hash_results table.
       * This query takes one key from the catcache_keys table, casts the key columns to the correct types,
       * hashes the key, and inserts the result into the catcache_hash_results table.
       *
       * Example generated query:
       * INSERT INTO catcache_hash_results (catcache_name, keys, hashval)
       * VALUES (
       *     'PROCNAMEARGSNSP', -- info.catcache_name
       *     '{"f1": "pg_ddl_command_send", "f2": ["32"], "f3": "11", "f4": null}'::jsonb, -- fkeys.keys
       *     catalog_cache_compute_hash_tuple(
       *         ROW(
       *             44, -- info.cache_id
       *             ('pg_ddl_command_send')::name,
       *             ('32')::oidvector,
       *             ('11')::oid,
       *             NULL
       *         )
       *     )
       * );
       */

      EXECUTE format($q$
        INSERT INTO catcache_hash_results (catcache_name, keys, hashval)
        VALUES (
          %L,                -- info.catcache_name
          %L::jsonb,         -- fkeys.keys
          catalog_cache_compute_hash_tuple(
            ROW(
              %s,            -- info.cache_id
              %s, %s, %s, %s -- cache_key[1], cache_key[2], cache_key[3], cache_key[4]
            )
          )
        )
      $q$,
      info.catcache_name,
      fkeys.keys::text,
      info.cache_id,
      cache_key[1],
      cache_key[2],
      cache_key[3],
      cache_key[4]);
    END LOOP;
  END LOOP;
END
$$;

-- Verify that we're testing all of the caches and 
-- the right number of keys for each catalog cache.
SELECT COUNT(DISTINCT catcache_name) FROM catcache_keys;

SELECT catcache_name, COUNT(*)
FROM catcache_keys
GROUP BY catcache_name
ORDER BY catcache_name;

-- Use extended output to make the diff easier to read.
\x

-- Finally, select all the results that we computed.
SELECT
  catcache_keys.catcache_name,
  catcache_keys.keys,
  results.hashval
FROM catcache_keys
JOIN catcache_hash_results results
  ON catcache_keys.catcache_name = results.catcache_name
  AND catcache_keys.keys = results.keys
-- Temp schemas change their names on every run.
WHERE NOT (
     catcache_keys.keys->>'f1' LIKE 'pg_temp_%'
  OR catcache_keys.keys->>'f1' LIKE 'pg_toast_temp_%'
)
ORDER BY catcache_keys.catcache_name, catcache_keys.keys;

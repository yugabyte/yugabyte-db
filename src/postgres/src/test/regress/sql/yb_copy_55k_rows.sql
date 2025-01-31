--
-- COPY
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

\set filename :abs_srcdir '/data/all-airport-codes.csv'
COPY airports FROM :'filename' CSV HEADER;
-- Workaround: Sleeping 2 minutes for docdb to complete its background tasks (issue #4855).
-- Don't sleep more than 60 seconds each to avoid any RPC issues.
SELECT pg_sleep(60);
SELECT pg_sleep(60);

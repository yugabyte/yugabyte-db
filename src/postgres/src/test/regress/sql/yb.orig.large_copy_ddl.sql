--
-- COPY large file while performing DDL in same txn
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

BEGIN;

CREATE INDEX ON airports (type);
CREATE INDEX ON airports (name);

\set filename :abs_srcdir '/data/airport-codes.csv'
COPY airports FROM :'filename' CSV HEADER;

COMMIT;

--
-- COPY large corrupt file
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

TRUNCATE TABLE airports;

-- should fail once it reaches corrupt input on last line
\set filename :abs_srcdir '/data/airport-codes-corrupt.csv'
COPY airports FROM :'filename' CSV HEADER;

-- table should be empty
SELECT COUNT(*) FROM airports;

--
-- Verify COPY fails if duplicate key error is hit.
--
\set filename :abs_srcdir '/data/airport-codes.csv'
COPY airports FROM :'filename' CSV HEADER;
DELETE FROM airports WHERE ident != '9LA6';

-- should fail with duplicate key error
\set filename :abs_srcdir '/data/airport-codes.csv'
COPY airports FROM :'filename' CSV HEADER;

-- table should just have one row
SELECT COUNT(*) FROM airports;

-- prepare for next tests
TRUNCATE TABLE airports;

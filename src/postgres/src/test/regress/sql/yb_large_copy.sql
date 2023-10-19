--
-- COPY large file
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

\set filename :abs_srcdir '/data/airport-codes.csv'
COPY airports FROM :'filename' CSV HEADER;

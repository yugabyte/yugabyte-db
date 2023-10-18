--
-- COPY
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR
\getenv abs_builddir PG_ABS_BUILDDIR

-- CLASS POPULATION
--	(any resemblance to real life is purely coincidental)
--
\set filename :abs_srcdir '/data/agg.data'
COPY aggtest FROM :'filename';

\set filename :abs_srcdir '/data/onek.data'
COPY onek FROM :'filename';

\set filename :abs_builddir '/results/onek.data'
COPY onek TO :'filename';

DELETE FROM onek;

COPY onek FROM :'filename';

\set filename :abs_srcdir '/data/tenk.data'
COPY tenk1 FROM :'filename';

\set filename :abs_srcdir '/data/person.data'
COPY person FROM :'filename';

\set filename :abs_srcdir '/data/tsearch.data'
COPY test_tsvector FROM :'filename';

\set filename :abs_srcdir '/data/jsonb.data'
COPY testjsonb FROM :'filename';

\set filename :abs_srcdir '/data/array.data'
COPY array_op_test FROM :'filename';

\set filename :abs_srcdir '/data/array.data'
COPY array_index_op_test FROM :'filename';

-- analyze all the data we just loaded, to ensure plan consistency
-- in later tests

ANALYZE tenk1;
ANALYZE person;

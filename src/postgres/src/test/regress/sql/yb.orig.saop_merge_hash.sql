--
-- See yb_saop_merge_schedule for details about the test.
-- TODO(#29072): fix output.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun_saop_merge.sql'
\i :filename

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM h3r2n WHERE h1 = 6 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 LIMIT 5'
:explain2

-- =, IN, sort...
-- SAOP merge should not be used.
\set query 'SELECT h1, h3, r1, r2, n, h2 FROM h3r2n WHERE h1 = 6 AND h2 IN (1, 3, 5, 7, 9) ORDER BY h3, r1, r2, n LIMIT 5'
:explain2

-- =, IN, =, sort...
\set query 'SELECT h1, h3, r1, r2, n, h2 FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY r1, r2, n LIMIT 5'
:explain2run2

-- =, IN, =/sort, sort...
\set query 'SELECT h1, h3, r1, r2, n, h2 FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY h3, r1, r2, n LIMIT 5'
:explain2run2

-- =, =, IN, sort...
\set query 'SELECT h1, h2, r1, r2, n, h3 FROM h3r2n WHERE h1 = 1 AND h2 = 8 AND h3 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) ORDER BY r1, r2, n LIMIT 5'
:explain2run2

-- =, =, IN/sort, sort...
-- SAOP merge should not be used.
\set query 'SELECT h1, h2, h3, r1, r2, n FROM h3r2n WHERE h1 = 1 AND h2 = 8 AND h3 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) ORDER BY h3, r1, r2, n LIMIT 5'
:explain2

-- =/sort, IN, IN, sort...
\set query 'SELECT h1, r1, r2, n, h2, h3 FROM h3r2n WHERE h1 = 2 AND h2 IN (3, 4, 5, 6, 7) AND h3 IN (6, 7, 8, 9, 10) ORDER BY h1, r1, r2, n LIMIT 5'
:explain2run2

/* TODO(#29079): uncomment when fixing this issue.
-- yb_hash_code equality
\set query 'SELECT r1, r2, n, yb_hash_code(h1, h2, h3), h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) = 28655 AND h1 IN (5, 7) AND h2 IN (1, 3, 7) AND h3 IN (3, 6, 7) ORDER BY r1, r2, n LIMIT 5'
:explain2run2

-- yb_hash_code inequality
\set query 'SELECT r1, r2, n, yb_hash_code(h1, h2, h3), h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) < 12283 AND h1 IN (5, 7) AND h2 IN (1, 3, 7) AND h3 IN (3, 6, 7) ORDER BY r1, r2, n LIMIT 5'
:explain2run2
*/; -- semicolon to avoid bleeding hints

-- yb_hash_code IN
-- Third hint is to use SAOP merge as the second hint ends up using sort.
\set query 'SELECT r1, r2, n, yb_hash_code(h1, h2, h3), h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) IN (17834, 28655, 32412) AND h1 IN (5, 7) AND h2 IN (1, 3, 7) AND h3 IN (3, 6, 7) ORDER BY r1, r2, n LIMIT 5'
\set hint3 '/*+Set(enable_sort off) Set(yb_max_saop_merge_streams 64)*/'
:explain3run3

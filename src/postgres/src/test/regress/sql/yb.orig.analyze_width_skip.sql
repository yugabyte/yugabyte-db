--
-- #31506: ybFetchSample does not materialize a sampled value whose bytes the
-- statistics code would never read, standing it in with its size instead.
--
-- Every statistic, avg_width included, must come out identical with the
-- substitution on and off: a stand-in reports the width of the value it
-- stands in for, so the feature is invisible to the statistics.
--
-- 300 rows: far below targrows, so every row is sampled and the statistics
-- are deterministic.  It also keeps the table small enough for the 1 GB
-- tserver memory limit the Java regress harness runs with.  Each table has a
-- primary key: without one the row order (hence correlation) varies per run.
--
-- avoid bit-exact output here because operations may not be bit-exact.
SET extra_float_digits = 0;
-- 300 * this is targrows; at the default the table below is sampled whole.
SET default_statistics_target = 100;

-- deterministic, incompressible text: n 32-char md5 blocks
CREATE FUNCTION wide_text(seed text, n int) RETURNS text IMMUTABLE STRICT LANGUAGE sql AS
  'SELECT string_agg(md5(seed || g), '''' ORDER BY g) FROM generate_series(1, n) g';
CREATE DOMAIN wide_dom AS text;
CREATE TYPE textrange AS RANGE (subtype = text);

CREATE TABLE width_skip_t (
  id        int PRIMARY KEY,
  small     text,      -- 2 B, 7 values: never wide, full MCV
  wide      text,      -- 1504 B on every row: skipped, stood in
  at_cap    text,      -- 1020 B: VARSIZE == WIDTH_THRESHOLD, read in full
  over_cap  text,      -- 1021 B: VARSIZE == WIDTH_THRESHOLD + 1, skipped
  straddle  text,      -- 1504 B on even rows, short on odd
  nullable  text,      -- 1504 B, NULL every 7th row
  dom       wide_dom,  -- domain over text: capped through the base type
  j_small   json,      -- ~10 B: no "=", trivial stats; kept (short header)
  j_wide    json,      -- 1.4 KB: no "=", trivial stats; stood in
  x_wide    xml,       -- 1.5 KB: no "=", trivial stats; stood in
  arr       text[],    -- 1.5 KB element: array stats read it (< 64 KB)
  arr_wide  text[],    -- 70 KB element on 50 rows: array stats skip it too
  j_arr     json[],    -- 1.4 KB json element, no "=": trivial stats; stood in
  tsv       tsvector,  -- ts_typanalyze: always read
  rng       textrange, -- range_typanalyze: always read
  mrng      int4multirange, -- multirange_typanalyze: always read
  t_exprix  text,      -- read by an expression index
  t_partix  text,      -- read by a partial-index predicate
  t_ext     text,      -- read by extended statistics (ndistinct)
  t_extexpr text,      -- read by an extended-statistics expression
  t_mcv     text,      -- read by extended statistics (mcv)
  t_deps    text       -- read by extended statistics (dependencies)
);
CREATE INDEX width_skip_t_exprix ON width_skip_t (md5(t_exprix));
CREATE INDEX width_skip_t_partix ON width_skip_t (id) WHERE length(t_partix) > 10;
CREATE STATISTICS width_skip_t_ext (ndistinct) ON id, t_ext FROM width_skip_t;
CREATE STATISTICS width_skip_t_extexpr ON (length(t_extexpr)) FROM width_skip_t;
CREATE STATISTICS width_skip_t_mcv (mcv) ON small, t_mcv FROM width_skip_t;
CREATE STATISTICS width_skip_t_deps (dependencies) ON small, t_deps FROM width_skip_t;

INSERT INTO width_skip_t
SELECT i,
       'v' || (i % 7),                                  -- 2 B text, i.e. 'v2'
       wide_text('wide' || i, 47),                      -- 1504 B hex, i.e. '70eb9098.....206a'
       lpad(i::text, 1020, 'a'),                        -- 1020 B text, i.e. 'aaaaaaaa.....aaa2'
       lpad(i::text, 1021, 'b'),                        -- 1021 B text, i.e. 'bbbbbbbb.....bbb2'
       CASE WHEN i % 2 = 1 THEN 'short' || (i % 11)     -- 6 B on odd rows, i.e. 'short1'
            ELSE wide_text('str' || i, 47) END,         -- 1504 B on even, i.e. 'ba601fc7.....5892'
       CASE WHEN i % 7 = 0 THEN NULL                    -- NULL every 7th row
            ELSE wide_text('nul' || i, 47) END,         -- else 1504 B, i.e. '3b517b66.....e770'
       wide_text('dom' || i, 47),                       -- 1504 B domain, i.e. 'c03608fd.....921d'
       ('{"k":' || i || '}')::json,                     -- 7 B json, i.e. '{"k":2}'
       ('{"k":"' || wide_text('j' || i, 44) || '"}')::json,  -- 1416 B json, i.e. '{"k":"1d.....2c"}'
       ('<v>' || wide_text('x' || i, 47) || '</v>')::xml,    -- 1511 B xml, i.e. '<v>d3d95.....</v>'
       ARRAY[wide_text('arr' || i, 47)],                -- one 1504 B element, i.e. '{3178c29.....bac}'
       CASE WHEN i <= 50                                -- one 70400 B element on 50 rows,
            THEN ARRAY[wide_text('big' || i, 2200)]     --   i.e. '{8b916a4.....765}'
            ELSE ARRAY['a' || (i % 9)] END,             -- 2 B elements after, i.e. '{a6}'
       ARRAY[('{"k":"' || wide_text('jarr' || i, 44) || '"}')::json],  -- one 1416 B json element, i.e. '{"{\"k\":\"1a.....05\"}"}'
       to_tsvector('simple', repeat('alpha beta gamma delta ', 80)),  -- 4 lexemes, i.e. "'alpha':1,5.....,319"
       textrange(wide_text('rng' || i, 22), wide_text('rng' || i, 22) || 'z'),  -- 704 B bounds, i.e. '[7648a03a.....88dz)'
       (SELECT range_agg(int4range(g * 10 + i, g * 10 + i + 5))       -- 150 ranges, 1960 B,
        FROM generate_series(1, 150) g),                              --   i.e. '{[12,17),[22,27).....[1502,1507)}'
       wide_text('exprix' || i, 47),                    -- 1504 B hex, i.e. '630d8971.....7592'
       wide_text('partix' || i, 47),                    -- 1504 B hex, i.e. '9b22393b.....4a89'
       wide_text('ext' || i, 47),                       -- 1504 B hex, i.e. '82b98234.....5141'
       wide_text('extexpr' || i, 47),                   -- 1504 B hex, i.e. '98b98c76.....e2b9'
       CASE WHEN i % 2 = 0 THEN wide_text('mcv' || i, 47)  -- 1504 B on even rows,
            ELSE 'm' || (i % 5) END,                    --   i.e. 'f4cf4661.....480e', else 'm1'
       CASE WHEN i % 3 = 0 THEN wide_text('dep' || i, 47)  -- 1504 B every 3rd row,
            ELSE 'd' || (i % 5) END                     --   i.e. '595f1737.....0ef1', else 'd2'
FROM generate_series(1, 300) i;

-- a whole-row reference reads every column: nothing may be stood in
CREATE TABLE width_skip_row (id int PRIMARY KEY, w text);
CREATE FUNCTION width_skip_row_key(width_skip_row) RETURNS text IMMUTABLE STRICT LANGUAGE sql AS
  'SELECT md5($1.w)';
CREATE INDEX width_skip_row_ix ON width_skip_row (width_skip_row_key(width_skip_row));
INSERT INTO width_skip_row SELECT i, wide_text('row' || i, 70) FROM generate_series(1, 100) i;

CREATE TABLE width_skip_part (id int PRIMARY KEY, w text) PARTITION BY RANGE (id);
CREATE TABLE width_skip_part1 PARTITION OF width_skip_part FOR VALUES FROM (1) TO (1000);
CREATE STATISTICS width_skip_part_ext (ndistinct) ON id, w FROM width_skip_part;
INSERT INTO width_skip_part SELECT i, wide_text('part' || i, 70) FROM generate_series(1, 100) i;

-- a partition created after its parent dropped a column: its columns are
-- numbered one lower than the parent's, so the parent's statistics must
-- match each column to the partition's by name, not by position
CREATE TABLE width_skip_dropped (id int PRIMARY KEY, old_col text, kept text,
                                 data_skipped text, j_wide json, arr text[])
  PARTITION BY RANGE (id);
ALTER TABLE width_skip_dropped DROP COLUMN old_col;
CREATE TABLE width_skip_dropped1 PARTITION OF width_skip_dropped
  FOR VALUES FROM (1) TO (1000);
CREATE STATISTICS width_skip_dropped_ext (ndistinct) ON id, kept FROM width_skip_dropped;
INSERT INTO width_skip_dropped
SELECT i, wide_text('kept' || i, 70),                -- 2240 B hex, i.e. '24562f8b.....424f'
       wide_text('data_skipped' || i, 70),           -- 2240 B hex, i.e. '35cf4823.....b75b'
       ('{"k":"' || wide_text('j' || i, 44) || '"}')::json,  -- 1416 B json, i.e. '{"k":"1d.....2c"}'
       ARRAY[wide_text('arr' || i, 47)]              -- one 1504 B element, i.e. '{3178c29.....bac}'
FROM generate_series(1, 100) i;

-- a column list: columns outside it are stood in, unless an
-- extended-statistics expression reads them
CREATE TABLE width_skip_cols (id int PRIMARY KEY, analyzed text,
                              data_skipped text, expr_input text);
CREATE STATISTICS width_skip_cols_expr ON (length(expr_input)) FROM width_skip_cols;
INSERT INTO width_skip_cols
SELECT i, 'v' || (i % 7),                             -- 2 B text, i.e. 'v2'
       wide_text('data_skipped' || i, 70),            -- 2240 B hex, i.e. '35cf4823.....b75b'
       wide_text('expr_input' || i, 70)               -- 2240 B hex, i.e. 'c7a30115.....d70d'
FROM generate_series(1, 100) i;

-- statistics turned off: such a column is stood in, unless an index
-- expression reads it
CREATE TABLE width_skip_set_statistics_0 (id int PRIMARY KEY,
                                          data_skipped text, index_input text);
ALTER TABLE width_skip_set_statistics_0 ALTER COLUMN data_skipped SET STATISTICS 0;
ALTER TABLE width_skip_set_statistics_0 ALTER COLUMN index_input SET STATISTICS 0;
CREATE INDEX width_skip_set_statistics_0_ix ON width_skip_set_statistics_0 (length(index_input));
INSERT INTO width_skip_set_statistics_0
SELECT i, wide_text('data_skipped' || i, 70),         -- 2240 B hex, i.e. '35cf4823.....b75b'
       wide_text('index_input' || i, 70)              -- 2240 B hex, i.e. 'da83d16d.....deae'
FROM generate_series(1, 100) i;

-- mcv statistics on a column and an expression: the column is stood in, and
-- the expression's input is kept in full
CREATE TABLE width_skip_mcv_with_expr (id int PRIMARY KEY, data_skipped text,
                                       expr_input text);
CREATE STATISTICS width_skip_mcv_with_expr_s (mcv)
  ON data_skipped, (length(expr_input)) FROM width_skip_mcv_with_expr;
INSERT INTO width_skip_mcv_with_expr
SELECT i, CASE WHEN i % 2 = 0 THEN wide_text('data_skipped' || i, 70)  -- 2240 B on even rows,
               ELSE 'm' || (i % 5) END,                                --   i.e. '35cf4823.....b75b', else 'm1'
       wide_text('expr_input' || i, 70 + i % 3)                        -- 2240-2304 B hex, i.e. 'c7a30115.....493f'
FROM generate_series(1, 100) i;

-- a row under 2 KB: its values are never stood in, even over the cap
CREATE TABLE width_skip_small_row (id int PRIMARY KEY, wide text);
INSERT INTO width_skip_small_row
SELECT i, wide_text('smallrow' || i, 47)                -- 1504 B hex, i.e. 'ef537c10.....29f0'
FROM generate_series(1, 100) i;

-- The tables above again, as a partitioned table (_part) and as an
-- inheritance parent (_inh).  ANALYZE builds the parent's statistics from its
-- children's rows, sampled in each child's own column numbering, so the
-- parent's statistics must match each column to the child's by name:
-- - every parent drops a column, so a partition created after that
--   (_part1) numbers its columns one lower than the parent;
-- - the other child (_part2, _inh1) is created on its own, with its columns
--   in reverse order and a dropped column, then attached or inherited;
-- - an inheritance child also has a wide column (extra) the parent does not
--   have.

-- width_skip_t on every 5th row, to keep the data small
CREATE TABLE width_skip_t_part (old_col text, LIKE width_skip_t, PRIMARY KEY (id))
  PARTITION BY RANGE (id);
ALTER TABLE width_skip_t_part DROP COLUMN old_col;
CREATE TABLE width_skip_t_part1 PARTITION OF width_skip_t_part FOR VALUES FROM (1) TO (26);
CREATE TABLE width_skip_t_part2 (
  t_deps text, t_mcv text, t_extexpr text, t_ext text, t_partix text, t_exprix text,
  mrng int4multirange, rng textrange, tsv tsvector, gone text, j_arr json[],
  arr_wide text[], arr text[], x_wide xml, j_wide json, j_small json, dom wide_dom,
  nullable text, straddle text, over_cap text, at_cap text, wide text, small text,
  id int PRIMARY KEY);
ALTER TABLE width_skip_t_part2 DROP COLUMN gone;
ALTER TABLE width_skip_t_part ATTACH PARTITION width_skip_t_part2 FOR VALUES FROM (26) TO (1000);
CREATE INDEX width_skip_t_part_exprix ON width_skip_t_part (md5(t_exprix));
CREATE INDEX width_skip_t_part_partix ON width_skip_t_part (id) WHERE length(t_partix) > 10;
CREATE STATISTICS width_skip_t_part_ext (ndistinct) ON id, t_ext FROM width_skip_t_part;
CREATE STATISTICS width_skip_t_part_extexpr ON (length(t_extexpr)) FROM width_skip_t_part;
CREATE STATISTICS width_skip_t_part_mcv (mcv) ON small, t_mcv FROM width_skip_t_part;
CREATE STATISTICS width_skip_t_part_deps (dependencies) ON small, t_deps FROM width_skip_t_part;
-- a partition's own statistics: its own ANALYZE reads wide in full
CREATE STATISTICS width_skip_t_part2_ext (ndistinct) ON id, wide FROM width_skip_t_part2;
INSERT INTO width_skip_t_part SELECT * FROM width_skip_t WHERE id % 5 = 0;

CREATE TABLE width_skip_t_inh (old_col text, LIKE width_skip_t, PRIMARY KEY (id));
ALTER TABLE width_skip_t_inh DROP COLUMN old_col;
CREATE INDEX width_skip_t_inh_exprix ON width_skip_t_inh (md5(t_exprix));
CREATE INDEX width_skip_t_inh_partix ON width_skip_t_inh (id) WHERE length(t_partix) > 10;
CREATE STATISTICS width_skip_t_inh_ext (ndistinct) ON id, t_ext FROM width_skip_t_inh;
CREATE STATISTICS width_skip_t_inh_extexpr ON (length(t_extexpr)) FROM width_skip_t_inh;
CREATE STATISTICS width_skip_t_inh_mcv (mcv) ON small, t_mcv FROM width_skip_t_inh;
CREATE STATISTICS width_skip_t_inh_deps (dependencies) ON small, t_deps FROM width_skip_t_inh;
CREATE TABLE width_skip_t_inh1 (
  extra text, t_deps text, t_mcv text, t_extexpr text, t_ext text, t_partix text,
  t_exprix text, mrng int4multirange, rng textrange, tsv tsvector, gone text,
  j_arr json[], arr_wide text[], arr text[], x_wide xml, j_wide json, j_small json,
  dom wide_dom, nullable text, straddle text, over_cap text, at_cap text, wide text,
  small text, id int PRIMARY KEY);
ALTER TABLE width_skip_t_inh1 DROP COLUMN gone;
ALTER TABLE width_skip_t_inh1 INHERIT width_skip_t_inh;
-- a child's own statistics: its own ANALYZE reads extra in full, while the
-- parent's statistics never read extra, so the parent's ANALYZE stands it in
CREATE STATISTICS width_skip_t_inh1_ext (ndistinct) ON id, extra FROM width_skip_t_inh1;
INSERT INTO width_skip_t_inh SELECT * FROM width_skip_t WHERE id % 5 = 0 AND id < 26;
INSERT INTO width_skip_t_inh1 (id, small, wide, at_cap, over_cap, straddle, nullable,
  dom, j_small, j_wide, x_wide, arr, arr_wide, j_arr, tsv, rng, mrng, t_exprix,
  t_partix, t_ext, t_extexpr, t_mcv, t_deps, extra)
SELECT *, wide_text('extra' || id, 70)                  -- 2240 B hex, i.e. 'cb86b12b.....b0d2'
FROM width_skip_t WHERE id % 5 = 0 AND id >= 26;

-- width_skip_t on every 5th row again, as a partition of partitions: ANALYZE
-- builds statistics from the leaf partitions' rows both for the top table and
-- for its partitioned partition (_part_of_part1), and the levels number their
-- columns differently:
-- - _part_of_part1 is created on its own, with its columns in reverse order
--   and a dropped column, then attached;
-- - _part_of_part1a is created after that, so it numbers _part_of_part1's
--   columns without the gap;
-- - _part_of_part1b is created on its own, in the top table's layout, then
--   attached.
CREATE TABLE width_skip_t_part_of_part (old_col text, LIKE width_skip_t, PRIMARY KEY (id))
  PARTITION BY RANGE (id);
ALTER TABLE width_skip_t_part_of_part DROP COLUMN old_col;
CREATE TABLE width_skip_t_part_of_part1 (
  t_deps text, t_mcv text, t_extexpr text, t_ext text, t_partix text, t_exprix text,
  mrng int4multirange, rng textrange, tsv tsvector, gone text, j_arr json[],
  arr_wide text[], arr text[], x_wide xml, j_wide json, j_small json, dom wide_dom,
  nullable text, straddle text, over_cap text, at_cap text, wide text, small text,
  id int PRIMARY KEY)
  PARTITION BY RANGE (id);
ALTER TABLE width_skip_t_part_of_part1 DROP COLUMN gone;
CREATE TABLE width_skip_t_part_of_part1a PARTITION OF width_skip_t_part_of_part1
  FOR VALUES FROM (1) TO (26);
CREATE TABLE width_skip_t_part_of_part1b (old_col text, LIKE width_skip_t, PRIMARY KEY (id));
ALTER TABLE width_skip_t_part_of_part1b DROP COLUMN old_col;
ALTER TABLE width_skip_t_part_of_part1 ATTACH PARTITION width_skip_t_part_of_part1b
  FOR VALUES FROM (26) TO (1000);
ALTER TABLE width_skip_t_part_of_part ATTACH PARTITION width_skip_t_part_of_part1
  FOR VALUES FROM (1) TO (1000);
CREATE INDEX width_skip_t_part_of_part_exprix ON width_skip_t_part_of_part (md5(t_exprix));
CREATE INDEX width_skip_t_part_of_part_partix ON width_skip_t_part_of_part (id)
  WHERE length(t_partix) > 10;
CREATE STATISTICS width_skip_t_part_of_part_ext (ndistinct) ON id, t_ext
  FROM width_skip_t_part_of_part;
CREATE STATISTICS width_skip_t_part_of_part_extexpr ON (length(t_extexpr))
  FROM width_skip_t_part_of_part;
CREATE STATISTICS width_skip_t_part_of_part_mcv (mcv) ON small, t_mcv
  FROM width_skip_t_part_of_part;
CREATE STATISTICS width_skip_t_part_of_part_deps (dependencies) ON small, t_deps
  FROM width_skip_t_part_of_part;
-- the partitioned partition's own statistics: its ANALYZE reads wide in full,
-- while the top table's ANALYZE stands it in
CREATE STATISTICS width_skip_t_part_of_part1_ext (ndistinct) ON id, wide
  FROM width_skip_t_part_of_part1;
INSERT INTO width_skip_t_part_of_part SELECT * FROM width_skip_t WHERE id % 5 = 0;

-- width_skip_row: an index on a partitioned table cannot have a whole-row
-- reference once its partitions number their columns differently, so each
-- partition gets its own
CREATE TABLE width_skip_row_part (old_col text, LIKE width_skip_row, PRIMARY KEY (id))
  PARTITION BY RANGE (id);
ALTER TABLE width_skip_row_part DROP COLUMN old_col;
CREATE TABLE width_skip_row_part1 PARTITION OF width_skip_row_part FOR VALUES FROM (1) TO (51);
CREATE TABLE width_skip_row_part2 (w text, gone text, id int PRIMARY KEY);
ALTER TABLE width_skip_row_part2 DROP COLUMN gone;
ALTER TABLE width_skip_row_part ATTACH PARTITION width_skip_row_part2 FOR VALUES FROM (51) TO (1000);
CREATE FUNCTION width_skip_row_part1_key(width_skip_row_part1) RETURNS text IMMUTABLE STRICT
  LANGUAGE sql AS 'SELECT md5($1.w)';
CREATE INDEX width_skip_row_part1_ix ON width_skip_row_part1
  (width_skip_row_part1_key(width_skip_row_part1));
CREATE FUNCTION width_skip_row_part2_key(width_skip_row_part2) RETURNS text IMMUTABLE STRICT
  LANGUAGE sql AS 'SELECT md5($1.w)';
CREATE INDEX width_skip_row_part2_ix ON width_skip_row_part2
  (width_skip_row_part2_key(width_skip_row_part2));
INSERT INTO width_skip_row_part SELECT * FROM width_skip_row;

CREATE TABLE width_skip_row_inh (old_col text, LIKE width_skip_row, PRIMARY KEY (id));
ALTER TABLE width_skip_row_inh DROP COLUMN old_col;
CREATE FUNCTION width_skip_row_inh_key(width_skip_row_inh) RETURNS text IMMUTABLE STRICT
  LANGUAGE sql AS 'SELECT md5($1.w)';
CREATE INDEX width_skip_row_inh_ix ON width_skip_row_inh
  (width_skip_row_inh_key(width_skip_row_inh));
CREATE TABLE width_skip_row_inh1 (extra text, w text, gone text, id int PRIMARY KEY);
ALTER TABLE width_skip_row_inh1 DROP COLUMN gone;
ALTER TABLE width_skip_row_inh1 INHERIT width_skip_row_inh;
CREATE FUNCTION width_skip_row_inh1_key(width_skip_row_inh1) RETURNS text IMMUTABLE STRICT
  LANGUAGE sql AS 'SELECT md5($1.w)';
CREATE INDEX width_skip_row_inh1_ix ON width_skip_row_inh1
  (width_skip_row_inh1_key(width_skip_row_inh1));
INSERT INTO width_skip_row_inh SELECT * FROM width_skip_row WHERE id <= 50;
INSERT INTO width_skip_row_inh1 (id, w, extra)
SELECT *, wide_text('extra' || id, 70)                  -- 2240 B hex, i.e. 'cb86b12b.....b0d2'
FROM width_skip_row WHERE id > 50;

-- width_skip_cols
CREATE TABLE width_skip_cols_part (old_col text, LIKE width_skip_cols, PRIMARY KEY (id))
  PARTITION BY RANGE (id);
ALTER TABLE width_skip_cols_part DROP COLUMN old_col;
CREATE TABLE width_skip_cols_part1 PARTITION OF width_skip_cols_part FOR VALUES FROM (1) TO (51);
CREATE TABLE width_skip_cols_part2 (expr_input text, data_skipped text, gone text,
                                    analyzed text, id int PRIMARY KEY);
ALTER TABLE width_skip_cols_part2 DROP COLUMN gone;
ALTER TABLE width_skip_cols_part ATTACH PARTITION width_skip_cols_part2 FOR VALUES FROM (51) TO (1000);
CREATE STATISTICS width_skip_cols_part_expr ON (length(expr_input)) FROM width_skip_cols_part;
INSERT INTO width_skip_cols_part SELECT * FROM width_skip_cols;

CREATE TABLE width_skip_cols_inh (old_col text, LIKE width_skip_cols, PRIMARY KEY (id));
ALTER TABLE width_skip_cols_inh DROP COLUMN old_col;
CREATE STATISTICS width_skip_cols_inh_expr ON (length(expr_input)) FROM width_skip_cols_inh;
CREATE TABLE width_skip_cols_inh1 (extra text, expr_input text, data_skipped text, gone text,
                                   analyzed text, id int PRIMARY KEY);
ALTER TABLE width_skip_cols_inh1 DROP COLUMN gone;
ALTER TABLE width_skip_cols_inh1 INHERIT width_skip_cols_inh;
INSERT INTO width_skip_cols_inh SELECT * FROM width_skip_cols WHERE id <= 50;
INSERT INTO width_skip_cols_inh1 (id, analyzed, data_skipped, expr_input, extra)
SELECT *, wide_text('extra' || id, 70)                  -- 2240 B hex, i.e. 'cb86b12b.....b0d2'
FROM width_skip_cols WHERE id > 50;

-- width_skip_set_statistics_0: SET STATISTICS on a parent also sets it on
-- the children it has at that point
CREATE TABLE width_skip_set_statistics_0_part (old_col text, LIKE width_skip_set_statistics_0,
                                               PRIMARY KEY (id))
  PARTITION BY RANGE (id);
ALTER TABLE width_skip_set_statistics_0_part DROP COLUMN old_col;
CREATE TABLE width_skip_set_statistics_0_part1 PARTITION OF width_skip_set_statistics_0_part
  FOR VALUES FROM (1) TO (51);
CREATE TABLE width_skip_set_statistics_0_part2 (index_input text, gone text,
                                                data_skipped text, id int PRIMARY KEY);
ALTER TABLE width_skip_set_statistics_0_part2 DROP COLUMN gone;
ALTER TABLE width_skip_set_statistics_0_part ATTACH PARTITION width_skip_set_statistics_0_part2
  FOR VALUES FROM (51) TO (1000);
ALTER TABLE width_skip_set_statistics_0_part ALTER COLUMN data_skipped SET STATISTICS 0;
ALTER TABLE width_skip_set_statistics_0_part ALTER COLUMN index_input SET STATISTICS 0;
CREATE INDEX width_skip_set_statistics_0_part_ix ON width_skip_set_statistics_0_part
  (length(index_input));
INSERT INTO width_skip_set_statistics_0_part SELECT * FROM width_skip_set_statistics_0;

CREATE TABLE width_skip_set_statistics_0_inh (old_col text, LIKE width_skip_set_statistics_0,
                                              PRIMARY KEY (id));
ALTER TABLE width_skip_set_statistics_0_inh DROP COLUMN old_col;
CREATE INDEX width_skip_set_statistics_0_inh_ix ON width_skip_set_statistics_0_inh
  (length(index_input));
CREATE TABLE width_skip_set_statistics_0_inh1 (extra text, index_input text, gone text,
                                               data_skipped text, id int PRIMARY KEY);
ALTER TABLE width_skip_set_statistics_0_inh1 DROP COLUMN gone;
ALTER TABLE width_skip_set_statistics_0_inh1 INHERIT width_skip_set_statistics_0_inh;
ALTER TABLE width_skip_set_statistics_0_inh ALTER COLUMN data_skipped SET STATISTICS 0;
ALTER TABLE width_skip_set_statistics_0_inh ALTER COLUMN index_input SET STATISTICS 0;
INSERT INTO width_skip_set_statistics_0_inh SELECT * FROM width_skip_set_statistics_0 WHERE id <= 50;
INSERT INTO width_skip_set_statistics_0_inh1 (id, data_skipped, index_input, extra)
SELECT *, wide_text('extra' || id, 70)                  -- 2240 B hex, i.e. 'cb86b12b.....b0d2'
FROM width_skip_set_statistics_0 WHERE id > 50;

-- width_skip_mcv_with_expr
CREATE TABLE width_skip_mcv_with_expr_part (old_col text, LIKE width_skip_mcv_with_expr,
                                            PRIMARY KEY (id))
  PARTITION BY RANGE (id);
ALTER TABLE width_skip_mcv_with_expr_part DROP COLUMN old_col;
CREATE TABLE width_skip_mcv_with_expr_part1 PARTITION OF width_skip_mcv_with_expr_part
  FOR VALUES FROM (1) TO (51);
CREATE TABLE width_skip_mcv_with_expr_part2 (expr_input text, gone text, data_skipped text,
                                             id int PRIMARY KEY);
ALTER TABLE width_skip_mcv_with_expr_part2 DROP COLUMN gone;
ALTER TABLE width_skip_mcv_with_expr_part ATTACH PARTITION width_skip_mcv_with_expr_part2
  FOR VALUES FROM (51) TO (1000);
CREATE STATISTICS width_skip_mcv_with_expr_part_s (mcv)
  ON data_skipped, (length(expr_input)) FROM width_skip_mcv_with_expr_part;
INSERT INTO width_skip_mcv_with_expr_part SELECT * FROM width_skip_mcv_with_expr;

CREATE TABLE width_skip_mcv_with_expr_inh (old_col text, LIKE width_skip_mcv_with_expr,
                                           PRIMARY KEY (id));
ALTER TABLE width_skip_mcv_with_expr_inh DROP COLUMN old_col;
CREATE STATISTICS width_skip_mcv_with_expr_inh_s (mcv)
  ON data_skipped, (length(expr_input)) FROM width_skip_mcv_with_expr_inh;
CREATE TABLE width_skip_mcv_with_expr_inh1 (extra text, expr_input text, gone text,
                                            data_skipped text, id int PRIMARY KEY);
ALTER TABLE width_skip_mcv_with_expr_inh1 DROP COLUMN gone;
ALTER TABLE width_skip_mcv_with_expr_inh1 INHERIT width_skip_mcv_with_expr_inh;
INSERT INTO width_skip_mcv_with_expr_inh SELECT * FROM width_skip_mcv_with_expr WHERE id <= 50;
INSERT INTO width_skip_mcv_with_expr_inh1 (id, data_skipped, expr_input, extra)
SELECT *, wide_text('extra' || id, 70)                  -- 2240 B hex, i.e. 'cb86b12b.....b0d2'
FROM width_skip_mcv_with_expr WHERE id > 50;

-- width_skip_small_row: the child's extra column is short, so its rows stay
-- under 2 KB too
CREATE TABLE width_skip_small_row_part (old_col text, LIKE width_skip_small_row,
                                        PRIMARY KEY (id))
  PARTITION BY RANGE (id);
ALTER TABLE width_skip_small_row_part DROP COLUMN old_col;
CREATE TABLE width_skip_small_row_part1 PARTITION OF width_skip_small_row_part
  FOR VALUES FROM (1) TO (51);
CREATE TABLE width_skip_small_row_part2 (wide text, gone text, id int PRIMARY KEY);
ALTER TABLE width_skip_small_row_part2 DROP COLUMN gone;
ALTER TABLE width_skip_small_row_part ATTACH PARTITION width_skip_small_row_part2
  FOR VALUES FROM (51) TO (1000);
INSERT INTO width_skip_small_row_part SELECT * FROM width_skip_small_row;

CREATE TABLE width_skip_small_row_inh (old_col text, LIKE width_skip_small_row,
                                       PRIMARY KEY (id));
ALTER TABLE width_skip_small_row_inh DROP COLUMN old_col;
CREATE TABLE width_skip_small_row_inh1 (extra text, wide text, gone text, id int PRIMARY KEY);
ALTER TABLE width_skip_small_row_inh1 DROP COLUMN gone;
ALTER TABLE width_skip_small_row_inh1 INHERIT width_skip_small_row_inh;
INSERT INTO width_skip_small_row_inh SELECT * FROM width_skip_small_row WHERE id <= 50;
INSERT INTO width_skip_small_row_inh1 (id, wide, extra)
SELECT *, 'e' || id                                     -- 2-4 B text, i.e. 'e52'
FROM width_skip_small_row WHERE id > 50;

-- every statistic, one hash per pg_stats row
CREATE VIEW width_skip_stats AS
SELECT tablename, attname, inherited, avg_width,
       md5(row(avg_width, null_frac, n_distinct, most_common_vals, most_common_freqs,
               histogram_bounds, correlation, most_common_elems,
               most_common_elem_freqs, elem_count_histogram,
               range_length_histogram, range_empty_frac,
               range_bounds_histogram)::text) AS stats_md5
FROM pg_stats WHERE tablename LIKE 'width\_skip\_%';
CREATE VIEW width_skip_ext AS
SELECT tablename, statistics_name, inherited, md5(s::text) AS stats_md5
FROM pg_stats_ext s WHERE tablename LIKE 'width\_skip\_%';
CREATE VIEW width_skip_ext_exprs AS
SELECT tablename, statistics_name, expr, inherited, md5(s::text) AS stats_md5
FROM pg_stats_ext_exprs s WHERE tablename LIKE 'width\_skip\_%';

-- substitution on (the default)
SHOW yb_enable_analyze_width_skip;
ANALYZE width_skip_t;
ANALYZE width_skip_t_part;
ANALYZE width_skip_t_inh;
ANALYZE width_skip_t_inh1;
ANALYZE width_skip_t_part_of_part;
ANALYZE width_skip_row;
ANALYZE width_skip_row_part;
ANALYZE width_skip_row_inh;
ANALYZE width_skip_row_inh1;
ANALYZE width_skip_part;
ANALYZE width_skip_part1;
ANALYZE width_skip_dropped;
ANALYZE width_skip_cols (analyzed);
ANALYZE width_skip_cols_part (analyzed);
ANALYZE width_skip_cols_inh (analyzed);
ANALYZE width_skip_cols_inh1 (analyzed);
ANALYZE width_skip_set_statistics_0;
ANALYZE width_skip_set_statistics_0_part;
ANALYZE width_skip_set_statistics_0_inh;
ANALYZE width_skip_set_statistics_0_inh1;
ANALYZE width_skip_mcv_with_expr;
ANALYZE width_skip_mcv_with_expr_part;
ANALYZE width_skip_mcv_with_expr_inh;
ANALYZE width_skip_mcv_with_expr_inh1;
ANALYZE width_skip_small_row;
ANALYZE width_skip_small_row_part;
ANALYZE width_skip_small_row_inh;
ANALYZE width_skip_small_row_inh1;
CREATE TEMP TABLE ws_on AS SELECT * FROM width_skip_stats;
CREATE TEMP TABLE ws_ext_on AS SELECT * FROM width_skip_ext;
CREATE TEMP TABLE ws_ext_exprs_on AS SELECT * FROM width_skip_ext_exprs;

-- substitution off: values are kept as fetched
SET yb_enable_analyze_width_skip = off;
ANALYZE width_skip_t;
ANALYZE width_skip_t_part;
ANALYZE width_skip_t_inh;
ANALYZE width_skip_t_inh1;
ANALYZE width_skip_t_part_of_part;
ANALYZE width_skip_row;
ANALYZE width_skip_row_part;
ANALYZE width_skip_row_inh;
ANALYZE width_skip_row_inh1;
ANALYZE width_skip_part;
ANALYZE width_skip_part1;
ANALYZE width_skip_dropped;
ANALYZE width_skip_cols (analyzed);
ANALYZE width_skip_cols_part (analyzed);
ANALYZE width_skip_cols_inh (analyzed);
ANALYZE width_skip_cols_inh1 (analyzed);
ANALYZE width_skip_set_statistics_0;
ANALYZE width_skip_set_statistics_0_part;
ANALYZE width_skip_set_statistics_0_inh;
ANALYZE width_skip_set_statistics_0_inh1;
ANALYZE width_skip_mcv_with_expr;
ANALYZE width_skip_mcv_with_expr_part;
ANALYZE width_skip_mcv_with_expr_inh;
ANALYZE width_skip_mcv_with_expr_inh1;
ANALYZE width_skip_small_row;
ANALYZE width_skip_small_row_part;
ANALYZE width_skip_small_row_inh;
ANALYZE width_skip_small_row_inh1;
CREATE TEMP TABLE ws_off AS SELECT * FROM width_skip_stats;
CREATE TEMP TABLE ws_ext_off AS SELECT * FROM width_skip_ext;
CREATE TEMP TABLE ws_ext_exprs_off AS SELECT * FROM width_skip_ext_exprs;
RESET yb_enable_analyze_width_skip;

-- the widths a reader can check by eye: on and off must agree everywhere
-- (the _part and _inh copies are left out here; the checks below compare
-- all of their statistics)
SELECT tablename, attname, inherited,
       o.avg_width AS avg_width_on, f.avg_width AS avg_width_off,
       n_distinct, null_frac
FROM ws_on o JOIN ws_off f USING (tablename, attname, inherited)
     JOIN pg_stats s USING (tablename, attname, inherited)
WHERE tablename NOT LIKE 'width\_skip\_%\_part%'
  AND tablename NOT LIKE 'width\_skip\_%\_inh%'
ORDER BY tablename, attname, inherited;
SELECT attname, most_common_vals, most_common_freqs
FROM pg_stats WHERE tablename = 'width_skip_t' AND attname IN ('small', 'straddle', 't_mcv')
ORDER BY attname;

-- no other statistic may differ (the two hash sets must be the same set)
SELECT count(*) AS rows_on FROM ws_on;
SELECT count(*) AS rows_off FROM ws_off;
(SELECT tablename, attname, inherited, stats_md5 FROM ws_on
 EXCEPT SELECT tablename, attname, inherited, stats_md5 FROM ws_off)
UNION ALL
(SELECT tablename, attname, inherited, stats_md5 FROM ws_off
 EXCEPT SELECT tablename, attname, inherited, stats_md5 FROM ws_on)
ORDER BY 1, 2, 3;
SELECT count(*) AS ext_rows_on FROM ws_ext_on;
(SELECT * FROM ws_ext_on EXCEPT SELECT * FROM ws_ext_off)
UNION ALL
(SELECT * FROM ws_ext_off EXCEPT SELECT * FROM ws_ext_on)
ORDER BY 1, 2, 3;
SELECT count(*) AS ext_expr_rows_on FROM ws_ext_exprs_on;
(SELECT * FROM ws_ext_exprs_on EXCEPT SELECT * FROM ws_ext_exprs_off)
UNION ALL
(SELECT * FROM ws_ext_exprs_off EXCEPT SELECT * FROM ws_ext_exprs_on)
ORDER BY 1, 2, 3;

DROP VIEW width_skip_stats;
DROP VIEW width_skip_ext;
DROP VIEW width_skip_ext_exprs;
DROP INDEX width_skip_row_ix;
DROP INDEX width_skip_row_part1_ix;
DROP INDEX width_skip_row_part2_ix;
DROP INDEX width_skip_row_inh_ix;
DROP INDEX width_skip_row_inh1_ix;
DROP FUNCTION width_skip_row_key(width_skip_row),
  width_skip_row_part1_key(width_skip_row_part1),
  width_skip_row_part2_key(width_skip_row_part2),
  width_skip_row_inh_key(width_skip_row_inh),
  width_skip_row_inh1_key(width_skip_row_inh1);
DROP TABLE width_skip_t, width_skip_row, width_skip_part, width_skip_dropped,
  width_skip_cols, width_skip_set_statistics_0, width_skip_mcv_with_expr,
  width_skip_small_row;
DROP TABLE width_skip_t_part, width_skip_t_inh, width_skip_t_inh1, width_skip_t_part_of_part,
  width_skip_row_part, width_skip_row_inh, width_skip_row_inh1,
  width_skip_cols_part, width_skip_cols_inh, width_skip_cols_inh1,
  width_skip_set_statistics_0_part, width_skip_set_statistics_0_inh,
  width_skip_set_statistics_0_inh1,
  width_skip_mcv_with_expr_part, width_skip_mcv_with_expr_inh,
  width_skip_mcv_with_expr_inh1,
  width_skip_small_row_part, width_skip_small_row_inh, width_skip_small_row_inh1;
DROP TYPE textrange;
DROP DOMAIN wide_dom;
DROP FUNCTION wide_text(text, int);

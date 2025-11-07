--
-- MATERIALIZED VIEWS
-- Derived from Postgres test/regress/sql/matview.sql.
--

-- Base table and seed data
CREATE TABLE mvtest_t (
  id   int PRIMARY KEY,
  type text NOT NULL,
  amt  numeric NOT NULL
);
INSERT INTO mvtest_t VALUES
  (1, 'x', 2),
  (2, 'x', 3),
  (3, 'y', 5),
  (4, 'y', 7),
  (5, 'z', 11);

-- A regular view used as a source for an MV
CREATE VIEW mvtest_tv AS
  SELECT type, sum(amt) AS totamt FROM mvtest_t GROUP BY type;

-- MV on base table, start unpopulated then refresh
CREATE MATERIALIZED VIEW mvtest_tm AS
  SELECT type, sum(amt) AS totamt FROM mvtest_t GROUP BY type WITH NO DATA;
CREATE UNIQUE INDEX mvtest_tm_type ON mvtest_tm(type);
REFRESH MATERIALIZED VIEW mvtest_tm;

-- MV based on a view
CREATE MATERIALIZED VIEW mvtest_tvm AS
  SELECT * FROM mvtest_tv ORDER BY type;

-- Nested MV (aggregate of another MV)
CREATE MATERIALIZED VIEW mvtest_tvmm AS
  SELECT sum(totamt) AS grandtot FROM mvtest_tvm;

ALTER MATERIALIZED VIEW mvtest_tvm RENAME TO mvtest_tvm_renamed;

-- Change base table and refresh MVs to update contents
INSERT INTO mvtest_t VALUES (6, 'z', 13);
REFRESH MATERIALIZED VIEW mvtest_tm;
REFRESH MATERIALIZED VIEW mvtest_tvm_renamed;
REFRESH MATERIALIZED VIEW mvtest_tvmm;

--
-- Part of yb.port.interval modified to use YB table instead of temp table.
--

SET DATESTYLE = 'ISO';
SET IntervalStyle to postgres;

-- Test intervals that are large enough to overflow 64 bits in comparisons
CREATE TABLE INTERVAL_TBL_OF (f1 interval); -- YB: avoid TEMP to exercise YB table
INSERT INTO INTERVAL_TBL_OF (f1) VALUES
  ('2147483647 days 2147483647 months'),
  ('2147483647 days -2147483648 months'),
  ('1 year'),
  ('-2147483648 days 2147483647 months'),
  ('-2147483648 days -2147483648 months');

SELECT r1.*, r2.*
   FROM INTERVAL_TBL_OF r1, INTERVAL_TBL_OF r2
   WHERE r1.f1 > r2.f1
   ORDER BY r1.f1, r2.f1;

CREATE INDEX ON INTERVAL_TBL_OF USING btree (f1);
SET enable_seqscan TO false;
EXPLAIN (COSTS OFF)
SELECT f1 FROM INTERVAL_TBL_OF r1 ORDER BY f1;
SELECT f1 FROM INTERVAL_TBL_OF r1 ORDER BY f1;
RESET enable_seqscan;

DROP TABLE INTERVAL_TBL_OF;

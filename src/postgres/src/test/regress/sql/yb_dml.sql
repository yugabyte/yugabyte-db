--
-- Yugabyte-owned file for testing DML queries that do not fall into more
-- specific buckets (yb_dml_*.sql).
--

-- GH-22967: Test that a Value Scan fed by a Sub Plan does not cause an INSERT
-- to crash. The query relies on the fact the EXISTS .. IN expression does not
-- get constant-folded and is evaluated in its own Sub Plan.
CREATE TABLE GH_22967 (k INT4 PRIMARY KEY);
EXPLAIN (COSTS OFF) INSERT INTO GH_22967 VALUES ((EXISTS(SELECT 1) in (SELECT true))::INT4), (-10);
INSERT INTO GH_22967 VALUES ((EXISTS(SELECT 1) in (SELECT true))::INT4), (-10);

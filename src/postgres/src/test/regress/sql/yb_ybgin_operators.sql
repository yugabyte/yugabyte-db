--
-- Yugabyte-owned test for covering ybgin operators.  The yb_ybgin test has
-- complete coverage for tsvector and anyarray types, so this will focus on
-- jsonb.  Operators taken from
-- <https://www.postgresql.org/docs/current/functions-json.html>.
--

-- Always choose index scan.
SET enable_seqscan = off;
SET yb_test_ybgin_disable_cost_factor = 0.5;

--
-- jsonb_ops
--

-- Setup
INSERT INTO jsonbs (j) VALUES ('{"aaa":[-1,2.5,"5"], "date":"2021-06-30"}');
DROP INDEX jsonbs_j_idx;
CREATE INDEX ON jsonbs USING ybgin (j);
-- jsonpath: number + number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ + 2 == 4)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ + 2 == 4)';
-- jsonpath: + number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (+@ == 5)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (+@ == 5)';
-- jsonpath: number - number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ - 2 == 3)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ - 2 == 3)';
-- jsonpath: - number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa.bbb[*] ? (-@ < -3)';
SELECT * FROM jsonbs WHERE j @? '$.aaa.bbb[*] ? (-@ < -3)';
-- jsonpath: number * number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ * 2 == 4)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ * 2 == 4)';
-- jsonpath: number / number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ / 2 == 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ / 2 == 1)';
-- jsonpath: number % number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ % 2 == 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ % 2 == 1)';
-- jsonpath: value . type()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$[*] ? (@.type() == "string")';
SELECT * FROM jsonbs WHERE j @? '$[*] ? (@.type() == "string")';
-- jsonpath: value . size()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @@ '$.aaa.size() == 3';
SELECT * FROM jsonbs WHERE j @@ '$.aaa.size() == 3';
-- jsonpath: value . double()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @@ '$.double() * 3 == 9';
SELECT * FROM jsonbs WHERE j @@ '$.double() * 3 == 9';
-- jsonpath: value . ceiling()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.ceiling() == 2)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.ceiling() == 2)';
-- jsonpath: value . floor()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.floor() == 2)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.floor() == 2)';
-- jsonpath: value . abs()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.abs() == 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.abs() == 1)';
-- jsonpath: value . datetime()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @@ '$.date.datetime() < "2021-07-01".datetime()';
SELECT * FROM jsonbs WHERE j @@ '$.date.datetime() < "2021-07-01".datetime()';
-- jsonpath: value . datetime(template)
-- (skip this)
-- jsonpath: object . keyvalue()
-- (skip this)
-- jsonpath: value == value
-- (skip this)
-- jsonpath: value != value
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != 1)';
-- jsonpath: value < value
-- (skip this)
-- jsonpath: value <= value
-- (skip this)
-- jsonpath: value > value
-- (skip this)
-- jsonpath: value >= value
-- (skip this)
-- jsonpath: boolean && boolean
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != -1 && @ != 2.5)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != -1 && @ != 2.5)';
-- jsonpath: boolean || boolean
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == -1 || @ == 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == -1 || @ == 1)';
-- jsonpath: ! boolean
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (!(@ > 0))';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (!(@ > 0))';
-- jsonpath: boolean is unknown
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? ((@ == "1") is unknown)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? ((@ == "1") is unknown)';
-- jsonpath: string like_regex string
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ like_regex "^[4-7]+$")';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ like_regex "^[4-7]+$")';
-- jsonpath: string starts with string
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ starts with "5")';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ starts with "5")';
-- jsonpath: exists ( path_expression )
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.* ? (exists (@ ? (@[*] < 0 || @[*] > 5)))';
SELECT * FROM jsonbs WHERE j @? '$.* ? (exists (@ ? (@[*] < 0 || @[*] > 5)))';

--
-- jsonb_path_ops
--

-- Setup
DROP INDEX jsonbs_j_idx;
CREATE INDEX ON jsonbs USING ybgin (j jsonb_path_ops);
-- jsonpath: number + number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ + 2 == 4)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ + 2 == 4)';
-- jsonpath: + number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (+@ == 5)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (+@ == 5)';
-- jsonpath: number - number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ - 2 == 3)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ - 2 == 3)';
-- jsonpath: - number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa.bbb[*] ? (-@ < -3)';
SELECT * FROM jsonbs WHERE j @? '$.aaa.bbb[*] ? (-@ < -3)';
-- jsonpath: number * number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ * 2 == 4)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ * 2 == 4)';
-- jsonpath: number / number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ / 2 == 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ / 2 == 1)';
-- jsonpath: number % number
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ % 2 == 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ % 2 == 1)';
-- jsonpath: value . type()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$[*] ? (@.type() == "string")';
SELECT * FROM jsonbs WHERE j @? '$[*] ? (@.type() == "string")';
-- jsonpath: value . size()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @@ '$.aaa.size() == 3';
SELECT * FROM jsonbs WHERE j @@ '$.aaa.size() == 3';
-- jsonpath: value . double()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @@ '$.double() * 3 == 9';
SELECT * FROM jsonbs WHERE j @@ '$.double() * 3 == 9';
-- jsonpath: value . ceiling()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.ceiling() == 2)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.ceiling() == 2)';
-- jsonpath: value . floor()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.floor() == 2)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.floor() == 2)';
-- jsonpath: value . abs()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.abs() == 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.abs() == 1)';
-- jsonpath: value . datetime()
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @@ '$.date.datetime() < "2021-07-01".datetime()';
SELECT * FROM jsonbs WHERE j @@ '$.date.datetime() < "2021-07-01".datetime()';
-- jsonpath: value . datetime(template)
-- (skip this)
-- jsonpath: object . keyvalue()
-- (skip this)
-- jsonpath: value == value
-- (skip this)
-- jsonpath: value != value
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != 1)';
-- jsonpath: value < value
-- (skip this)
-- jsonpath: value <= value
-- (skip this)
-- jsonpath: value > value
-- (skip this)
-- jsonpath: value >= value
-- (skip this)
-- jsonpath: boolean && boolean
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != -1 && @ != 2.5)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != -1 && @ != 2.5)';
-- jsonpath: boolean || boolean
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == -1 || @ == 1)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == -1 || @ == 1)';
-- jsonpath: ! boolean
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (!(@ > 0))';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (!(@ > 0))';
-- jsonpath: boolean is unknown
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ is unknown)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ is unknown)';
-- jsonpath: string like_regex string
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ like_regex "^[4-7]+$")';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ like_regex "^[4-7]+$")';
-- jsonpath: string starts with string
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ starts with "5")';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ starts with "5")';
-- jsonpath: exists ( path_expression )
EXPLAIN (costs off)
SELECT * FROM jsonbs WHERE j @? '$.* ? (exists (@ ? (@[*] < 0 || @[*] > 5)))';
SELECT * FROM jsonbs WHERE j @? '$.* ? (exists (@ ? (@[*] < 0 || @[*] > 5)))';

--
-- Yugabyte-owned test for covering ybgin operators.  The yb.orig.ybgin test has
-- complete coverage for tsvector and anyarray types, so this will focus on
-- jsonb.  Operators taken from
-- <https://www.postgresql.org/docs/current/functions-json.html>.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (costs off)'

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
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ + 2 == 4)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: + number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (+@ == 5)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: number - number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ - 2 == 3)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: - number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa.bbb[*] ? (-@ < -3)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: number * number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ * 2 == 4)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: number / number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ / 2 == 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: number % number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ % 2 == 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . type()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$[*] ? (@.type() == "string")';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . size()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @@ '$.aaa.size() == 3';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . double()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @@ '$.double() * 3 == 9';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . ceiling()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.ceiling() == 2)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . floor()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.floor() == 2)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . abs()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.abs() == 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . datetime()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @@ '$.date.datetime() < "2021-07-01".datetime()';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . datetime(template)
-- (skip this)
-- jsonpath: object . keyvalue()
-- (skip this)
-- jsonpath: value == value
-- (skip this)
-- jsonpath: value != value
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value < value
-- (skip this)
-- jsonpath: value <= value
-- (skip this)
-- jsonpath: value > value
-- (skip this)
-- jsonpath: value >= value
-- (skip this)
-- jsonpath: boolean && boolean
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != -1 && @ != 2.5)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: boolean || boolean
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == -1 || @ == 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: ! boolean
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (!(@ > 0))';
$$ AS query \gset
\i :iter_P2
-- jsonpath: boolean is unknown
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? ((@ == "1") is unknown)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: string like_regex string
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ like_regex "^[4-7]+$")';
$$ AS query \gset
\i :iter_P2
-- jsonpath: string starts with string
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ starts with "5")';
$$ AS query \gset
\i :iter_P2
-- jsonpath: exists ( path_expression )
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.* ? (exists (@ ? (@[*] < 0 || @[*] > 5)))';
$$ AS query \gset
\i :iter_P2

--
-- jsonb_path_ops
--

-- Setup
DROP INDEX jsonbs_j_idx;
CREATE INDEX ON jsonbs USING ybgin (j jsonb_path_ops);
-- jsonpath: number + number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ + 2 == 4)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: + number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (+@ == 5)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: number - number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ - 2 == 3)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: - number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa.bbb[*] ? (-@ < -3)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: number * number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ * 2 == 4)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: number / number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ / 2 == 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: number % number
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ % 2 == 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . type()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$[*] ? (@.type() == "string")';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . size()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @@ '$.aaa.size() == 3';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . double()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @@ '$.double() * 3 == 9';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . ceiling()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.ceiling() == 2)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . floor()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.floor() == 2)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . abs()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@.abs() == 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . datetime()
SELECT $$
:P SELECT * FROM jsonbs WHERE j @@ '$.date.datetime() < "2021-07-01".datetime()';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value . datetime(template)
-- (skip this)
-- jsonpath: object . keyvalue()
-- (skip this)
-- jsonpath: value == value
-- (skip this)
-- jsonpath: value != value
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: value < value
-- (skip this)
-- jsonpath: value <= value
-- (skip this)
-- jsonpath: value > value
-- (skip this)
-- jsonpath: value >= value
-- (skip this)
-- jsonpath: boolean && boolean
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ != -1 && @ != 2.5)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: boolean || boolean
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == -1 || @ == 1)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: ! boolean
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (!(@ > 0))';
$$ AS query \gset
\i :iter_P2
-- jsonpath: boolean is unknown
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ is unknown)';
$$ AS query \gset
\i :iter_P2
-- jsonpath: string like_regex string
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ like_regex "^[4-7]+$")';
$$ AS query \gset
\i :iter_P2
-- jsonpath: string starts with string
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ starts with "5")';
$$ AS query \gset
\i :iter_P2
-- jsonpath: exists ( path_expression )
SELECT $$
:P SELECT * FROM jsonbs WHERE j @? '$.* ? (exists (@ ? (@[*] < 0 || @[*] > 5)))';
$$ AS query \gset
\i :iter_P2

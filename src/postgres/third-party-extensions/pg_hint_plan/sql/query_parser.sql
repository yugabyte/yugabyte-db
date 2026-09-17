-- Tests for the query parser

LOAD 'pg_hint_plan';
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET pg_hint_plan.enable_hint TO on;

-- Dollar-based constants.
SELECT $$fox$$ AS constant;
SELECT $SomeTag$s fox$SomeTag$ AS constant;
-- Dollar-based constants with quotes embedded.
SELECT $$Mike's fox$$ AS constant;
SELECT $SomeTag$Mike's fox$SomeTag$ AS constant;
SELECT $$Mike"s fox$$ AS constant;
SELECT $SomeTag$Mike"s fox$SomeTag$ AS constant;

CREATE TABLE hint_parser_tab (a int, b int);
CREATE INDEX hint_parser_ind ON hint_parser_tab(a);
INSERT INTO hint_parser_tab VALUES (generate_series(1,200), generate_series(1,200));

EXPLAIN SELECT /*+ IndexScan (hint_parser_tab hint_parser_ind) */ b
  FROM hint_parser_tab WHERE a = 108;
SELECT /*+ IndexScan (hint_parser_tab hint_parser_ind) */ b
  FROM hint_parser_tab WHERE a = 108;

-- Dollar-quoted constant with hint embedded.
-- IndexScan used, with SeqScan ignored because it is a query constant.
EXPLAIN SELECT /*+ IndexScan (hint_parser_tab hint_parser_ind) */ b,
  $$/*+ SeqScan (hint_parser_tab) */$$ AS hint_ignored
  FROM hint_parser_tab WHERE a = 108;
-- Different dollar-based pattern.
-- SeqScan used, with IndexScan ignored because it is a query constant.
EXPLAIN SELECT /*+ SeqScan (hint_parser_tab) */ b,
  $SomeTag$/*+ IndexScan (hint_parser_tab hint_parser_ind */$SomeTag$ AS hint_ignored
  FROM hint_parser_tab WHERE a = 108;

DROP TABLE hint_parser_tab;

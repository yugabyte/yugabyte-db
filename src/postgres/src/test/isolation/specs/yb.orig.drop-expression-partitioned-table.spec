setup
{
  CREATE TABLE part (
      amount NUMERIC,
      double_amount NUMERIC GENERATED ALWAYS AS (amount * 2) STORED
  ) partition by range(amount);
  CREATE TABLE part_1_100 partition of part for values from (1) to (100);
  CREATE TABLE part_2_200 partition of part for values from (101) to (200);
  INSERT INTO part VALUES (1), (101);
}

teardown
{
  DROP TABLE part;
}

session "s1"
setup			{ BEGIN; }
step "s1_insert"		{ INSERT INTO part VALUES (10); }
step "s1_commit"		{ COMMIT; }

session "s2"
step "s2_drop_expression"		{ ALTER TABLE part ALTER COLUMN double_amount DROP EXPRESSION; }

permutation "s1_insert" "s2_drop_expression" "s1_commit"

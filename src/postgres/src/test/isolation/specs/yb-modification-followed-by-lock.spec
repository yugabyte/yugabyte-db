setup
{
  CREATE TABLE queue (
    id	int		PRIMARY KEY,
    data			text	NOT NULL,
    status			text	NOT NULL
  );
  INSERT INTO queue VALUES (1, 'foo', 'NEW'), (2, 'bar', 'NEW');
}

teardown
{
  DROP TABLE queue;
}

session "s1"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1a"	{ SELECT * FROM queue; -- this is just to ensure we have picked the read point}
step "s1b"	{ SELECT * FROM queue ORDER BY id FOR UPDATE LIMIT 1; }
step "s1c"	{ COMMIT; }


session "s2"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2a"	{ SELECT * FROM queue; -- this is just to ensure we have picked the read point}
step "s2b"	{ UPDATE queue set status='OLD' WHERE id=1; }
step "s2c"	{ COMMIT; }

# This test is to ensure that the UPDATE doesn't conflict with the row lock that was taken by the
# committed transaction.
permutation "s1a" "s2a" "s1b" "s1c" "s2b" "s2c"

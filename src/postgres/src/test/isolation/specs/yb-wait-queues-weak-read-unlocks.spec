setup
{
  DROP TABLE IF EXISTS foo;
  CREATE TABLE foo (
    k	int		PRIMARY KEY,
    v	int 	NOT NULL
  );

  INSERT INTO foo SELECT generate_series(1, 10), 0;
}

teardown
{
  DROP TABLE foo;
}

session "s1"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1u1" { select * from foo order by k asc; }
step "s1u2" { select * from foo where k=1 for key share; }
step "s1c"	{ COMMIT; }

session "s2"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2u1" { select * from foo order by k asc; }
step "s2u2" { select * from foo where k=1 for update; }
step "s2c"	{ COMMIT; }

permutation "s1u1" "s2u1" "s1u2" "s2u2" "s1c" "s2c"
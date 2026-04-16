setup
{
  DROP TABLE IF EXISTS foo;
  CREATE TABLE foo (
    k	int		PRIMARY KEY,
    v	int 	NOT NULL
  );

  INSERT INTO foo SELECT generate_series(1, 100), 0;
}

teardown
{
  DROP TABLE foo;
}

session "s1"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1u1" { select * from foo where k = 1 for share; }
step "s1u2" { select * from foo where k=1 for update; }
step "s1c"	{ COMMIT; }

session "s2"
step "s2u"		{ update foo set v=10 where k=1; }

permutation "s1u1" "s2u" "s1u2" "s1c"
permutation "s1u2" "s2u" "s1c"
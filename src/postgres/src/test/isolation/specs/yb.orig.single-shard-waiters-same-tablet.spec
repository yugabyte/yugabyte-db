setup
{
  DROP TABLE IF EXISTS foo;
  CREATE TABLE foo (
    k	int		PRIMARY KEY,
    v	int 	NOT NULL
  ) SPLIT INTO 1 TABLETS;

  INSERT INTO foo SELECT generate_series(1, 100), 0;
}

teardown
{
  DROP TABLE foo;
}

session "s1"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1u1" { SELECT * FROM foo WHERE k = 1 FOR UPDATE; }
step "s1u2" { SELECT * FROM foo WHERE k = 2 FOR UPDATE; }
step "s1c"	{ COMMIT; }

session "s2"
step "s2u"		{ update foo set v=10 where k=1; }

session "s3"
step "s3u"		{ update foo set v=10 where k=2; }

permutation "s1u1" "s2u" "s1u2" "s3u" "s1c"
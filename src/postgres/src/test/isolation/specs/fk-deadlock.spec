setup
{
  CREATE TABLE parent (
	parent_key	int		PRIMARY KEY,
	aux			text	NOT NULL
  );

  CREATE TABLE child (
	child_key	int		PRIMARY KEY,
	parent_key	int		NOT NULL REFERENCES parent
  );

  INSERT INTO parent VALUES (1, 'foo');
}

teardown
{
  DROP TABLE parent, child;
}

session s1
setup		{ BEGIN; SET deadlock_timeout = '100ms'; }
step s1i	{ INSERT INTO child VALUES (1, 1); }
step s1u	{ UPDATE parent SET aux = 'bar'; }
step s1c	{ COMMIT; }

session s2
setup		{ BEGIN; SET deadlock_timeout = '10s'; }
step s2i	{ INSERT INTO child VALUES (2, 1); }
step s2u	{ UPDATE parent SET aux = 'baz'; }
step s2c	{ COMMIT; }

permutation s1i s1u s1c s2i s2u s2c
permutation s1i s1u s2i s1c s2u s2c
permutation s1i s1u s2i s2u s1c s2c
permutation s1i s2i s1u s1c s2u s2c
permutation s1i s2i s1u s2u s1c s2c
permutation s1i s2i s2u s1u s2c s1c
permutation s1i s2i s2u s2c s1u s1c
permutation s2i s1i s1u s1c s2u s2c
permutation s2i s1i s1u s2u s1c s2c
permutation s2i s1i s2u s1u s2c s1c
permutation s2i s1i s2u s2c s1u s1c
permutation s2i s2u s1i s1u s2c s1c
permutation s2i s2u s1i s2c s1u s1c
permutation s2i s2u s2c s1i s1u s1c

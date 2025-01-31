# ALTER TABLE - Add and Validate constraint with concurrent writes
#
# VALIDATE allows a minimum of ShareUpdateExclusiveLock
# so we mix reads with it to see what works or waits

setup
{
 CREATE TABLE a (i int PRIMARY KEY);
 CREATE TABLE b (a_id int);
 INSERT INTO a VALUES (0), (1), (2), (3);
 INSERT INTO b SELECT generate_series(1,1000) % 4;
}

teardown
{
 DROP TABLE a, b;
}

session s1
step s1		{ BEGIN; }
step at1	{ ALTER TABLE b ADD CONSTRAINT bfk FOREIGN KEY (a_id) REFERENCES a (i) NOT VALID; }
step sc1	{ COMMIT; }
step s2		{ BEGIN; }
step at2	{ ALTER TABLE b VALIDATE CONSTRAINT bfk; }
step sc2	{ COMMIT; }

session s2
setup		{ BEGIN; }
step rx1	{ SELECT * FROM b WHERE a_id = 1 LIMIT 1; }
step wx		{ INSERT INTO b VALUES (0); }
step rx3	{ SELECT * FROM b WHERE a_id = 3 LIMIT 3; }
step c2		{ COMMIT; }

permutation s1 at1 sc1 s2 at2 sc2 rx1 wx rx3 c2
permutation s1 at1 sc1 s2 at2 rx1 sc2 wx rx3 c2
permutation s1 at1 sc1 s2 at2 rx1 wx sc2 rx3 c2
permutation s1 at1 sc1 s2 at2 rx1 wx rx3 sc2 c2
permutation s1 at1 sc1 s2 at2 rx1 wx rx3 c2 sc2
permutation s1 at1 sc1 s2 rx1 at2 sc2 wx rx3 c2
permutation s1 at1 sc1 s2 rx1 at2 wx sc2 rx3 c2
permutation s1 at1 sc1 s2 rx1 at2 wx rx3 sc2 c2
permutation s1 at1 sc1 s2 rx1 at2 wx rx3 c2 sc2
permutation s1 at1 sc1 s2 rx1 wx at2 sc2 rx3 c2
permutation s1 at1 sc1 s2 rx1 wx at2 rx3 sc2 c2
permutation s1 at1 sc1 s2 rx1 wx at2 rx3 c2 sc2
permutation s1 at1 sc1 s2 rx1 wx rx3 at2 sc2 c2
permutation s1 at1 sc1 s2 rx1 wx rx3 at2 c2 sc2
permutation s1 at1 sc1 s2 rx1 wx rx3 c2 at2 sc2
permutation s1 at1 sc1 rx1 s2 at2 sc2 wx rx3 c2
permutation s1 at1 sc1 rx1 s2 at2 wx sc2 rx3 c2
permutation s1 at1 sc1 rx1 s2 at2 wx rx3 sc2 c2
permutation s1 at1 sc1 rx1 s2 at2 wx rx3 c2 sc2
permutation s1 at1 sc1 rx1 s2 wx at2 sc2 rx3 c2
permutation s1 at1 sc1 rx1 s2 wx at2 rx3 sc2 c2
permutation s1 at1 sc1 rx1 s2 wx at2 rx3 c2 sc2
permutation s1 at1 sc1 rx1 s2 wx rx3 at2 sc2 c2
permutation s1 at1 sc1 rx1 s2 wx rx3 at2 c2 sc2
permutation s1 at1 sc1 rx1 s2 wx rx3 c2 at2 sc2
permutation s1 at1 sc1 rx1 wx s2 at2 sc2 rx3 c2
permutation s1 at1 sc1 rx1 wx s2 at2 rx3 sc2 c2
permutation s1 at1 sc1 rx1 wx s2 at2 rx3 c2 sc2
permutation s1 at1 sc1 rx1 wx s2 rx3 at2 sc2 c2
permutation s1 at1 sc1 rx1 wx s2 rx3 at2 c2 sc2
permutation s1 at1 sc1 rx1 wx s2 rx3 c2 at2 sc2
permutation s1 at1 sc1 rx1 wx rx3 s2 at2 sc2 c2
permutation s1 at1 sc1 rx1 wx rx3 s2 at2 c2 sc2
permutation s1 at1 sc1 rx1 wx rx3 s2 c2 at2 sc2
permutation s1 at1 sc1 rx1 wx rx3 c2 s2 at2 sc2
permutation s1 at1 rx1 sc1 s2 at2 sc2 wx rx3 c2
permutation s1 at1 rx1 sc1 s2 at2 wx sc2 rx3 c2
permutation s1 at1 rx1 sc1 s2 at2 wx rx3 sc2 c2
permutation s1 at1 rx1 sc1 s2 at2 wx rx3 c2 sc2
permutation s1 at1 rx1 sc1 s2 wx at2 sc2 rx3 c2
permutation s1 at1 rx1 sc1 s2 wx at2 rx3 sc2 c2
permutation s1 at1 rx1 sc1 s2 wx at2 rx3 c2 sc2
permutation s1 at1 rx1 sc1 s2 wx rx3 at2 sc2 c2
permutation s1 at1 rx1 sc1 s2 wx rx3 at2 c2 sc2
permutation s1 at1 rx1 sc1 s2 wx rx3 c2 at2 sc2
permutation s1 at1 rx1 sc1 wx s2 at2 sc2 rx3 c2
permutation s1 at1 rx1 sc1 wx s2 at2 rx3 sc2 c2
permutation s1 at1 rx1 sc1 wx s2 at2 rx3 c2 sc2
permutation s1 at1 rx1 sc1 wx s2 rx3 at2 sc2 c2
permutation s1 at1 rx1 sc1 wx s2 rx3 at2 c2 sc2
permutation s1 at1 rx1 sc1 wx s2 rx3 c2 at2 sc2
permutation s1 at1 rx1 sc1 wx rx3 s2 at2 sc2 c2
permutation s1 at1 rx1 sc1 wx rx3 s2 at2 c2 sc2
permutation s1 at1 rx1 sc1 wx rx3 s2 c2 at2 sc2
permutation s1 at1 rx1 sc1 wx rx3 c2 s2 at2 sc2
permutation s1 at1 rx1 wx sc1 s2 at2 sc2 rx3 c2
permutation s1 at1 rx1 wx sc1 s2 at2 rx3 sc2 c2
permutation s1 at1 rx1 wx sc1 s2 at2 rx3 c2 sc2
permutation s1 at1 rx1 wx sc1 s2 rx3 at2 sc2 c2
permutation s1 at1 rx1 wx sc1 s2 rx3 at2 c2 sc2
permutation s1 at1 rx1 wx sc1 s2 rx3 c2 at2 sc2
permutation s1 at1 rx1 wx sc1 rx3 s2 at2 sc2 c2
permutation s1 at1 rx1 wx sc1 rx3 s2 at2 c2 sc2
permutation s1 at1 rx1 wx sc1 rx3 s2 c2 at2 sc2
permutation s1 at1 rx1 wx sc1 rx3 c2 s2 at2 sc2
permutation s1 rx1 at1 sc1 s2 at2 sc2 wx rx3 c2
permutation s1 rx1 at1 sc1 s2 at2 wx sc2 rx3 c2
permutation s1 rx1 at1 sc1 s2 at2 wx rx3 sc2 c2
permutation s1 rx1 at1 sc1 s2 at2 wx rx3 c2 sc2
permutation s1 rx1 at1 sc1 s2 wx at2 sc2 rx3 c2
permutation s1 rx1 at1 sc1 s2 wx at2 rx3 sc2 c2
permutation s1 rx1 at1 sc1 s2 wx at2 rx3 c2 sc2
permutation s1 rx1 at1 sc1 s2 wx rx3 at2 sc2 c2
permutation s1 rx1 at1 sc1 s2 wx rx3 at2 c2 sc2
permutation s1 rx1 at1 sc1 s2 wx rx3 c2 at2 sc2
permutation s1 rx1 at1 sc1 wx s2 at2 sc2 rx3 c2
permutation s1 rx1 at1 sc1 wx s2 at2 rx3 sc2 c2
permutation s1 rx1 at1 sc1 wx s2 at2 rx3 c2 sc2
permutation s1 rx1 at1 sc1 wx s2 rx3 at2 sc2 c2
permutation s1 rx1 at1 sc1 wx s2 rx3 at2 c2 sc2
permutation s1 rx1 at1 sc1 wx s2 rx3 c2 at2 sc2
permutation s1 rx1 at1 sc1 wx rx3 s2 at2 sc2 c2
permutation s1 rx1 at1 sc1 wx rx3 s2 at2 c2 sc2
permutation s1 rx1 at1 sc1 wx rx3 s2 c2 at2 sc2
permutation s1 rx1 at1 sc1 wx rx3 c2 s2 at2 sc2
permutation s1 rx1 at1 wx sc1 s2 at2 sc2 rx3 c2
permutation s1 rx1 at1 wx sc1 s2 at2 rx3 sc2 c2
permutation s1 rx1 at1 wx sc1 s2 at2 rx3 c2 sc2
permutation s1 rx1 at1 wx sc1 s2 rx3 at2 sc2 c2
permutation s1 rx1 at1 wx sc1 s2 rx3 at2 c2 sc2
permutation s1 rx1 at1 wx sc1 s2 rx3 c2 at2 sc2
permutation s1 rx1 at1 wx sc1 rx3 s2 at2 sc2 c2
permutation s1 rx1 at1 wx sc1 rx3 s2 at2 c2 sc2
permutation s1 rx1 at1 wx sc1 rx3 s2 c2 at2 sc2
permutation s1 rx1 at1 wx sc1 rx3 c2 s2 at2 sc2
permutation s1 rx1 wx at1 rx3 c2 sc1 s2 at2 sc2
permutation s1 rx1 wx rx3 at1 c2 sc1 s2 at2 sc2
permutation s1 rx1 wx rx3 c2 at1 sc1 s2 at2 sc2
permutation rx1 s1 at1 sc1 s2 at2 sc2 wx rx3 c2
permutation rx1 s1 at1 sc1 s2 at2 wx sc2 rx3 c2
permutation rx1 s1 at1 sc1 s2 at2 wx rx3 sc2 c2
permutation rx1 s1 at1 sc1 s2 at2 wx rx3 c2 sc2
permutation rx1 s1 at1 sc1 s2 wx at2 sc2 rx3 c2
permutation rx1 s1 at1 sc1 s2 wx at2 rx3 sc2 c2
permutation rx1 s1 at1 sc1 s2 wx at2 rx3 c2 sc2
permutation rx1 s1 at1 sc1 s2 wx rx3 at2 sc2 c2
permutation rx1 s1 at1 sc1 s2 wx rx3 at2 c2 sc2
permutation rx1 s1 at1 sc1 s2 wx rx3 c2 at2 sc2
permutation rx1 s1 at1 sc1 wx s2 at2 sc2 rx3 c2
permutation rx1 s1 at1 sc1 wx s2 at2 rx3 sc2 c2
permutation rx1 s1 at1 sc1 wx s2 at2 rx3 c2 sc2
permutation rx1 s1 at1 sc1 wx s2 rx3 at2 sc2 c2
permutation rx1 s1 at1 sc1 wx s2 rx3 at2 c2 sc2
permutation rx1 s1 at1 sc1 wx s2 rx3 c2 at2 sc2
permutation rx1 s1 at1 sc1 wx rx3 s2 at2 sc2 c2
permutation rx1 s1 at1 sc1 wx rx3 s2 at2 c2 sc2
permutation rx1 s1 at1 sc1 wx rx3 s2 c2 at2 sc2
permutation rx1 s1 at1 sc1 wx rx3 c2 s2 at2 sc2
permutation rx1 s1 at1 wx sc1 s2 at2 sc2 rx3 c2
permutation rx1 s1 at1 wx sc1 s2 at2 rx3 sc2 c2
permutation rx1 s1 at1 wx sc1 s2 at2 rx3 c2 sc2
permutation rx1 s1 at1 wx sc1 s2 rx3 at2 sc2 c2
permutation rx1 s1 at1 wx sc1 s2 rx3 at2 c2 sc2
permutation rx1 s1 at1 wx sc1 s2 rx3 c2 at2 sc2
permutation rx1 s1 at1 wx sc1 rx3 s2 at2 sc2 c2
permutation rx1 s1 at1 wx sc1 rx3 s2 at2 c2 sc2
permutation rx1 s1 at1 wx sc1 rx3 s2 c2 at2 sc2
permutation rx1 s1 at1 wx sc1 rx3 c2 s2 at2 sc2
permutation rx1 s1 wx at1 rx3 c2 sc1 s2 at2 sc2
permutation rx1 s1 wx rx3 at1 c2 sc1 s2 at2 sc2
permutation rx1 s1 wx rx3 c2 at1 sc1 s2 at2 sc2
permutation rx1 wx s1 at1 rx3 c2 sc1 s2 at2 sc2
permutation rx1 wx s1 rx3 at1 c2 sc1 s2 at2 sc2
permutation rx1 wx s1 rx3 c2 at1 sc1 s2 at2 sc2
permutation rx1 wx rx3 s1 at1 c2 sc1 s2 at2 sc2
permutation rx1 wx rx3 s1 c2 at1 sc1 s2 at2 sc2
permutation rx1 wx rx3 c2 s1 at1 sc1 s2 at2 sc2

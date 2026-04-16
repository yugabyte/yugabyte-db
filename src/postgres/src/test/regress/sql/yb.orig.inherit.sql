-- Test non-inheritable primary key constraints
-- primary key on inh parent does not recurse
CREATE TABLE parent(id int PRIMARY KEY, c1 int);
CREATE TABLE child (c2 int) INHERITS (parent);
-- unique constraint on inh parent does not recurse
ALTER TABLE parent ADD CONSTRAINT parent_uniq_c1 UNIQUE(c1);
\d+ parent
\d+ child
ALTER TABLE parent DROP CONSTRAINT parent_pkey;
\d+ parent
\d+ child
ALTER TABLE parent DROP CONSTRAINT parent_uniq_c1;
\d+ parent
\d+ child
-- Test combining alter table cmds with inherit #27263
ALTER TABLE child NO INHERIT parent, ADD COLUMN c3 int;
SELECT * FROM child;
DROP TABLE parent, child;

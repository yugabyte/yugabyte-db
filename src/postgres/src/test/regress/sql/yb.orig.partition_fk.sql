--
-- Test PARTITIONed table interaction with FK constraints
--

CREATE TABLE referenced (a int PRIMARY KEY);


-- FK constraint attached to a partitioned table itself

CREATE TABLE partitioned (a int) PARTITION BY LIST (a);
CREATE TABLE partitioned_part PARTITION OF partitioned FOR VALUES IN (123);
ALTER TABLE partitioned ADD FOREIGN KEY (a) REFERENCES referenced;

INSERT INTO partitioned (a) VALUES (123);
SELECT tableoid::regclass, a FROM partitioned ORDER BY a;

INSERT INTO partitioned_part (a) VALUES (123);
SELECT tableoid::regclass, a FROM partitioned ORDER BY a;

DROP TABLE partitioned CASCADE;


-- FK constraint attached to a table partition

CREATE TABLE partitioned (a int) PARTITION BY LIST (a);
CREATE TABLE partitioned_part PARTITION OF partitioned FOR VALUES IN (123);
ALTER TABLE partitioned_part ADD FOREIGN KEY (a) REFERENCES referenced;

INSERT INTO partitioned (a) VALUES (123);
SELECT tableoid::regclass, a FROM partitioned ORDER BY a;

INSERT INTO partitioned_part (a) VALUES (123);
SELECT tableoid::regclass, a FROM partitioned ORDER BY a;

DROP TABLE partitioned CASCADE;


-- FK constraint attached to intermediate partition in a two-level partitioned scheme

CREATE TABLE partitioned (a int) PARTITION BY LIST (a);
CREATE TABLE partitioned_lvl1 PARTITION OF partitioned FOR VALUES IN (123) PARTITION BY LIST (a);
CREATE TABLE partitioned_lvl2 PARTITION OF partitioned_lvl1 FOR VALUES IN (123);

ALTER TABLE partitioned_lvl1 ADD FOREIGN KEY (a) REFERENCES referenced;

INSERT INTO partitioned (a) VALUES (123);
SELECT tableoid::regclass, a FROM partitioned ORDER BY a;

INSERT INTO partitioned_lvl1 (a) VALUES (123);
SELECT tableoid::regclass, a FROM partitioned ORDER BY a;

INSERT INTO partitioned_lvl2 (a) VALUES (123);
SELECT tableoid::regclass, a FROM partitioned ORDER BY a;

DROP TABLE partitioned CASCADE;

-- Cleanup

DROP TABLE referenced;
